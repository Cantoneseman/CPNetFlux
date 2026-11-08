#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cpnetflux/core/io/persistent_data_session.h"
#include "cpnetflux/core/io/work_executor.h"
#include "cpnetflux/storage/mapped_file_window.h"

namespace {
namespace io = cpnetflux::core::io;
namespace protocol = cpnetflux::core::protocol;
namespace checksum = cpnetflux::checksum;
using Clock = std::chrono::steady_clock;

void require(const cpnetflux::common::Status& status) {
    if (!status.isOk()) throw std::runtime_error(status.message());
}

std::uint64_t number(std::string_view text, std::uint64_t maximum) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value == 0 || value > maximum) throw std::runtime_error("numeric argument out of range");
    return value;
}

struct Lane {
    std::uint64_t bytes = 0, frames = 0;
    std::uint64_t leaseBytes = 0, directBytes = 0;
    cpnetflux::storage::MappedWriteStats mapped;
    Clock::time_point start{}, end{};
    std::string error;
};

// The unlinked file cannot survive an exception, SIGBUS, or process termination.
// No externally visible file is ever published by this diagnostic.
struct Target {
    io::UniqueFd fd;
    Target(const char* directory, std::uint64_t bytes) {
        if (!std::filesystem::is_directory(directory)) throw std::runtime_error("target directory required");
        auto path = (std::filesystem::path(directory) / "cpnetflux-direct-receive-XXXXXX").string();
        std::vector<char> name(path.begin(), path.end());
        name.push_back(0);
        fd.reset(::mkstemp(name.data()));
        if (!fd.isValid()) throw std::runtime_error("target mkstemp failed");
        if (::unlink(name.data()) != 0) throw std::runtime_error("target unlink failed");
        if (::ftruncate(fd.get(), static_cast<off_t>(bytes)) != 0) throw std::runtime_error("target resize failed");
    }
    void verify(std::uint64_t bytes) {
        std::vector<std::uint8_t> actual(1024 * 1024);
        for (std::uint64_t offset = 0; offset < bytes;) {
            const auto requested = std::min<std::uint64_t>(actual.size(), bytes - offset);
            const auto count = ::pread(fd.get(), actual.data(), requested, static_cast<off_t>(offset));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0 || !std::all_of(actual.begin(), actual.begin() + count,
                    [](auto byte) { return byte == 0x5a; }))
                throw std::runtime_error("independent full target readback failed");
            offset += static_cast<std::size_t>(count);
        }
    }
};

struct LeaseIo {
    io::WorkExecutor& executor;
    const io::WorkLease& lease;
    std::uint64_t bytes;
    bool active = false;
    LeaseIo(io::WorkExecutor& executor, const io::WorkLease& lease, std::uint64_t bytes)
        : executor(executor), lease(lease), bytes(bytes) {
        require(executor.startIo(lease, bytes));
        active = true;
    }
    ~LeaseIo() { if (active) (void)executor.finishIo(lease, bytes); }
    void finish() { require(executor.finishIo(lease, bytes)); active = false; }
};

