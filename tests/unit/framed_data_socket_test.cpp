#include "cpnetflux/core/io/framed_data_socket.h"
#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/core/protocol/compressed_data.h"

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <future>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using cpnetflux::common::Status;
using cpnetflux::common::StatusCode;
using cpnetflux::core::io::FramedDataSocket;
using cpnetflux::core::io::UniqueFd;

std::atomic<std::size_t> observedAllocationSize{0};
std::atomic<std::size_t> observedAllocations{0};

struct DataWrite {
    cpnetflux::core::protocol::FrameHeader header;
    std::vector<std::size_t> spans;
    std::vector<std::uint8_t> body;
};
struct SendScript {
    std::vector<ssize_t> steps;
    std::size_t next = 0;
    std::size_t sendCalls = 0, sendmsgCalls = 0;
    bool noSignal = true;
    std::promise<void>* firstSendReturned = nullptr;
    bool timeoutCleared = false;
    bool observeData = false;
    std::vector<DataWrite> dataWrites;
};
thread_local SendScript* currentScript = nullptr;
struct ObserveSends {
    SendScript* previous;
    explicit ObserveSends(SendScript& script) : previous(currentScript) { currentScript = &script; }
    ~ObserveSends() { currentScript = previous; }
};
struct SocketPair {
    FramedDataSocket sender;
    UniqueFd receiver;
    SocketPair() {
        int sockets[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
            throw std::runtime_error("socketpair");
        sender = FramedDataSocket(UniqueFd(sockets[0]));
        receiver.reset(sockets[1]);
        const timeval timeout{2, 0};
        if (::setsockopt(sender.fd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0 ||
            ::setsockopt(receiver.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
            throw std::runtime_error("socket timeout");
    }
};
std::vector<std::uint8_t> readBytes(int fd, std::size_t length) {
    std::vector<std::uint8_t> bytes(length);
    std::size_t completed = 0;
    while (completed < length) {
        const auto received = ::recv(fd, bytes.data() + completed, length - completed, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) break;
        completed += static_cast<std::size_t>(received);
    }
    bytes.resize(completed);
    return bytes;
}
std::vector<std::uint8_t> testBytes(std::size_t length) {
    std::vector<std::uint8_t> bytes(length);
    for (std::size_t index = 0; index < length; ++index)
        bytes[index] = static_cast<std::uint8_t>((index * 17U + 31U) % 251U);
    return bytes;
}
}  // namespace

extern "C" ssize_t __real_send(int fd, const void* bytes, std::size_t length, int flags);
extern "C" ssize_t __real_sendmsg(int fd, const msghdr* message, int flags);

#if CPNETFLUX_TEST_WRAP_NEW
extern "C" void* __real__Znwm(std::size_t size);
extern "C" void* __wrap__Znwm(std::size_t size) {
    if (size == observedAllocationSize.load(std::memory_order_relaxed))
        observedAllocations.fetch_add(1, std::memory_order_relaxed);
    return __real__Znwm(size);
}
#endif

extern "C" ssize_t __wrap_send(int fd, const void* bytes, std::size_t length, int flags) {
    if (currentScript) ++currentScript->sendCalls;
    return __real_send(fd, bytes, length, flags);
}

extern "C" ssize_t __wrap_sendmsg(int fd, const msghdr* message, int flags) {
    if (!currentScript) return __real_sendmsg(fd, message, flags);
    auto& script = *currentScript;
    ++script.sendmsgCalls;
    script.noSignal = script.noSignal && (flags & MSG_NOSIGNAL) != 0;
    if (script.observeData && message->msg_iovlen > 1 &&
        message->msg_iov[0].iov_len == cpnetflux::core::protocol::kFrameHeaderSize) {
        cpnetflux::core::protocol::EncodedFrameHeader encoded;
        std::copy_n(static_cast<const std::uint8_t*>(message->msg_iov[0].iov_base),
                    encoded.size(), encoded.begin());
        const auto header = cpnetflux::core::protocol::decodeFrameHeader(encoded);
        if (header.isOk() && header.value().type == cpnetflux::core::protocol::FrameType::Data) {
            DataWrite observed{header.value(), {}, {}};
            for (std::size_t index = 0; index < message->msg_iovlen; ++index) {
                const auto& span = message->msg_iov[index];
                observed.spans.push_back(span.iov_len);
                if (index != 0) {
                    const auto* bytes = static_cast<const std::uint8_t*>(span.iov_base);
                    observed.body.insert(observed.body.end(), bytes, bytes + span.iov_len);
                }
            }
            script.dataWrites.push_back(std::move(observed));
        }
    }
    if (script.next == script.steps.size()) {
        const auto result = __real_sendmsg(fd, message, flags);
        if (script.firstSendReturned) {
            // Only the first real call times out; subsequent calls can wait for the reader.
            const timeval timeout{};
            script.timeoutCleared = ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                                                 &timeout, sizeof(timeout)) == 0;
            script.firstSendReturned->set_value();
            script.firstSendReturned = nullptr;
        }
        return result;
    }
    const auto step = script.steps[script.next++];
    if (step < 0) { errno = static_cast<int>(-step); return -1; }
    if (step == 0) return 0;
    // Only cap this syscall; real sockets still carry the bytes checked by the reader.
    std::vector<iovec> capped;
    auto remaining = static_cast<std::size_t>(step);
    for (std::size_t index = 0; index < message->msg_iovlen && remaining > 0; ++index) {
        const auto count = std::min(remaining, message->msg_iov[index].iov_len);
        capped.push_back(iovec{message->msg_iov[index].iov_base, count});
        remaining -= count;
    }
    msghdr limited = *message;
    limited.msg_iov = capped.data();
    limited.msg_iovlen = capped.size();
    return __real_sendmsg(fd, &limited, flags);
}

TEST(FramedDataSocketTest, BufferedFrameWritePreservesTcpByteStreamBytes) {
    const auto header = testBytes(17);
    const auto prefix = testBytes(4);
    const auto payload = testBytes(257);
    SocketPair pair;

    ASSERT_TRUE(pair.sender.writeFrameBuffered(header.data(), header.size(),
                                               prefix.data(), prefix.size(),
                                               payload.data(), payload.size()).isOk());

    auto expected = header;
    expected.insert(expected.end(), prefix.begin(), prefix.end());
    expected.insert(expected.end(), payload.begin(), payload.end());
    EXPECT_EQ(readBytes(pair.receiver.get(), expected.size()), expected);
}

TEST(FramedDataSocketTest, PersistentSenderBoundsPayloadAllocationsAcrossManyFrames) {
#if !CPNETFLUX_TEST_WRAP_NEW
    GTEST_SKIP() << "requires the Linux 64-bit allocation wrapper";
#else
    using namespace cpnetflux::core::io;
    using cpnetflux::checksum::ChecksumAlgorithm;
    struct Directory {
        std::filesystem::path path;
        Directory() {
            char pattern[] = "/tmp/cpnetflux-buffer-unit-XXXXXX";
            const auto* name = ::mkdtemp(pattern);
            if (!name) throw std::runtime_error("mkdtemp");
            path = name;
        }
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    } source, destination;
    struct Depth {
        std::optional<std::string> previous;
        Depth() {
            const auto* value = std::getenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH");
            if (value) previous = value;
            ::setenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH", "2", 1);
        }
        ~Depth() {
            if (previous) ::setenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH", previous->c_str(), 1);
            else ::unsetenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH");
        }
    } depth;
    constexpr std::size_t bufferBytes = 65532;
    const auto content = testBytes(4 * 1024 * 1024 + 37);
    {
        std::ofstream output(source.path / "buffers.bin", std::ios::binary);
        output.write(reinterpret_cast<const char*>(content.data()), content.size());
    }
    struct stat metadata{};
    ASSERT_EQ(::stat((source.path / "buffers.bin").c_str(), &metadata), 0);
    PersistentFileIdentity id{1, 7, content.size(), "buffers.bin", "buffer-budget",
        1048576, metadata.st_mtim.tv_sec, ChecksumAlgorithm::Crc32c};
    int sockets[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    FramedDataSocket sender{UniqueFd(sockets[0])}, receiver{UniqueFd(sockets[1])};
    PersistentTreeStats received, sent;
    Status receiveStatus;
    std::jthread reader([&] {
        receiveStatus = receivePersistentTree(&receiver, destination.path.string(), false, "", &received);
    });
    observedAllocations.store(0, std::memory_order_relaxed);
    observedAllocationSize.store(bufferBytes, std::memory_order_relaxed);
    const auto sendStatus = sendPersistentTree(&sender, source.path.string(), {id}, &sent);
    observedAllocationSize.store(0, std::memory_order_relaxed);
    const auto allocations = observedAllocations.load(std::memory_order_relaxed);
    if (!sendStatus.isOk()) receiver.shutdown();
    reader.join();
    ASSERT_TRUE(sendStatus.isOk()) << sendStatus.message();
    ASSERT_TRUE(receiveStatus.isOk()) << receiveStatus.message();
    EXPECT_GT(allocations, 0U) << "the observer must detect a real initial payload allocation";
    EXPECT_LE(allocations, 4U) << "depth two plus one producer and one consumer buffer";
    std::ifstream output(destination.path / "buffers.bin", std::ios::binary);
    const std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(output)), {});
    EXPECT_EQ(actual, content);
#endif
}

TEST(FramedDataSocketTest, PersistentSenderSeparatesCrcFromPayloadWithIdenticalWireContent) {
    using namespace cpnetflux::core::io;
    using namespace cpnetflux::core::protocol;
    using cpnetflux::checksum::ChecksumAlgorithm;
    struct Directory {
        std::filesystem::path path;
        Directory() {
            char pattern[] = "/tmp/cpnetflux-span-unit-XXXXXX";
            const auto* name = ::mkdtemp(pattern);
            if (!name) throw std::runtime_error("mkdtemp");
            path = name;
        }
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    };
    const auto oracleCrc = [](const std::vector<std::uint8_t>& bytes) {
        std::uint32_t crc = 0xffffffffU;
        for (const auto byte : bytes) {
            crc ^= byte;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1U) ^ ((crc & 1U) ? 0x82f63b78U : 0U);
        }
        return crc ^ 0xffffffffU;
    };
    struct ProfileSetting {
        std::optional<std::string> previous;
        explicit ProfileSetting(bool enabled) {
            const auto* value = std::getenv("CPNETFLUX_RECEIVER_PROFILE");
            if (value) previous = value;
            ::setenv("CPNETFLUX_RECEIVER_PROFILE", enabled ? "1" : "0", 1);
        }
        ~ProfileSetting() {
            if (previous) ::setenv("CPNETFLUX_RECEIVER_PROFILE", previous->c_str(), 1);
            else ::unsetenv("CPNETFLUX_RECEIVER_PROFILE");
        }
    };
    for (const bool profileEnabled : {false, true}) {
    ProfileSetting profile(profileEnabled);
    for (const auto algorithm : {ChecksumAlgorithm::None, ChecksumAlgorithm::Crc32c}) {
        for (const bool compressed : {false, true}) {
            Directory source, destination;
            const auto content = testBytes(2 * 70001 + 17);
            {
                std::ofstream output(source.path / "spans.bin", std::ios::binary);
                output.write(reinterpret_cast<const char*>(content.data()), content.size());
            }
            struct stat metadata{};
            ASSERT_EQ(::stat((source.path / "spans.bin").c_str(), &metadata), 0);
            PersistentFileIdentity id{1, 7, content.size(), "spans.bin", "span-test",
                70001, metadata.st_mtim.tv_sec, algorithm};
            cpnetflux::config::FileTransferOptions options;
            options.hotPathCompression.enabled = options.hotPathCompression.candidate = compressed;
            PersistentTransferPolicy policy;
            policy.options = options;
            auto context = makePersistentTransferContext(policy);
            int sockets[2];
            ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
            FramedDataSocket sender{UniqueFd(sockets[0])}, receiver{UniqueFd(sockets[1])};
            PersistentTreeStats received, sent;
            received.phaseTiming = true;
            Status receiveStatus;
            std::jthread reader([&] {
                receiveStatus = receivePersistentTree(&receiver, destination.path.string(),
                    false, "", &received, {}, 1, 0, 1, 1, false, false, context);
            });
            SendScript script;
            script.observeData = true;
            Status sendStatus;
            {
                ObserveSends observe(script);
                sendStatus = sendPersistentTree(&sender, source.path.string(), {id}, &sent,
                    {}, 1, 0, 1, nullptr, &options);
            }
            if (!sendStatus.isOk()) receiver.shutdown();
            reader.join();
            ASSERT_TRUE(sendStatus.isOk()) << sendStatus.message();
            ASSERT_TRUE(receiveStatus.isOk()) << receiveStatus.message();
            ASSERT_FALSE(script.dataWrites.empty());
            std::size_t offset = 0;
            for (const auto& write : script.dataWrites) {
                const bool crc = algorithm == ChecksumAlgorithm::Crc32c;
                EXPECT_EQ(write.spans.size(), crc ? 3U : 2U);
                if (crc && write.spans.size() == 3) EXPECT_EQ(write.spans[1], 4U);
                EXPECT_EQ(write.header.offset, offset);
                EXPECT_EQ(write.header.payloadSize, write.body.size());
                EXPECT_EQ(bool(write.header.flags & kDataChecksum), crc);
                const auto prefix = crc ? 4U : 0U;
                ASSERT_GT(write.body.size(), prefix);
                std::vector<std::uint8_t> logical(write.body.begin() + prefix, write.body.end());
                const auto expectedLength = std::min<std::size_t>({65532,
                    content.size() - offset, id.chunkSize - offset % id.chunkSize});
                const bool frameCompressed = (write.header.flags & kDataCompressed) != 0;
                EXPECT_TRUE(compressed || !frameCompressed);
                if (frameCompressed) {
                    auto decoded = decodeCompressedDataPayload(logical.data(), logical.size(),
                        expectedLength, 65536);
                    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
                    logical = std::move(decoded.value());
                }
                EXPECT_EQ(logical.size(), expectedLength);
                ASSERT_LE(offset + logical.size(), content.size());
                EXPECT_TRUE(std::equal(logical.begin(), logical.end(), content.begin() + offset));
                if (crc) {
                    const auto expected = oracleCrc(logical);
                    const std::array<std::uint8_t, 4> bytes{
                        static_cast<std::uint8_t>(expected >> 24U), static_cast<std::uint8_t>(expected >> 16U),
                        static_cast<std::uint8_t>(expected >> 8U), static_cast<std::uint8_t>(expected)};
                    EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), write.body.begin()));
                }
                offset += logical.size();
            }
            EXPECT_EQ(offset, content.size());
            if (compressed) EXPECT_GT(sent.runtime.compressedFrames, 0U);
            else EXPECT_EQ(sent.runtime.compressedFrames, 0U);
            std::ifstream output(destination.path / "spans.bin", std::ios::binary);
            const std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(output)), {});
            EXPECT_EQ(actual, content);
            std::ostringstream timing;
            appendPersistentFileTimings(timing, received.fileTimings);
            if (!profileEnabled) {
                EXPECT_NE(timing.str().find("\"receiver_payload_observation\":null"), std::string::npos);
            } else {
                EXPECT_NE(timing.str().find("\"schema\":\"cpnetflux_receiver_payload_observation_v1\""), std::string::npos);
                std::uint64_t wireBytes = 0, shiftedBytes = 0, decodedFrames = 0;
                for (const auto& write : script.dataWrites) {
                    wireBytes += write.body.size();
                    if (algorithm == ChecksumAlgorithm::Crc32c) shiftedBytes += write.body.size() - 4;
                    if (write.header.flags & kDataCompressed) ++decodedFrames;
                }
                const auto count = [&](const char* key, std::uint64_t expected) {
                    EXPECT_NE(timing.str().find(std::string("\"") + key + "\":" +
                        std::to_string(expected) + ","), std::string::npos) << key;
                };
                const auto frames = script.dataWrites.size();
                count("data_frame_count", frames);
                count("frame_payload_allocations", frames);
                count("frame_payload_bytes", wireBytes);
                count("logical_buffer_allocations", frames);
                count("logical_copy_bytes", wireBytes);
                count("prefix_shift_calls", algorithm == ChecksumAlgorithm::Crc32c ? frames : 0);
                count("prefix_shift_bytes", shiftedBytes);
                count("decompression_calls", decodedFrames);
                count("frame_crc_bytes", algorithm == ChecksumAlgorithm::Crc32c ? content.size() : 0);
                count("whole_file_write_at_all_calls", frames);
                count("whole_file_write_at_all_bytes", content.size());
                EXPECT_EQ(received.fileIoStats.writeBytes(), content.size());
                EXPECT_EQ(received.fileIoStats.posixWriteSyscallCount(), frames);
            }
        }
    }
    }
}

