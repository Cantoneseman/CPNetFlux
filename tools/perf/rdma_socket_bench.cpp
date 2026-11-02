#include "cpnetflux/core/io/rdma_data_socket.h"
#include "cpnetflux/core/protocol/frame.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <ctime>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using cpnetflux::common::Status;
using cpnetflux::core::io::RdmaDataSocket;
using cpnetflux::core::io::RdmaListener;
using cpnetflux::core::io::RdmaTransportStats;
using cpnetflux::core::io::kRdmaDataMessageBufferBytes;
using cpnetflux::core::protocol::EncodedFrameHeader;
using cpnetflux::core::protocol::FrameHeader;
using cpnetflux::core::protocol::FrameType;

constexpr std::uint8_t kPayloadByte = 0x5a;
constexpr std::uint8_t kCompletionByte = 0xa5;
constexpr std::size_t kPayloadBytes = 64U << 10;

void phase(const char* name, double seconds = 0.0) {
    if (std::getenv("CPNETFLUX_RDMA_DIAGNOSTICS") == nullptr) return;
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const char* port = std::getenv("CPNETFLUX_RDMA_BENCH_SYSFS_PORT");
    std::cout << "CPNETFLUX_RDMA_PHASE {\"phase\":\"" << name
              << "\",\"pid\":" << ::getpid() << ",\"monotonic_ns\":" << stamp
              << ",\"seconds\":" << seconds << ",\"counters\":{";
    bool comma = false;
    if (port != nullptr) {
        for (const char* field : {"out_of_buffer", "rnr_nak_retry_err", "req_cqe_error",
             "resp_cqe_error", "req_cqe_flush_error", "resp_cqe_flush_error",
             "local_ack_timeout_err", "req_transport_retries_exceeded"}) {
            std::ifstream input(std::string(port) + "/hw_counters/" + field);
            std::uint64_t value = 0;
            if (input >> value) {
                if (comma) std::cout << ',';
                std::cout << '\"' << field << "\":" << value;
                comma = true;
            }
        }
    }
    std::cout << "}}" << std::endl;
}

int finish(RdmaDataSocket& socket) {
    if (std::getenv("CPNETFLUX_RDMA_DIAGNOSTICS") == nullptr) return 0;
    phase("payload_complete");
    if (std::getenv("CPNETFLUX_RDMA_BENCH_CLOSE_BARRIER") != nullptr) {
        std::string command;
        if (!std::getline(std::cin, command) || command != "close") {
            std::cerr << "RDMA close barrier requires close command\n";
            return 1;
        }
    }
    const auto started = std::chrono::steady_clock::now();
    socket.close();
    phase("socket_closed", std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count());
    return 0;
}