void receiveMapped(io::FramedDataSocket& socket, const io::PersistentFileIdentity& identity,
                   std::size_t frameBytes, bool direct, Target& target, std::uint64_t base,
                   std::uint64_t targetBytes, io::DynamicFileQueue& queue, std::uint32_t worker,
                   Lane& result) {
    using cpnetflux::storage::MappedFileWindow;
    MappedFileWindow window(target.fd.get(), targetBytes);
    auto owner = queue.worker(worker);
    if (!owner.isOk()) throw std::runtime_error(owner.status().message());
    io::WorkExecutor executor(queue, owner.value());
    std::optional<io::WorkLease> lease;
    try {
        std::vector<std::uint8_t> buffer(direct ? 0 : frameBytes);
        io::PersistentFrame frame;
        io::PersistentDataSession session;
        const std::uint8_t ready = 0x5a;
        require(socket.writeAll(&ready, 1));
        result.start = Clock::now();
        require(io::PersistentDataSession::readNext(&socket, session.kMaxPayload, &frame));
        auto begin = io::PersistentDataSession::decodeBegin(frame);
        if (!begin.isOk()) throw std::runtime_error(begin.status().message());
        const auto& actual = begin.value();
        if (!io::samePersistentIdentity(actual, identity) || actual.rangeId != 0 ||
            actual.attempt != identity.attempt || actual.rangeOffset || actual.rangeLength ||
            actual.rangeCount || frame.header.flags)
            throw std::runtime_error("benchmark mapped identity mismatch");
        require(session.begin(frame.header, actual));
        auto claimed = executor.claimIncoming(io::WorkItem{identity, identity.transferId, false});
        if (!claimed.isOk()) throw std::runtime_error(claimed.status().message());
        lease = claimed.value();
        require(executor.begin(*lease));
        while (result.bytes < identity.totalSize) {
            const auto length = static_cast<std::size_t>(
                std::min<std::uint64_t>(frameBytes, identity.totalSize - result.bytes));
            protocol::EncodedFrameHeader encoded{};
            require(socket.readAll(encoded.data(), encoded.size()));
            auto decoded = protocol::decodeFrameHeader(encoded);
            if (!decoded.isOk()) throw std::runtime_error(decoded.status().message());
            auto header = decoded.value();
            require(protocol::validateFrameHeader(header, session.kMaxPayload));
            // This diagnostic accepts only full ordered CRC32C DATA. Preflight is
            // deliberately stricter than the general session and occurs before mapping.
            if (header.type != protocol::FrameType::Data || header.flags != protocol::kDataChecksum ||
                header.streamId != identity.fileId || header.chunkId != identity.generation ||
                header.rangeId || header.attempt != identity.attempt ||
                header.totalSize != identity.totalSize || header.statusCode != protocol::FrameStatusCode::Ok ||
                header.offset != result.bytes || header.payloadSize != length + 4)
                throw std::runtime_error("DATA preflight rejected");
            LeaseIo ioGuard(executor, *lease, length);
            std::array<std::uint8_t, 4> prefix{};
            checksum::ChecksumComputer crcComputer(identity.checksumAlgorithm);
            if (direct) {
                std::size_t completed = 0;
                while (completed < length) {
                    auto borrowed = window.writableSpan(base + result.bytes + completed, length - completed);
                    if (!borrowed.isOk()) throw std::runtime_error(borrowed.status().message());
                    auto span = borrowed.value();
                    if (!completed) require(socket.readAllV(prefix.data(), prefix.size(), span.data(), span.size()));
                    else require(socket.readAll(span.data(), span.size()));
                    // Consume the old view before the next borrow may unmap it.
                    crcComputer.update(span.data(), span.size());
                    completed += span.size();
                }
            } else {
                require(socket.readAllV(prefix.data(), prefix.size(), buffer.data(), length));
                crcComputer.update(buffer.data(), length);
            }
            const auto crc = crcComputer.finalize();
            const auto expected = (std::uint32_t(prefix[0]) << 24) | (std::uint32_t(prefix[1]) << 16) |
                (std::uint32_t(prefix[2]) << 8) | std::uint32_t(prefix[3]);
            if (crc.value != expected) throw std::runtime_error("DATA checksum mismatch");
            if (!direct) require(window.write(base + result.bytes, {buffer.data(), length}));
            header.flags = 0;
            header.payloadSize = static_cast<std::uint32_t>(length);
            require(session.dataWithChecksum(header, length, crc));
            ioGuard.finish();
            result.leaseBytes += length;
            result.directBytes += direct ? length : 0;
            result.bytes += length;
            ++result.frames;
        }
        require(io::PersistentDataSession::readNext(&socket, session.kMaxPayload, &frame));
        auto ended = session.end(frame.header, frame.payload.data(), frame.payload.size());
        if (!ended.isOk()) throw std::runtime_error(ended.status().message());
        // Retirement and lease completion are inside ready-to-ACK timing.
        require(window.close());
        require(executor.finalize(*lease, cpnetflux::common::Status::ok()));
        lease.reset();
        require(io::PersistentDataSession::writeResult(&socket, identity, protocol::FrameStatusCode::Ok));
        result.end = Clock::now();
        result.mapped = window.stats();
    } catch (const std::exception& error) {
        if (lease) {
            auto snapshot = queue.snapshot(*lease);
            if (!snapshot.isOk() || snapshot.value().inflightFrames || snapshot.value().inflightBytes)
                throw std::runtime_error("failed mapped receive did not drain IO lease");
            require(executor.reject(*lease));
        }
        result.mapped = window.stats();
        throw std::runtime_error(std::string(error.what()) + ";maps_before_failure=" +
                                 std::to_string(result.mapped.mapCalls));
    }
}

checksum::ChecksumValue digest(const std::uint8_t* bytes, std::size_t length,
                               checksum::ChecksumAlgorithm algorithm) {
    checksum::ChecksumComputer computer(algorithm);
    computer.update(bytes, length);
    return computer.finalize();
}