TEST(FramedDataSocketTest, BatchesThreeSpansIntoOneSyscallWithIdenticalBytes) {
    const auto header = testBytes(64);
    const auto prefix = testBytes(4);
    const auto payload = testBytes(257);
    SocketPair reference, candidate;
    SendScript oldCalls, newCalls;
    {
        ObserveSends observe(oldCalls);
        ASSERT_TRUE(reference.sender.writeAll(header.data(), header.size()).isOk());
        ASSERT_TRUE(reference.sender.writeAll(prefix.data(), prefix.size()).isOk());
        ASSERT_TRUE(reference.sender.writeAll(payload.data(), payload.size()).isOk());
    }
    {
        ObserveSends observe(newCalls);
        ASSERT_TRUE(candidate.sender.writeFrame(header.data(), header.size(),
                                                prefix.data(), prefix.size(),
                                                payload.data(), payload.size()).isOk());
    }
    auto expected = header;
    expected.reserve(header.size() + prefix.size() + payload.size());
    expected.insert(expected.end(), prefix.begin(), prefix.end());
    expected.insert(expected.end(), payload.begin(), payload.end());
    EXPECT_EQ(readBytes(reference.receiver.get(), expected.size()), expected);
    EXPECT_EQ(readBytes(candidate.receiver.get(), expected.size()), expected);
    EXPECT_EQ(oldCalls.sendCalls, 3U);
    EXPECT_EQ(oldCalls.sendmsgCalls, 0U);
    EXPECT_EQ(newCalls.sendCalls, 0U);
    EXPECT_EQ(newCalls.sendmsgCalls, 1U);
    EXPECT_TRUE(newCalls.noSignal);
    std::cout << "frame_syscalls reference_send=" << oldCalls.sendCalls
              << " candidate_sendmsg=" << newCalls.sendmsgCalls << '\n';
}