bool parseUnsigned(std::string_view text, std::uint64_t* value) {
    if (value == nullptr || text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), *value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

void printResult(const char* role, std::uint64_t logicalBytes, std::uint64_t wireBytes,
                 std::uint64_t frames, double elapsedSeconds,
                 double processCpuSeconds,
                 const RdmaTransportStats& stats) {
    const double throughputGbps = elapsedSeconds > 0.0
        ? static_cast<double>(logicalBytes) * 8.0 / elapsedSeconds / 1.0e9
        : 0.0;
    const double wireThroughputGbps = elapsedSeconds > 0.0
        ? static_cast<double>(wireBytes) * 8.0 / elapsedSeconds / 1.0e9
        : 0.0;
    std::cout << "CPNETFLUX_RDMA_SOCKET_BENCH {\"role\":\"" << role
              << "\",\"result\":\"pass\",\"logical_bytes\":" << logicalBytes
              << ",\"wire_bytes\":" << wireBytes
              << ",\"frame_count\":" << frames
              << ",\"elapsed_seconds\":" << elapsedSeconds
              << ",\"process_cpu_seconds\":" << processCpuSeconds
              << ",\"throughput_gbps\":" << throughputGbps
              << ",\"wire_throughput_gbps\":" << wireThroughputGbps
              << ",\"configured_send_queue_depth\":"
              << cpnetflux::core::io::kRdmaDataQueueDepth
              << ",\"configured_receive_queue_depth\":"
              << cpnetflux::core::io::kRdmaDataReceiveQueueDepth
              << ",\"negotiated_send_queue_depth\":"
              << stats.negotiatedSendQueueDepth
              << ",\"negotiated_receive_queue_depth\":"
              << stats.negotiatedReceiveQueueDepth
              << ",\"negotiated_path_mtu_bytes\":"
              << stats.negotiatedPathMtuBytes
              << ",\"negotiated_min_rnr_timer\":"
              << stats.negotiatedMinRnrTimer
              << ",\"negotiated_rnr_retry\":"
              << stats.negotiatedRnrRetry
              << ",\"slot_bytes\":" << kRdmaDataMessageBufferBytes
              << ",\"send_bytes\":" << stats.sendBytes
              << ",\"send_work_requests\":" << stats.sendWorkRequests
              << ",\"send_completions\":" << stats.sendCompletions
              << ",\"send_completion_poll_calls\":"
              << stats.sendCompletionPollCalls
              << ",\"send_completion_event_waits\":"
              << stats.sendCompletionEventWaits
              << ",\"send_completion_wait_seconds\":"
              << static_cast<double>(stats.sendCompletionWaitNanoseconds) / 1.0e9
              << ",\"max_send_inflight\":" << stats.maxSendInFlight
              << ",\"receive_bytes\":" << stats.receiveBytes
              << ",\"receive_completions\":" << stats.receiveCompletions
              << ",\"receive_post_calls\":" << stats.receivePostCalls
              << ",\"receive_reposts\":" << stats.receiveReposts
              << ",\"receive_completion_poll_calls\":"
              << stats.receiveCompletionPollCalls
              << ",\"receive_ready_queue_high_watermark\":" << stats.receiveReadyQueueHighWatermark
              << ",\"receive_completion_event_waits\":"
              << stats.receiveCompletionEventWaits
              << ",\"receive_completion_wait_seconds\":"
              << static_cast<double>(stats.receiveCompletionWaitNanoseconds) / 1.0e9
              << ",\"initialize_ns\":" << stats.queuePairInitializeNanoseconds
              << ",\"buffer_allocate_ns\":" << stats.bufferAllocateNanoseconds
              << ",\"memory_register_ns\":" << stats.memoryRegisterNanoseconds
              << ",\"receive_prepost_ns\":" << stats.receivePrepostNanoseconds
              << ",\"send_max_wait_ns\":" << stats.sendCompletionMaxWaitNanoseconds
              << ",\"receive_max_wait_ns\":" << stats.receiveCompletionMaxWaitNanoseconds
              << "}\n";
}

int runDuplex(RdmaDataSocket& socket, const char* role, std::uint64_t bytes) {
    Status writeStatus = Status::ok();
    const auto cpuStarted = std::clock();
    const auto started = std::chrono::steady_clock::now();
    std::thread writer([&]() {
        std::vector<std::uint8_t> payload(kPayloadBytes);
        for (std::uint64_t offset = 0; offset < bytes;) {
            const auto length = static_cast<std::size_t>(
                std::min<std::uint64_t>(payload.size(), bytes - offset));
            const auto pattern = static_cast<std::uint8_t>(offset / kPayloadBytes);
            std::fill(payload.begin(), payload.end(), pattern);
            FrameHeader header;
            header.type = FrameType::Data;
            header.streamId = 1;
            header.chunkId = 1;
            header.offset = offset;
            header.totalSize = bytes;
            header.payloadSize = static_cast<std::uint32_t>(length);
            const auto encoded = cpnetflux::core::protocol::encodeFrameHeader(header);
            writeStatus = socket.writeFrameBuffered(
                encoded.data(), encoded.size(), nullptr, 0, payload.data(), length);
            if (!writeStatus.isOk()) {
                socket.shutdown();
                return;
            }
            offset += length;
            std::this_thread::yield();
        }
        const std::uint8_t completion = kCompletionByte;
        writeStatus = socket.writeAll(&completion, sizeof(completion));
        if (!writeStatus.isOk()) socket.shutdown();
    });
    Status readStatus = Status::ok();
    std::uint64_t frames = 0;
    std::uint64_t wireBytes = 0;
    std::vector<std::uint8_t> payload(kPayloadBytes);
    for (std::uint64_t offset = 0; offset < bytes;) {
        const auto length = static_cast<std::size_t>(
            std::min<std::uint64_t>(payload.size(), bytes - offset));
        EncodedFrameHeader encoded{};
        readStatus = socket.readAll(encoded.data(), encoded.size());
        if (!readStatus.isOk()) break;
        const auto decoded = cpnetflux::core::protocol::decodeFrameHeader(encoded);
        if (!decoded.isOk() || decoded.value().type != FrameType::Data ||
            decoded.value().streamId != 1 || decoded.value().chunkId != 1 ||
            decoded.value().offset != offset || decoded.value().totalSize != bytes ||
            decoded.value().payloadSize != length) {
            readStatus = Status::runtimeError("duplex frame ordering/identity mismatch");
            break;
        }
        readStatus = socket.readAll(payload.data(), length);
        if (!readStatus.isOk()) break;
        const auto pattern = static_cast<std::uint8_t>(offset / kPayloadBytes);
        if (!std::all_of(payload.begin(), payload.begin() + length,
                         [pattern](std::uint8_t value) { return value == pattern; })) {
            readStatus = Status::runtimeError("duplex payload verification failed");
            break;
        }
        offset += length;
        ++frames;
        wireBytes += encoded.size() + length;
    }
    if (readStatus.isOk()) {
        std::uint8_t completion = 0;
        readStatus = socket.readAll(&completion, sizeof(completion));
        if (readStatus.isOk() && completion != kCompletionByte)
            readStatus = Status::runtimeError("duplex completion marker mismatch");
    }
    if (!readStatus.isOk()) socket.shutdown();
    writer.join();
    if (!readStatus.isOk() || !writeStatus.isOk()) {
        std::cerr << "RDMA duplex failed: read=" << readStatus.message()
                  << " write=" << writeStatus.message() << '\n';
        return 1;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const double cpu = static_cast<double>(std::clock() - cpuStarted) / CLOCKS_PER_SEC;
    printResult(role, bytes, wireBytes, frames, elapsed, cpu, socket.stats());
    return finish(socket);
}

int runServer(const char* host, std::uint16_t port, std::uint64_t bytes, bool duplex = false) {
    const auto setupStarted = std::chrono::steady_clock::now();
    auto listener = RdmaListener::listen(host, port, 1);
    if (!listener.isOk()) {
        std::cerr << "RDMA listen failed: " << listener.status().message() << '\n';
        return 1;
    }
    std::cout << "CPNETFLUX_RDMA_BENCH_READY" << std::endl;
    auto accepted = listener.value().accept();
    if (!accepted.isOk()) {
        std::cerr << "RDMA accept failed: " << accepted.status().message() << '\n';
        return 1;
    }
    auto socket = std::move(accepted.value());
    phase("transport_ready", std::chrono::duration<double>(
        std::chrono::steady_clock::now() - setupStarted).count());
    if (duplex) return runDuplex(socket, "duplex-server", bytes);
    std::vector<std::uint8_t> payload(kPayloadBytes, kPayloadByte);
    std::uint64_t completed = 0;
    std::uint64_t frames = 0;
    std::uint64_t wireBytes = 0;
    const auto cpuStarted = std::clock();
    const auto started = std::chrono::steady_clock::now();
    while (completed < bytes) {
        const auto length = static_cast<std::size_t>(
            std::min<std::uint64_t>(payload.size(), bytes - completed));
        EncodedFrameHeader encoded{};
        auto status = socket.readAll(encoded.data(), encoded.size());
        if (!status.isOk()) {
            std::cerr << "RDMA frame header read failed: " << status.message() << '\n';
            return 1;
        }
        auto decoded = cpnetflux::core::protocol::decodeFrameHeader(encoded);
        if (!decoded.isOk() || decoded.value().type != FrameType::Data ||
            decoded.value().streamId != 1 || decoded.value().chunkId != 1 ||
            decoded.value().offset != completed || decoded.value().totalSize != bytes ||
            decoded.value().payloadSize != length) {
            std::cerr << "RDMA frame header verification failed at byte " << completed << '\n';
            return 1;
        }
        status = socket.readAll(payload.data(), length);
        if (!status.isOk()) {
            std::cerr << "RDMA frame payload read failed: " << status.message() << '\n';
            return 1;
        }
        if (!std::all_of(payload.begin(), payload.begin() + length,
                         [](std::uint8_t value) { return value == kPayloadByte; })) {
            std::cerr << "RDMA payload verification failed at byte " << completed << '\n';
            return 1;
        }
        completed += length;
        ++frames;
        wireBytes += encoded.size() + length;
    }
    const std::uint8_t completion = kCompletionByte;
    auto status = socket.writeAll(&completion, sizeof(completion));
    if (!status.isOk()) {
        std::cerr << "RDMA completion reply failed: " << status.message() << '\n';
        return 1;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const double processCpuSeconds = static_cast<double>(std::clock() - cpuStarted) /
                                     static_cast<double>(CLOCKS_PER_SEC);
    printResult("server", completed, wireBytes, frames, elapsed,
                processCpuSeconds, socket.stats());
    return finish(socket);
}

int runClient(const char* host, std::uint16_t port, std::uint64_t bytes,
              std::string_view localBindAddress, bool duplex = false) {
    const auto setupStarted = std::chrono::steady_clock::now();
    auto connected = RdmaDataSocket::connect(host, port, {}, localBindAddress);
    if (!connected.isOk()) {
        std::cerr << "RDMA connect failed: " << connected.status().message() << '\n';
        return 1;
    }
    auto socket = std::move(connected.value());
    phase("transport_ready", std::chrono::duration<double>(
        std::chrono::steady_clock::now() - setupStarted).count());
    if (duplex) return runDuplex(socket, "duplex-client", bytes);
    std::vector<std::uint8_t> payload(kPayloadBytes, kPayloadByte);
    std::uint64_t completed = 0;
    std::uint64_t frames = 0;
    std::uint64_t wireBytes = 0;
    const auto cpuStarted = std::clock();
    const auto started = std::chrono::steady_clock::now();
    while (completed < bytes) {
        const auto length = static_cast<std::size_t>(
            std::min<std::uint64_t>(payload.size(), bytes - completed));
        FrameHeader header;
        header.type = FrameType::Data;
        header.streamId = 1;
        header.chunkId = 1;
        header.offset = completed;
        header.totalSize = bytes;
        header.payloadSize = static_cast<std::uint32_t>(length);
        const auto encoded = cpnetflux::core::protocol::encodeFrameHeader(header);
        auto status = socket.writeFrameBuffered(encoded.data(), encoded.size(), nullptr, 0,
                                                payload.data(), length);
        if (!status.isOk()) {
            std::cerr << "RDMA frame write failed: " << status.message() << '\n';
            return 1;
        }
        completed += length;
        ++frames;
        wireBytes += encoded.size() + length;
    }
    auto status = socket.flushWrites();
    if (!status.isOk()) {
        std::cerr << "RDMA final buffered write failed: " << status.message() << '\n';
        return 1;
    }
    std::uint8_t completion = 0;
    status = socket.readAll(&completion, sizeof(completion));
    if (!status.isOk() || completion != kCompletionByte) {
        std::cerr << "RDMA completion reply was not received or was invalid\n";
        return 1;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const double processCpuSeconds = static_cast<double>(std::clock() - cpuStarted) /
                                     static_cast<double>(CLOCKS_PER_SEC);
    printResult("client", completed, wireBytes, frames, elapsed,
                processCpuSeconds, socket.stats());
    return finish(socket);
}

}  // namespace

int main(int argc, char** argv) {
    phase("process_start");
    if (argc != 5 && argc != 6) {
        std::cerr << "Usage: " << argv[0]
                  << " server <bind-ip> <port> <bytes>\n       " << argv[0]
                  << " client <peer-ip> <port> <bytes> <local-bind-ip>\n"
                  << "Use duplex-server/duplex-client for concurrent read/write regression.\n";
        return 2;
    }
    const std::string_view mode(argv[1]);
    std::uint64_t portValue = 0;
    std::uint64_t bytes = 0;
    if (!parseUnsigned(argv[3], &portValue) || portValue == 0 || portValue > 65535 ||
        !parseUnsigned(argv[4], &bytes) || bytes == 0) {
        std::cerr << "Port and byte count must be positive decimal integers\n";
        return 2;
    }
    const auto port = static_cast<std::uint16_t>(portValue);
    if (mode == "server" && argc == 5) return runServer(argv[2], port, bytes);
    if (mode == "client" && argc == 6)
        return runClient(argv[2], port, bytes, argv[5]);
    if (mode == "duplex-server" && argc == 5)
        return runServer(argv[2], port, bytes, true);
    if (mode == "duplex-client" && argc == 6)
        return runClient(argv[2], port, bytes, argv[5], true);
    std::cerr << "Invalid mode or argument count\n";
    return 2;
}