void send(io::FramedDataSocket& socket, const io::PersistentFileIdentity& identity,
          std::size_t frameBytes, bool framed, Lane& result) {
    std::vector<std::uint8_t> payload(frameBytes, 0x5a);
    const auto fullCrc = digest(payload.data(), payload.size(), identity.checksumAlgorithm);
    checksum::ChecksumComputer whole(identity.checksumAlgorithm);
    std::uint8_t ready = 0;
    require(socket.readAll(&ready, 1));
    if (ready != 0x5a) throw std::runtime_error("invalid receiver ready");
    result.start = Clock::now();
    if (framed) require(io::PersistentDataSession::writeBegin(&socket, identity));
    while (result.bytes < identity.totalSize) {
        const auto length = static_cast<std::size_t>(
            std::min<std::uint64_t>(frameBytes, identity.totalSize - result.bytes));
        if (framed) {
            require(io::PersistentDataSession::writeData(
                &socket, identity, result.bytes, payload.data(), length));
            const auto crc = length == frameBytes ? fullCrc
                : digest(payload.data(), length, identity.checksumAlgorithm);
            if (!whole.combine(crc, length)) throw std::runtime_error("checksum combine failed");
        } else {
            require(socket.writeAll(payload.data(), length));
        }
        result.bytes += length;
        ++result.frames;
    }
    if (framed) {
        require(io::PersistentDataSession::writeEnd(&socket, identity, whole.finalize()));
        auto reply = io::PersistentDataSession::readResult(&socket, identity);
        if (!reply.isOk()) throw std::runtime_error(reply.status().message());
        if (reply.value() != protocol::FrameStatusCode::Ok)
            throw std::runtime_error("receiver rejected stream");
    } else {
        std::uint8_t reply = 0;
        require(socket.readAll(&reply, 1));
        if (reply != 0xa5) throw std::runtime_error("receiver rejected raw stream");
    }
    result.end = Clock::now();
}

void receive(io::FramedDataSocket& socket, const io::PersistentFileIdentity& identity,
             std::size_t frameBytes, bool framed, Lane& result) {
    std::vector<std::uint8_t> raw(frameBytes);
    io::PersistentFrame frame;
    io::PersistentDataSession session;
    const std::uint8_t ready = 0x5a;
    require(socket.writeAll(&ready, 1));
    result.start = Clock::now();
    if (framed) {
        require(io::PersistentDataSession::readNext(&socket, session.kMaxPayload, &frame));
        auto begin = io::PersistentDataSession::decodeBegin(frame);
        if (!begin.isOk()) throw std::runtime_error(begin.status().message());
        const auto& actual = begin.value();
        if (actual.fileId != identity.fileId || actual.generation != identity.generation ||
            actual.totalSize != identity.totalSize || actual.relativePath != identity.relativePath ||
            actual.transferId != identity.transferId || actual.rangeId != 0 ||
            actual.checksumAlgorithm != identity.checksumAlgorithm)
            throw std::runtime_error("benchmark identity mismatch");
        require(session.begin(frame.header, actual));
    }
    while (result.bytes < identity.totalSize) {
        const auto length = static_cast<std::size_t>(
            std::min<std::uint64_t>(frameBytes, identity.totalSize - result.bytes));
        if (framed) {
            require(io::PersistentDataSession::readNext(&socket, session.kMaxPayload, &frame));
            const bool checked = identity.checksumAlgorithm == checksum::ChecksumAlgorithm::Crc32c;
            const auto prefix = checked ? 4U : 0U;
            if (frame.header.type != protocol::FrameType::Data ||
                frame.header.flags != (checked ? protocol::kDataChecksum : 0) ||
                frame.payload.size() != length + prefix)
                throw std::runtime_error("benchmark DATA shape mismatch");
            auto header = frame.header;
            header.flags = 0;
            header.payloadSize -= prefix;
            if (checked) {
                const auto crc = digest(frame.payload.data() + prefix, length,
                                         identity.checksumAlgorithm);
                const auto* p = frame.payload.data();
                const auto expected = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
                    (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
                if (crc.value != expected) throw std::runtime_error("DATA checksum mismatch");
                require(session.dataWithChecksum(header, length, crc));
            } else {
                require(session.data(header, length, frame.payload.data()));
            }
        } else {
            require(socket.readAll(raw.data(), length));
        }
        result.bytes += length;
        ++result.frames;
    }
    if (framed) {
        require(io::PersistentDataSession::readNext(&socket, session.kMaxPayload, &frame));
        auto ended = session.end(frame.header, frame.payload.data(), frame.payload.size());
        if (!ended.isOk()) throw std::runtime_error(ended.status().message());
        require(io::PersistentDataSession::writeResult(&socket, identity, protocol::FrameStatusCode::Ok));
    } else {
        const std::uint8_t reply = 0xa5;
        require(socket.writeAll(&reply, 1));
    }
    result.end = Clock::now();
}
}  // namespace