TEST(FramedDataSocketTest, ThreeSpansHandleShortWritesAndEintrAcrossBoundaries) {
    const auto header = testBytes(64);
    const auto prefix = testBytes(4);
    const auto payload = testBytes(257);
    auto expected = header;
    expected.insert(expected.end(), prefix.begin(), prefix.end());
    expected.reserve(header.size() + prefix.size() + payload.size());
    expected.insert(expected.end(), payload.begin(), payload.end());

    for (const auto firstWrite : {1, 63, 64, 65, 67, 68, 69, 324}) {
        SCOPED_TRACE(firstWrite);
        SocketPair pair;
        SendScript script{{firstWrite, -EINTR}};
        ObserveSends observe(script);
        const auto status = pair.sender.writeFrame(
            header.data(), header.size(), prefix.data(), prefix.size(),
            payload.data(), payload.size());
        ASSERT_TRUE(status.isOk()) << status.message();
        EXPECT_EQ(readBytes(pair.receiver.get(), expected.size()), expected);
        EXPECT_EQ(script.sendmsgCalls, 3U);
        EXPECT_TRUE(script.noSignal);
    }
}

TEST(FramedDataSocketTest, PartialWritesAndEintrPreserveEveryByte) {
    const auto bytes = testBytes(321);
    for (const auto firstWrite : {1, 63, 64, 65, 320}) {
        SCOPED_TRACE(firstWrite);
        SocketPair pair;
        SendScript script{{firstWrite, -EINTR}};
        ObserveSends observe(script);
        const auto status = pair.sender.writeFrame(bytes.data(), 64, bytes.data() + 64, 257);
        ASSERT_TRUE(status.isOk()) << status.message();
        EXPECT_EQ(readBytes(pair.receiver.get(), bytes.size()), bytes);
        EXPECT_EQ(script.sendmsgCalls, 3U);
        EXPECT_TRUE(script.noSignal);
    }
}