int main(int argc, const char* argv[]) {
    try {
        if (argc != 10 && argc != 12) throw std::runtime_error(
            "Usage: cpnetflux-tcp-frame-bench server|client local-ip peer-ip port connections "
            "bytes raw|v3 none|crc32c frame-bytes [mapped_copy|mapped_direct output-directory]");
        // Every parameter is explicit so a diagnostic cannot silently select another interface.
        const std::string_view role(argv[1]), mode(argv[7]), crcText(argv[8]);
        if ((role != "server" && role != "client") || (mode != "raw" && mode != "v3"))
            throw std::runtime_error("invalid role or mode");
        const auto port = static_cast<std::uint16_t>(number(argv[4], 65535));
        const auto connections = static_cast<std::uint32_t>(number(argv[5], 64));
        const auto bytes = number(argv[6], 1ULL << 40);
        const auto frameBytes = static_cast<std::size_t>(number(argv[9], 65532));
        auto algorithm = checksum::parseChecksumAlgorithm(crcText);
        if (!algorithm.isOk()) throw std::runtime_error(algorithm.status().message());
        if (bytes < connections || (mode == "raw" && crcText != "none"))
            throw std::runtime_error("invalid byte partition or raw checksum");
        auto backend = checksum::resolveChecksumBackend(algorithm.value(), checksum::ChecksumBackend::Auto);
        if (!backend.isOk()) throw std::runtime_error(backend.status().message());
        const bool server = role == "server", framed = mode == "v3";
        const auto setupStart = Clock::now();
        const std::string_view sink = argc == 12 ? argv[10] : "memory";
        const bool mapped = argc == 12;
        if (mapped && (!server || !framed || crcText != "crc32c" || connections > 4 ||
            bytes > 8ULL * 1024 * 1024 * 1024 || (sink != "mapped_copy" && sink != "mapped_direct")))
            throw std::runtime_error("mapped sink requires server/v3/crc32c,1..4 lanes,<=8GiB");
        std::unique_ptr<Target> target;
        std::unique_ptr<io::DynamicFileQueue> queue;
        if (mapped) {
            rlimit coreLimit{0, 0};
            if (::setrlimit(RLIMIT_CORE, &coreLimit) != 0) throw std::runtime_error("disable payload cores");
            target = std::make_unique<Target>(argv[11], bytes);
            queue = std::make_unique<io::DynamicFileQueue>(std::vector<io::PersistentFileIdentity>{},
                                                          connections, 1, connections);
        }
        const auto deadline = Clock::now() + std::chrono::seconds(60);
        std::vector<io::FramedDataSocket> sockets;
        sockets.reserve(connections);
        if (server) {
            auto listening = io::createListener(argv[2], port, static_cast<int>(connections));
            if (!listening.isOk()) throw std::runtime_error(listening.status().message());
            auto listener = std::move(listening.value());
            std::cout << "CPNETFLUX_TCP_FRAME_READY" << std::endl;
            while (sockets.size() < connections) {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                if (remaining <= 0) throw std::runtime_error("accept deadline expired");
                pollfd descriptor{listener.get(), POLLIN, 0};
                const int polled = ::poll(&descriptor, 1, static_cast<int>(remaining));
                if (polled < 0 && errno == EINTR) continue;
                if (polled <= 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
                    throw std::runtime_error("accept poll failed");
                const int fd = ::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC);
                if (fd < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                if (fd < 0) throw std::runtime_error("accept failed");
                sockets.emplace_back(io::UniqueFd(fd));
            }
        } else {
            for (std::uint32_t index = 0; index < connections; ++index) {
                auto connected = io::connectBlockingSocket(argv[3], port, deadline, argv[2]);
                if (!connected.isOk()) throw std::runtime_error(connected.status().message());
                sockets.emplace_back(std::move(connected.value()));
            }
        }
        for (auto& socket : sockets) require(io::PersistentDataSession::setTimeout(&socket, 30));
        const double setupSeconds = std::chrono::duration<double>(Clock::now() - setupStart).count();
        std::vector<Lane> results(connections);
        std::vector<std::jthread> workers;
        workers.reserve(connections);
        for (std::uint32_t index = 0; index < connections; ++index) {
            workers.emplace_back([&, index] {
                try {
                    io::PersistentFileIdentity identity;
                    identity.fileId = index + 1;
                    identity.generation = 1;
                    identity.totalSize = bytes / connections + (index < bytes % connections ? 1 : 0);
                    identity.relativePath = "memory.bin";
                    identity.transferId = "memory-attribution";
                    identity.checksumAlgorithm = algorithm.value();
                    if (mapped) {
                        const auto base = index * (bytes / connections) + std::min<std::uint64_t>(index, bytes % connections);
                        receiveMapped(sockets[index], identity, frameBytes, sink == "mapped_direct", *target,
                                      base, bytes, *queue, index, results[index]);
                    } else if (server) receive(sockets[index], identity, frameBytes, framed, results[index]);
                    else send(sockets[index], identity, frameBytes, framed, results[index]);
                } catch (const std::exception& error) {
                    results[index].error = error.what();
                    // Wake sibling workers; never acknowledge a partially received stream.
                    for (auto& socket : sockets) socket.shutdown();
                }
            });
        }
        for (auto& worker : workers) worker.join();
        std::uint64_t transferred = 0, frames = 0, maps = 0, unmaps = 0, active = 0, peaks = 0;
        std::uint64_t copied = 0, directBytes = 0, leaseBytes = 0;
        auto first = results.front().start, last = results.front().end, latestStart = first;
        for (const auto& lane : results) {
            if (!lane.error.empty()) throw std::runtime_error(lane.error);
            transferred += lane.bytes;
            frames += lane.frames;
            maps += lane.mapped.mapCalls;
            unmaps += lane.mapped.unmapCalls;
            active += lane.mapped.activeMappedBytes;
            peaks += lane.mapped.peakMappedBytes;
            copied += lane.mapped.writtenBytes;
            directBytes += lane.directBytes;
            leaseBytes += lane.leaseBytes;
            first = std::min(first, lane.start);
            last = std::max(last, lane.end);
            latestStart = std::max(latestStart, lane.start);
        }
        if (transferred != bytes) throw std::runtime_error("byte count mismatch");
        if (mapped && (queue->outstanding() || leaseBytes != bytes || active || maps != unmaps))
            throw std::runtime_error("mapped receive did not retire mappings and leases");
        const double elapsed = std::chrono::duration<double>(last - first).count();
        const auto verifyStart = Clock::now();
        if (target) target->verify(bytes);
        const double verifySeconds = target ? std::chrono::duration<double>(Clock::now() - verifyStart).count() : 0;
        rusage usage{};
        if (::getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
        const double cpu = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
            usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
        std::cout << std::setprecision(12)
            << "{\"benchmark\":\"" << (mapped ? "tcp_frame_mapped" : "tcp_frame_memory")
            << "\",\"diagnostic_only\":true,\"complete_v3_task\":false,\"file_io\":" << (mapped ? "true" : "false") << ','
            << "\"role\":\"" << role << "\",\"mode\":\"" << mode << "\",\"checksum\":\"" << crcText
            << "\",\"checksum_backend\":\"" << checksum::checksumBackendName(backend.value())
            << "\",\"connections\":" << connections << ",\"logical_bytes\":" << transferred
            << ",\"data_frames\":" << (framed ? frames : 0) << ",\"payload_chunks\":" << frames
            << ",\"frame_bytes\":" << frameBytes
            << ",\"receiver_acknowledged\":true,\"elapsed_seconds\":" << elapsed
            << ",\"setup_seconds\":" << setupSeconds
            << ",\"lane_start_spread_seconds\":" << std::chrono::duration<double>(latestStart - first).count()
            << ",\"throughput_gbps\":" << bytes * 8.0 / elapsed / 1e9
            << ",\"process_cpu_seconds\":" << cpu << ",\"max_rss_kib\":" << usage.ru_maxrss
            << ",\"sink\":\"" << sink << "\",\"readback_verified\":" << (mapped ? "true" : "false")
            << ",\"readback_seconds\":" << verifySeconds
            << ",\"process_wall_seconds\":" << std::chrono::duration<double>(Clock::now() - setupStart).count()
            << ",\"map_calls\":" << maps << ",\"unmap_calls\":" << unmaps
            << ",\"active_mapped_bytes\":" << active << ",\"peak_mapped_bytes_sum\":" << peaks
            << ",\"mapped_copy_bytes\":" << copied << ",\"direct_receive_bytes\":" << directBytes
            << ",\"lease_io_bytes\":" << leaseBytes
            << ",\"minor_faults\":" << usage.ru_minflt << ",\"major_faults\":" << usage.ru_majflt
            << ",\"voluntary_context_switches\":" << usage.ru_nvcsw
            << ",\"involuntary_context_switches\":" << usage.ru_nivcsw << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