TEST(FramedDataSocketTest, HandlesEmptyHeaderOrPayload) {
    const auto bytes = testBytes(7);
    SocketPair pair;
    SendScript script;
    ObserveSends observe(script);
    ASSERT_TRUE(pair.sender.writeFrame(nullptr, 0, bytes.data(), bytes.size()).isOk());
    EXPECT_EQ(readBytes(pair.receiver.get(), bytes.size()), bytes);
    ASSERT_TRUE(pair.sender.writeFrame(bytes.data(), bytes.size(), nullptr, 0).isOk());
    EXPECT_EQ(readBytes(pair.receiver.get(), bytes.size()), bytes);
    EXPECT_EQ(script.sendmsgCalls, 2U);
    ASSERT_TRUE(pair.sender.writeFrame(nullptr, 0, nullptr, 0).isOk());
    EXPECT_EQ(script.sendmsgCalls, 2U);
}

TEST(FramedDataSocketTest, RejectsInvalidSpansAndOverflowBeforeSending) {
    SocketPair pair;
    const std::uint8_t byte = 1;
    SendScript script;
    ObserveSends observe(script);
    for (const auto& status : {
            pair.sender.writeFrame(nullptr, 1, &byte, 1),
            pair.sender.writeFrame(&byte, 1, nullptr, 1),
            pair.sender.writeFrame(&byte, static_cast<std::size_t>(
                std::numeric_limits<ssize_t>::max()), &byte, 1),
            pair.sender.writeFrame(&byte, std::numeric_limits<std::size_t>::max(), &byte, 1)}) {
        EXPECT_EQ(status.code(), StatusCode::InvalidArgument);
    }
    EXPECT_EQ(script.sendmsgCalls, 0U);
    std::uint8_t received;
    EXPECT_EQ(::recv(pair.receiver.get(), &received, 1, MSG_DONTWAIT), -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

TEST(FramedDataSocketTest, ZeroProgressIsTransportFailure) {
    SocketPair pair;
    const std::uint8_t byte = 1;
    SendScript script{{0}};
    ObserveSends observe(script);
    const auto status = pair.sender.writeFrame(&byte, 1, &byte, 1);
    EXPECT_EQ(status.code(), StatusCode::TransportError);
    EXPECT_NE(status.message().find("zero"), std::string::npos);
    EXPECT_EQ(script.sendmsgCalls, 1U);
}

TEST(FramedDataSocketTest, EagainReturnsWithoutBusyRetry) {
    SocketPair pair;
    const std::uint8_t byte = 1;
    SendScript script{{-EAGAIN}};
    ObserveSends observe(script);
    const auto status = pair.sender.writeFrame(&byte, 1, &byte, 1);
    EXPECT_EQ(status.code(), StatusCode::TransportError);
    EXPECT_EQ(status.errorNumber(), EAGAIN);
    EXPECT_EQ(script.sendmsgCalls, 1U);
}

TEST(FramedDataSocketTest, ErrorAfterPartialHeaderRetainsWrittenPrefixOnly) {
    SocketPair pair;
    const auto bytes = testBytes(100);
    SendScript script{{17, -EPIPE}};
    ObserveSends observe(script);
    const auto status = pair.sender.writeFrame(bytes.data(), 64, bytes.data() + 64, 36);
    EXPECT_EQ(status.code(), StatusCode::TransportError);
    EXPECT_EQ(status.errorNumber(), EPIPE);
    EXPECT_EQ(script.sendmsgCalls, 2U);
    EXPECT_EQ(readBytes(pair.receiver.get(), 17),
              std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 17));
}

TEST(FramedDataSocketTest, InvalidSocketReturnsTransportFailure) {
    FramedDataSocket socket;
    const std::uint8_t byte = 1;
    const auto status = socket.writeFrame(&byte, 1, &byte, 1);
    EXPECT_EQ(status.code(), StatusCode::TransportError);
    EXPECT_EQ(status.errorNumber(), EBADF);
}

TEST(FramedDataSocketTest, PeerClosureDoesNotRaiseSigpipe) {
    const auto child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        ::signal(SIGPIPE, SIG_DFL);
        SocketPair pair;
        pair.receiver.reset();
        const std::uint8_t byte = 1;
        const auto status = pair.sender.writeFrame(&byte, 1, &byte, 1);
        ::_exit(status.code() == StatusCode::TransportError && status.errorNumber() == EPIPE ? 0 : 1);
    }
    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(FramedDataSocketTest, SmallSocketBufferHandlesRealBackpressure) {
    SocketPair pair;
    const int bufferBytes = 1024;
    ASSERT_EQ(::setsockopt(pair.sender.fd(), SOL_SOCKET, SO_SNDBUF,
                          &bufferBytes, sizeof(bufferBytes)), 0);
    const timeval sendTimeout{0, 20000};
    ASSERT_EQ(::setsockopt(pair.sender.fd(), SOL_SOCKET, SO_SNDTIMEO,
                          &sendTimeout, sizeof(sendTimeout)), 0);
    const auto bytes = testBytes(512U * 1024U + 64U);
    Status sent;
    SendScript script;
    std::promise<void> firstReturned;
    auto firstCall = firstReturned.get_future();
    script.firstSendReturned = &firstReturned;
    std::jthread writer([&] {
        ObserveSends observe(script);
        sent = pair.sender.writeFrame(bytes.data(), 64, bytes.data() + 64, bytes.size() - 64);
        ::shutdown(pair.sender.fd(), SHUT_WR);
    });
    // Start reading only after the full kernel buffer produced a real short write.
    const auto firstReady = firstCall.wait_for(std::chrono::seconds(2));
    auto received = readBytes(pair.receiver.get(), bytes.size());
    if (received.size() != bytes.size()) ::shutdown(pair.sender.fd(), SHUT_RDWR);
    writer.join();
    EXPECT_EQ(firstReady, std::future_status::ready);
    EXPECT_TRUE(script.timeoutCleared);
    EXPECT_TRUE(sent.isOk()) << sent.message();
    EXPECT_EQ(received, bytes);
    EXPECT_GT(script.sendmsgCalls, 1U);
    EXPECT_TRUE(script.noSignal);
}
