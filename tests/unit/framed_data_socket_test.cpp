#include "cpnetflux/core/io/framed_data_socket.h"
#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/core/protocol/compressed_data.h"

#include <gtest/gtest.h>

#include <sys/resource.h>
#include <sys/timerfd.h>
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
std::atomic<std::size_t> receiverFrameAllocations{0};
thread_local bool observeReceiverFrames = false;

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
struct PipelineDepthSetting {
    std::optional<std::string> previous;
    explicit PipelineDepthSetting(unsigned depth) {
        const auto* value = std::getenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH");
        if (value) previous = value;
        ::setenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH", std::to_string(depth).c_str(), 1);
    }
    ~PipelineDepthSetting() {
        if (previous) ::setenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH", previous->c_str(), 1);
        else ::unsetenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH");
    }
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
extern "C" ssize_t __real_readv(int fd, const iovec* spans, int count);
thread_local std::vector<ssize_t>* readSteps = nullptr;
extern "C" ssize_t __wrap_readv(int fd, const iovec* spans, int count) {
    if (!readSteps || readSteps->empty()) return __real_readv(fd, spans, count);
    const auto step = readSteps->front();
    readSteps->erase(readSteps->begin());
    if (step < 0) { errno = static_cast<int>(-step); return -1; }
    std::vector<iovec> bounded;
    auto remaining = static_cast<std::size_t>(step);
    for (int i = 0; i < count && remaining; ++i) {
        const auto size = std::min(remaining, spans[i].iov_len);
        bounded.push_back({spans[i].iov_base, size});
        remaining -= size;
    }
    return __real_readv(fd, bounded.data(), static_cast<int>(bounded.size()));
}

#if CPNETFLUX_TEST_WRAP_NEW
extern "C" void* __real__Znwm(std::size_t size);
extern "C" void* __wrap__Znwm(std::size_t size) {
    if (size == observedAllocationSize.load(std::memory_order_relaxed))
        observedAllocations.fetch_add(1, std::memory_order_relaxed);
    if (observeReceiverFrames && size == 65536)
        receiverFrameAllocations.fetch_add(1, std::memory_order_relaxed);
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

TEST(FramedDataSocketTest, ScatterReceiveHandlesEintrAndEverySpanBoundary) {
    SocketPair pair;
    const std::array<std::uint8_t, 11> input{1,2,3,4,5,6,7,8,9,10,11};
    ASSERT_EQ(::send(pair.receiver.get(), input.data(), input.size(), MSG_NOSIGNAL), 11);
    std::array<std::uint8_t, 4> prefix{};
    std::array<std::uint8_t, 7> payload{};
    std::vector<ssize_t> script{-EINTR, 1, 2, -EINTR, 1, 3, 4};
    readSteps = &script;
    const auto status = pair.sender.readAllV(prefix.data(), prefix.size(), payload.data(), payload.size());
    readSteps = nullptr;
    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_TRUE(script.empty());
    EXPECT_TRUE(std::equal(prefix.begin(), prefix.end(), input.begin()));
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), input.begin()+4));
}

TEST(FramedDataSocketTest, ScatterReceiveRejectsInvalidSpansAndTruncation) {
    SocketPair pair;
    std::array<std::uint8_t, 4> bytes{};
    EXPECT_FALSE(pair.sender.readAllV(nullptr, 1, bytes.data(), 1).isOk());
    EXPECT_FALSE(pair.sender.readAllV(bytes.data(), std::numeric_limits<std::size_t>::max(), nullptr, 0).isOk());
    EXPECT_TRUE(pair.sender.readAllV(nullptr, 0, nullptr, 0).isOk());
    const std::uint8_t value = 19;
    ASSERT_EQ(::send(pair.receiver.get(), &value, 1, MSG_NOSIGNAL), 1);
    pair.receiver.reset();
    EXPECT_FALSE(pair.sender.readAllV(bytes.data(), 2, bytes.data()+2, 2).isOk());
    EXPECT_EQ(bytes[0], value);
    EXPECT_FALSE(FramedDataSocket().readAllV(bytes.data(), 1, nullptr, 0).isOk());
}

TEST(FramedDataSocketTest, ScatterReceiveEmptyPrefixAndPayloadDoNotConsumeExtraBytes) {
    SocketPair pair;
    const std::array<std::uint8_t, 4> input{3, 5, 7, 9};
    ASSERT_EQ(::send(pair.receiver.get(), input.data(), input.size(), MSG_NOSIGNAL), 4);
    std::array<std::uint8_t, 4> actual{};
    ASSERT_TRUE(pair.sender.readAllV(nullptr, 0, actual.data(), 2).isOk());
    ASSERT_TRUE(pair.sender.readAllV(actual.data()+2, 2, nullptr, 0).isOk());
    EXPECT_EQ(actual, input);
}

TEST(FramedDataSocketTest, ScatterReceiveRejectsOtherTransportsAndReturnsEagain) {
    std::array<std::uint8_t, 2> bytes{};
    FramedDataSocket tls(cpnetflux::core::io::TlsConnection{});
    FramedDataSocket rdma(cpnetflux::core::io::RdmaDataSocket{});
    EXPECT_FALSE(tls.readAllV(bytes.data(), 1, bytes.data()+1, 1).isOk());
    EXPECT_FALSE(rdma.readAllV(bytes.data(), 1, bytes.data()+1, 1).isOk());
    SocketPair pair;
    std::vector<ssize_t> script{-EAGAIN, 1};
    readSteps = &script;
    const auto status = pair.sender.readAllV(bytes.data(), 1, bytes.data()+1, 1);
    readSteps = nullptr;
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(script.size(), 1U);
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
    for (const unsigned depth : {2U, 8U}) {
    SCOPED_TRACE(depth);
    PipelineDepthSetting depthSetting(depth);
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
    received.phaseTiming = true;
    Status receiveStatus;
    receiverFrameAllocations.store(0, std::memory_order_relaxed);
    std::jthread reader([&] {
        observeReceiverFrames = true;
        receiveStatus = receivePersistentTree(&receiver, destination.path.string(), false, "", &received);
        observeReceiverFrames = false;
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
    EXPECT_LE(allocations, depth + 2U) << "queue depth plus producer and consumer buffers";
    const auto receiverAllocations = receiverFrameAllocations.load(std::memory_order_relaxed);
    EXPECT_GT(receiverAllocations, 0U) << "observe real receiver allocations independently of profiling";
    const auto verifiedChunks = (content.size() + id.chunkSize - 1) / id.chunkSize;
    ASSERT_EQ(received.fileTimings.size(), 1U);
    EXPECT_LE(receiverAllocations, depth + 1U + received.fileTimings.front().receiverWriteBatchLimit + verifiedChunks)
        << "bounded queue plus producer/batch owners and one readback buffer per verified chunk";
    ASSERT_EQ(received.fileTimings.size(), 1U);
    const auto& receiverTiming = received.fileTimings.front();
    const auto* execution = std::getenv("CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION");
    const bool inlineReceiver = execution && std::string(execution) == "inline";
    EXPECT_EQ(receiverTiming.receiverInlinePayload, inlineReceiver);
    EXPECT_EQ(receiverTiming.payloadPipelineDepth, inlineReceiver ? 0U : depth);
    EXPECT_EQ(receiverTiming.pipelineBudgetBytes, inlineReceiver ? 0U : depth * 65536U);
    if (inlineReceiver) {
        EXPECT_EQ(receiverTiming.pipelineHighWatermarkBytes, 0U);
        EXPECT_EQ(receiverTiming.receiverWriterThreadCpuSeconds, 0);
        EXPECT_LE(receiverAllocations, 1U + verifiedChunks);
    } else {
        EXPECT_GT(receiverTiming.pipelineHighWatermarkBytes, 0U);
    }
    EXPECT_LE(receiverTiming.pipelineHighWatermarkBytes, receiverTiming.pipelineBudgetBytes);
    std::ifstream output(destination.path / "buffers.bin", std::ios::binary);
    const std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(output)), {});
    EXPECT_EQ(actual, content);
    }
#endif
}

TEST(FramedDataSocketTest, PersistentSenderSeparatesCrcFromPayloadWithIdenticalWireContent) {
    constexpr unsigned pipelineDepth = 2;
    PipelineDepthSetting depthSetting(pipelineDepth);
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
                EXPECT_NE(timing.str().find("\"schema\":\"cpnetflux_receiver_payload_observation_v2\""), std::string::npos);
                std::uint64_t wireBytes = 0, decodedFrames = 0;
                for (const auto& write : script.dataWrites) {
                    wireBytes += write.body.size();
                    if (write.header.flags & kDataCompressed) ++decodedFrames;
                }
                const auto count = [&](const char* key, std::uint64_t expected) {
                    EXPECT_NE(timing.str().find(std::string("\"") + key + "\":" +
                        std::to_string(expected) + ","), std::string::npos) << key;
                };
                const auto frames = script.dataWrites.size();
                count("data_frame_count", frames);
                ASSERT_EQ(received.fileTimings.size(), 1U);
                const auto& observed = received.fileTimings.front().receiverPayloadObservation;
                EXPECT_GT(observed.framePayloadAllocations, 0U);
                EXPECT_LE(observed.framePayloadAllocations,
                    std::min<std::uint64_t>(frames, pipelineDepth + 3U));
                EXPECT_EQ(observed.framePayloadAllocations + observed.framePayloadReuses, frames);
                EXPECT_LE(observed.framePayloadAllocatedCapacityBytes, (pipelineDepth + 3U) * 65536U);
                EXPECT_LE(observed.framePayloadPeakCapacityBytes, 65536U);
                count("frame_payload_allocations", observed.framePayloadAllocations);
                count("frame_payload_reuses", frames - observed.framePayloadAllocations);
                count("frame_payload_bytes", wireBytes);
                count("logical_buffer_allocations", 0);
                count("logical_copy_bytes", 0);
                count("prefix_shift_calls", 0);
                count("prefix_shift_bytes", 0);
                count("decompression_calls", decodedFrames);
                count("frame_crc_bytes", algorithm == ChecksumAlgorithm::Crc32c ? content.size() : 0);
                const auto gathered = received.fileTimings.front().gatheredWriteCalls;
                EXPECT_LE(gathered * 2, frames);
                count("whole_file_write_at_all_calls", frames - gathered);
                count("whole_file_write_at_all_bytes", content.size());
                EXPECT_EQ(received.fileIoStats.writeBytes(), content.size());
                EXPECT_EQ(received.fileIoStats.posixWriteSyscallCount(), frames - gathered);
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

namespace {
struct SenderPacingSetting {
    std::optional<std::string> previous;
    explicit SenderPacingSetting(const char* value) {
        if (const auto* old = std::getenv("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND")) previous = old;
        ::setenv("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND", value, 1);
    }
    ~SenderPacingSetting() {
        if (previous) ::setenv("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND", previous->c_str(), 1);
        else ::unsetenv("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND");
    }
};
}
TEST(FramedDataSocketTest, SenderPacingRejectsInvalidSettingBeforeAnyProtocolWrite) {
    SenderPacingSetting setting("1499999999");
    SocketPair pair;
    cpnetflux::core::io::PersistentTreeStats stats;
    ::shutdown(pair.receiver.get(), SHUT_RDWR);
    const auto result = cpnetflux::core::io::sendPersistentTree(&pair.sender, "/tmp", {}, &stats);
    EXPECT_EQ(result.code(), StatusCode::InvalidArgument) << result.message();
}

#include "cpnetflux/core/io/detail/sender_pacing.h"

TEST(SenderPacerTest, AdmissionSpacingAndNoCatchupAfterPause) {
    using P = cpnetflux::core::io::detail::SenderPacer;
    P pacer(P::kRate);
    auto current = P::Clock::time_point{};
    const auto now = [&] { return current; };
    const auto sleep = [&](P::Clock::duration delay) { current += delay; };
    const auto ok = [] { return Status::ok(); };
    ASSERT_TRUE(pacer.beforeSend(262212, ok, now, sleep).isOk());
    EXPECT_EQ(current, P::Clock::time_point{});
    ASSERT_TRUE(pacer.beforeSend(262212, ok, now, sleep).isOk());
    EXPECT_GE(current.time_since_epoch(), std::chrono::nanoseconds(174808));
    current += std::chrono::seconds(1);
    ASSERT_TRUE(pacer.beforeSend(262212, ok, now, sleep).isOk());
    const auto afterPause = current;
    ASSERT_TRUE(pacer.beforeSend(262212, ok, now, sleep).isOk());
    EXPECT_GE(current-afterPause, std::chrono::nanoseconds(174808));
    EXPECT_EQ(pacer.frames, 4U);
    EXPECT_EQ(pacer.wireBytes, 4U*262212);
    EXPECT_EQ(pacer.waits, 2U);
    EXPECT_EQ(pacer.spacingViolations, 0U);
}
TEST(SenderPacerTest, CancellationDuringWaitDoesNotAdmitAnotherFrame) {
    using P = cpnetflux::core::io::detail::SenderPacer;
    P pacer(1024);
    auto current = P::Clock::time_point{};
    bool cancelled = false;
    const auto status = [&] { return cancelled ? Status::runtimeError("cancelled") : Status::ok(); };
    const auto now = [&] { return current; };
    const auto sleep = [&](P::Clock::duration delay) {
        EXPECT_LE(delay, std::chrono::milliseconds(1));
        current += delay; cancelled = true;
    };
    ASSERT_TRUE(pacer.beforeSend(1024, status, now, sleep).isOk());
    EXPECT_FALSE(pacer.beforeSend(1024, status, now, sleep).isOk());
    EXPECT_EQ(pacer.frames, 1U);
    EXPECT_EQ(pacer.wireBytes, 1024U);
}
TEST(SenderPacerTest, DefaultDoesNotReadClockWaitOrChangeCounters) {
    cpnetflux::core::io::detail::SenderPacer pacer;
    const auto status = pacer.beforeSend(262212,
        [] { ADD_FAILURE(); return Status::ok(); },
        [] { ADD_FAILURE(); return std::chrono::steady_clock::time_point{}; },
        [](auto) { ADD_FAILURE(); });
    EXPECT_TRUE(status.isOk());
    EXPECT_EQ(pacer.frames, 0U);
}
TEST(FramedDataSocketTest, SenderPacingRejectsMoreThanFourLanesBeforeProtocol) {
    SenderPacingSetting setting("1500000000");
    SocketPair pair;
    cpnetflux::core::io::PersistentTreeStats stats;
    ::shutdown(pair.receiver.get(), SHUT_RDWR);
    auto status = cpnetflux::core::io::sendPersistentTree(&pair.sender, "/tmp", {}, &stats,
        {}, 1, 0, 5);
    EXPECT_EQ(status.code(), StatusCode::InvalidArgument);
}
TEST(FramedDataSocketTest, PacedV3SenderPreservesContentAndEmitsAdmissionReceipt) {
    using namespace cpnetflux::core::io;
    SenderPacingSetting setting("1500000000");
    struct Directory {
        std::filesystem::path path;
        Directory() {
            char pattern[] = "/tmp/cpnetflux-pacing-unit-XXXXXX";
            const auto* name = ::mkdtemp(pattern);
            if (!name) throw std::runtime_error("mkdtemp");
            path = name;
        }
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    } source, destination;
    const auto content = testBytes(4*1024*1024+37);
    {
        std::ofstream output(source.path/"paced.bin", std::ios::binary);
        output.write(reinterpret_cast<const char*>(content.data()), content.size());
    }
    struct stat metadata{};
    ASSERT_EQ(::stat((source.path/"paced.bin").c_str(), &metadata), 0);
    PersistentFileIdentity id{1, 7, content.size(), "paced.bin", "pacing-content",
        1048576, metadata.st_mtim.tv_sec, cpnetflux::checksum::ChecksumAlgorithm::Crc32c};
    int sockets[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0, sockets),0);
    FramedDataSocket sender{UniqueFd(sockets[0])}, receiver{UniqueFd(sockets[1])};
    PersistentTreeStats sent, received;
    Status receiveStatus;
    std::jthread reader([&] { receiveStatus=receivePersistentTree(&receiver,destination.path.string(),false,"",&received); });
    testing::internal::CaptureStderr();
    const auto status=sendPersistentTree(&sender,source.path.string(),{id},&sent);
    const auto receipt=testing::internal::GetCapturedStderr();
    if (!status.isOk()) receiver.shutdown();
    reader.join();
    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_TRUE(receiveStatus.isOk()) << receiveStatus.message();
    EXPECT_EQ(sent.payloadBytes, content.size());
    EXPECT_NE(receipt.find("CPNETFLUX_SENDER_PACING {"),std::string::npos);
    EXPECT_NE(receipt.find("\"spacing_violations\":0"),std::string::npos);
    std::ifstream file(destination.path/"paced.bin",std::ios::binary);
    const std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(file)),{});
    EXPECT_EQ(actual,content);
}

namespace {
std::size_t pacingTimerCount() {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        std::error_code error;
        if (std::filesystem::read_symlink(entry.path(), error) == "anon_inode:[timerfd]") ++count;
    }
    return count;
}
}
TEST(SenderPacerTest, WaiterAllocatesOneLazyTimerAndReleasesIt) {
    using P = cpnetflux::core::io::detail::SenderPacer;
    const auto baseline = pacingTimerCount();
    const auto ok = [] { return Status::ok(); };
    {
        P pacer(10240);
        ASSERT_TRUE(pacer.beforeSend(1024, ok).isOk());
        EXPECT_EQ(pacingTimerCount(), baseline);
        ASSERT_TRUE(pacer.beforeSend(1024, ok).isOk());
        EXPECT_EQ(pacingTimerCount(), baseline + 1);
        ASSERT_TRUE(pacer.beforeSend(1024, ok).isOk());
        EXPECT_EQ(pacingTimerCount(), baseline + 1);
    }
    EXPECT_EQ(pacingTimerCount(), baseline);
}
TEST(SenderPacerTest, WaiterFdExhaustionDoesNotAdmitAnotherFrame) {
    using P = cpnetflux::core::io::detail::SenderPacer;
    P pacer(10240);
    const auto ok = [] { return Status::ok(); };
    ASSERT_TRUE(pacer.beforeSend(1024, ok).isOk());
    rlimit original{};
    ASSERT_EQ(::getrlimit(RLIMIT_NOFILE, &original), 0);
    auto limited = original;
    limited.rlim_cur = 0;
    ASSERT_EQ(::setrlimit(RLIMIT_NOFILE, &limited), 0);
    const auto status = pacer.beforeSend(1024, ok);
    const auto restored = ::setrlimit(RLIMIT_NOFILE, &original);
    ASSERT_EQ(restored, 0);
    EXPECT_EQ(status.code(), StatusCode::SystemError);
    EXPECT_EQ(status.errorNumber(), EMFILE);
    EXPECT_EQ(pacer.frames, 1U);
    EXPECT_EQ(pacer.wireBytes, 1024U);
}
TEST(SenderPacerTest, RealWaitCancellationPreservesCauseAndReleasesTimer) {
    using P = cpnetflux::core::io::detail::SenderPacer;
    const auto baseline = pacingTimerCount();
    {
        P pacer(10240);
        ASSERT_TRUE(pacer.beforeSend(1024, [] { return Status::ok(); }).isOk());
        unsigned checks = 0;
        const auto status = pacer.beforeSend(1024, [&] {
            return ++checks > 1 ? Status::runtimeError("cancel during real wait") : Status::ok();
        });
        EXPECT_EQ(status.code(), StatusCode::RuntimeError);
        EXPECT_EQ(status.message(), "cancel during real wait");
        EXPECT_EQ(pacer.frames, 1U);
        EXPECT_EQ(pacingTimerCount(), baseline + 1);
    }
    EXPECT_EQ(pacingTimerCount(), baseline);
}

TEST(SenderPacerTest, WaitFailureDoesNotAdmitAnotherFrame) {
    using P = cpnetflux::core::io::detail::SenderPacer;
    P pacer(10240);
    auto current = P::Clock::time_point{};
    const auto ok = [] { return Status::ok(); };
    const auto now = [&] { return current; };
    ASSERT_TRUE(pacer.beforeSend(1024, ok, now, [](auto) {}).isOk());
    const auto status = pacer.beforeSend(1024, ok, now, [&](auto delay) {
        current += delay;
        return Status::systemError("wait failed", EIO);
    });
    EXPECT_EQ(status.code(), StatusCode::SystemError);
    EXPECT_EQ(status.errorNumber(), EIO);
    EXPECT_EQ(pacer.frames, 1U);
}

namespace {
enum class PacingWaitFault { Arm, Read, Interrupt };
struct PacingFault {
    PacingWaitFault kind;
    unsigned calls = 0;
    bool interrupted = false;
};
thread_local PacingFault* pacingFault = nullptr;
}
extern "C" int __real_timerfd_settime(int, int, const itimerspec*, itimerspec*);
extern "C" ssize_t __real_read(int, void*, size_t);
extern "C" int __wrap_timerfd_settime(int fd, int flags, const itimerspec* value, itimerspec* old) {
    if (pacingFault && pacingFault->kind == PacingWaitFault::Arm) {
        ++pacingFault->calls; errno = EIO; return -1;
    }
    return __real_timerfd_settime(fd, flags, value, old);
}
extern "C" ssize_t __wrap_read(int fd, void* bytes, size_t length) {
    if (pacingFault && pacingFault->kind != PacingWaitFault::Arm) {
        ++pacingFault->calls;
        if (pacingFault->kind == PacingWaitFault::Interrupt) {
            pacingFault->interrupted = true; errno = EINTR;
        } else errno = EIO;
        return -1;
    }
    return __real_read(fd, bytes, length);
}
TEST(SenderPacerTest, TimerArmAndReadErrorsDoNotAdmitOrLeak) {
    using P = cpnetflux::core::io::detail::SenderPacer;
    for (const auto kind : {PacingWaitFault::Arm, PacingWaitFault::Read}) {
        const auto baseline = pacingTimerCount();
        {
            P pacer(10240);
            const auto ok = [] { return Status::ok(); };
            ASSERT_TRUE(pacer.beforeSend(1024, ok).isOk());
            PacingFault fault{kind};
            pacingFault = &fault;
            const auto status = pacer.beforeSend(1024, ok);
            pacingFault = nullptr;
            EXPECT_EQ(status.code(), StatusCode::SystemError);
            EXPECT_EQ(status.errorNumber(), EIO);
            EXPECT_EQ(fault.calls, 1U);
            EXPECT_EQ(pacer.frames, 1U);
            EXPECT_EQ(pacer.wireBytes, 1024U);
        }
        EXPECT_EQ(pacingTimerCount(), baseline);
    }
}
TEST(SenderPacerTest, TimerReadInterruptionChecksCancellationWithoutRetrySpin) {
    using P = cpnetflux::core::io::detail::SenderPacer;
    const auto baseline = pacingTimerCount();
    {
        P pacer(10240);
        ASSERT_TRUE(pacer.beforeSend(1024, [] { return Status::ok(); }).isOk());
        PacingFault fault{PacingWaitFault::Interrupt};
        pacingFault = &fault;
        const auto status = pacer.beforeSend(1024, [&] {
            return fault.interrupted ? Status::runtimeError("interrupted and cancelled") : Status::ok();
        });
        pacingFault = nullptr;
        EXPECT_EQ(status.code(), StatusCode::RuntimeError);
        EXPECT_EQ(status.message(), "interrupted and cancelled");
        EXPECT_EQ(fault.calls, 1U);
        EXPECT_EQ(pacer.frames, 1U);
    }
    EXPECT_EQ(pacingTimerCount(), baseline);
}

namespace {
struct ExactReadSetting {
    std::optional<std::string> old;
    explicit ExactReadSetting(const char* value) {
        if (const auto* p=std::getenv("CPNETFLUX_TCP_EXACT_READ")) old=p;
        if(value) ::setenv("CPNETFLUX_TCP_EXACT_READ",value,1);
        else ::unsetenv("CPNETFLUX_TCP_EXACT_READ");
    }
    ~ExactReadSetting() {
        if(old) ::setenv("CPNETFLUX_TCP_EXACT_READ",old->c_str(),1);
        else ::unsetenv("CPNETFLUX_TCP_EXACT_READ");
    }
};
struct RecvScript {
    int fd=-1;
    std::vector<ssize_t> steps;
    std::size_t next=0;
    std::vector<int> flags;
    std::vector<std::size_t> lengths;
    std::promise<void>* entered=nullptr;
};
thread_local RecvScript* currentRecv=nullptr;
struct ObserveRecv {
    RecvScript* previous;
    explicit ObserveRecv(RecvScript& s):previous(currentRecv){currentRecv=&s;}
    ~ObserveRecv(){currentRecv=previous;}
};
}
extern "C" ssize_t __real_recv(int,void*,std::size_t,int);
extern "C" ssize_t __wrap_recv(int fd,void* bytes,std::size_t length,int flags) {
    if(!currentRecv || fd!=currentRecv->fd) return __real_recv(fd,bytes,length,flags);
    auto& s=*currentRecv;
    s.flags.push_back(flags);s.lengths.push_back(length);
    if(s.entered){s.entered->set_value();s.entered=nullptr;}
    if(s.next<s.steps.size()){
        auto step=s.steps[s.next++];
        if(step<0){errno=static_cast<int>(-step);return -1;}
        if(step==0)return 0;
        length=std::min(length,static_cast<std::size_t>(step));
    }
    return __real_recv(fd,bytes,length,flags);
}

TEST(FramedDataSocketTest, ExactReadV3ReceiverUsesWaitallOnlyForValidatedLargePayload) {
    using namespace cpnetflux::core::io;
    ExactReadSetting setting("on");
    struct Directory {
        std::filesystem::path path;
        Directory(){char pattern[]="/tmp/cpnetflux-exact-read-unit-XXXXXX";auto* p=::mkdtemp(pattern);
            if(!p)throw std::runtime_error("mkdtemp");path=p;}
        ~Directory(){std::error_code ec;std::filesystem::remove_all(path,ec);}
    } source,destination;
    const auto content=testBytes(1024*1024+37);
    {std::ofstream f(source.path/"exact.bin",std::ios::binary);
     f.write(reinterpret_cast<const char*>(content.data()),content.size());}
    struct stat meta{};ASSERT_EQ(::stat((source.path/"exact.bin").c_str(),&meta),0);
    PersistentFileIdentity id{1,7,content.size(),"exact.bin","exact-content",1048576,
        meta.st_mtim.tv_sec,cpnetflux::checksum::ChecksumAlgorithm::Crc32c};
    int fds[2];ASSERT_EQ(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fds),0);
    FramedDataSocket sender{UniqueFd(fds[0])},receiver{UniqueFd(fds[1])};
    ASSERT_TRUE(sender.configureDataFrameBytes(262144).isOk());
    ASSERT_TRUE(receiver.configureDataFrameBytes(262144).isOk());
    PersistentTreeStats sent,received;Status result;
    RecvScript script;script.fd=receiver.fd();
    std::jthread reader([&]{ObserveRecv observed(script);
        result=receivePersistentTree(&receiver,destination.path.string(),false,"",&received);});
    const auto status=sendPersistentTree(&sender,source.path.string(),{id},&sent);
    if(!status.isOk())receiver.shutdown();
    reader.join();
    ASSERT_TRUE(status.isOk())<<status.message();
    ASSERT_TRUE(result.isOk())<<result.message();
    std::ifstream f(destination.path/"exact.bin",std::ios::binary);
    EXPECT_EQ(std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),{}),content);
    std::size_t large=0;
    for(std::size_t i=0;i<script.flags.size();++i){
        if(script.lengths[i]>65536){++large;EXPECT_NE(script.flags[i]&MSG_WAITALL,0);}
        if(script.lengths[i]==64) EXPECT_EQ(script.flags[i]&MSG_WAITALL,0);
    }
    EXPECT_GE(large,4U);
}

TEST(FramedDataSocketTest, ExactReadCompletesShortReadsAndEintrWithoutExtraConsumption) {
    SocketPair pair;
    ASSERT_TRUE(pair.sender.configureDataFrameBytes(262144).isOk());
    ASSERT_TRUE(pair.sender.configureTcpExactRead(true).isOk());
    const auto input=testBytes(65540);
    ASSERT_EQ(::send(pair.receiver.get(),input.data(),input.size(),MSG_NOSIGNAL),static_cast<ssize_t>(input.size()));
    RecvScript script;script.fd=pair.sender.fd();script.steps={-EINTR,1,65535,-EINTR,4};
    ObserveRecv observed(script);
    std::vector<std::uint8_t> actual(input.size());
    ASSERT_TRUE(pair.sender.readAll(actual.data(),actual.size()).isOk());
    EXPECT_EQ(actual,input);
    ASSERT_EQ(script.flags.size(),5U);
    for(auto flags:script.flags)EXPECT_NE(flags&MSG_WAITALL,0);
    const auto stats=pair.sender.tcpExactReadStats();
    EXPECT_EQ(stats.largeReadCalls,1U);EXPECT_EQ(stats.recvCalls,5U);
    EXPECT_EQ(stats.shortReturns,2U);EXPECT_EQ(stats.interruptions,2U);
    EXPECT_EQ(stats.bytes,input.size());
    const std::uint8_t last=37;ASSERT_EQ(::send(pair.receiver.get(),&last,1,MSG_NOSIGNAL),1);
    std::uint8_t read=0;ASSERT_TRUE(pair.sender.readAll(&read,1).isOk());
    EXPECT_EQ(read,last);EXPECT_EQ(script.flags.back()&MSG_WAITALL,0);
    EXPECT_EQ(pair.sender.tcpExactReadStats().recvCalls,stats.recvCalls);
}
TEST(FramedDataSocketTest, ExactReadDefaultOffAndSmallReadSomeRetainOrdinaryFlags) {
    SocketPair pair;
    ASSERT_TRUE(pair.sender.configureDataFrameBytes(262144).isOk());
    const auto input=testBytes(65540);
    ASSERT_EQ(::send(pair.receiver.get(),input.data(),input.size(),MSG_NOSIGNAL),static_cast<ssize_t>(input.size()));
    RecvScript script;script.fd=pair.sender.fd();ObserveRecv observed(script);
    std::vector<std::uint8_t> actual(input.size());
    ASSERT_TRUE(pair.sender.readAll(actual.data(),actual.size()).isOk());
    EXPECT_EQ(actual,input);
    for(auto flags:script.flags)EXPECT_EQ(flags&MSG_WAITALL,0);
    EXPECT_FALSE(pair.sender.tcpExactReadStats().enabled);
    EXPECT_EQ(pair.sender.tcpExactReadStats().recvCalls,0U);
    ASSERT_TRUE(pair.sender.configureTcpExactRead(true).isOk());
    const std::uint8_t value=77;ASSERT_EQ(::send(pair.receiver.get(),&value,1,MSG_NOSIGNAL),1);
    std::uint8_t byte=0;ASSERT_TRUE(pair.sender.readSome(&byte,1).isOk());
    EXPECT_EQ(script.flags.back()&MSG_WAITALL,0);EXPECT_EQ(byte,value);
}
TEST(FramedDataSocketTest, ExactReadRealTimeoutAfterPartialDataPreservesErrno) {
    SocketPair pair;
    ASSERT_TRUE(pair.sender.configureDataFrameBytes(262144).isOk());
    ASSERT_TRUE(pair.sender.configureTcpExactRead(true).isOk());
    const timeval timeout{0,20000};
    ASSERT_EQ(::setsockopt(pair.sender.fd(),SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)),0);
    const std::array<std::uint8_t,7> prefix{1,2,3,4,5,6,7};
    ASSERT_EQ(::send(pair.receiver.get(),prefix.data(),prefix.size(),MSG_NOSIGNAL),7);
    std::vector<std::uint8_t> bytes(65540);
    const auto status=pair.sender.readAll(bytes.data(),bytes.size());
    EXPECT_EQ(status.code(),StatusCode::TransportError);
    EXPECT_TRUE(status.errorNumber()==EAGAIN || status.errorNumber()==EWOULDBLOCK);
    EXPECT_TRUE(std::equal(prefix.begin(),prefix.end(),bytes.begin()));
    EXPECT_EQ(pair.sender.tcpExactReadStats().bytes,7U);
    EXPECT_EQ(pair.sender.tcpExactReadStats().shortReturns,1U);
}
TEST(FramedDataSocketTest, ExactReadShutdownInterruptsBlockedReceive) {
    SocketPair pair;
    ASSERT_TRUE(pair.sender.configureDataFrameBytes(262144).isOk());
    ASSERT_TRUE(pair.sender.configureTcpExactRead(true).isOk());
    std::promise<void> entered;auto in=entered.get_future();
    std::promise<Status> done;auto result=done.get_future();
    std::jthread receiver([&]{
        RecvScript script;script.fd=pair.sender.fd();script.entered=&entered;
        ObserveRecv observed(script);
        std::vector<std::uint8_t> bytes(65540);
        done.set_value(pair.sender.readAll(bytes.data(),bytes.size()));
    });
    const auto ready=in.wait_for(std::chrono::seconds(2));
    pair.sender.shutdown();
    EXPECT_EQ(ready,std::future_status::ready);
    ASSERT_EQ(result.wait_for(std::chrono::seconds(2)),std::future_status::ready);
    EXPECT_EQ(result.get().code(),StatusCode::TransportError);
    receiver.join();
}
TEST(FramedDataSocketTest, ExactReadTruncatedFrameClearsPreviouslyValidData) {
    using namespace cpnetflux::core::io;
    using namespace cpnetflux::core::protocol;
    SocketPair pair;
    ASSERT_TRUE(pair.sender.configureDataFrameBytes(262144).isOk());
    ASSERT_TRUE(pair.sender.configureTcpExactRead(true).isOk());
    FrameHeader header;header.type=FrameType::Data;header.streamId=1;header.chunkId=7;
    header.payloadSize=262148;header.totalSize=262144;header.flags=kDataChecksum;
    const auto encoded=encodeFrameHeader(header);
    ASSERT_EQ(::send(pair.receiver.get(),encoded.data(),encoded.size(),MSG_NOSIGNAL),static_cast<ssize_t>(encoded.size()));
    const auto partial=testBytes(37);
    ASSERT_EQ(::send(pair.receiver.get(),partial.data(),partial.size(),MSG_NOSIGNAL),37);
    ASSERT_EQ(::shutdown(pair.receiver.get(),SHUT_WR),0);
    PersistentFrame frame;frame.payload={1,2,3};frame.header.streamId=99;
    const auto status=PersistentDataSession::readNext(&pair.sender,pair.sender.dataPayloadLimit(),&frame);
    EXPECT_EQ(status.code(),StatusCode::TransportError);
    EXPECT_TRUE(frame.payload.empty());EXPECT_EQ(frame.header.streamId,0U);
    EXPECT_EQ(pair.sender.tcpExactReadStats().bytes,37U);
}
TEST(FramedDataSocketTest, ExactReadRejectsUnsupportedModeAndClearsStateOnClose) {
    SocketPair pair;
    EXPECT_FALSE(pair.sender.configureTcpExactRead(true).isOk());
    EXPECT_FALSE(FramedDataSocket{}.configureTcpExactRead(true).isOk());
    FramedDataSocket rdma(cpnetflux::core::io::RdmaDataSocket{});
    EXPECT_FALSE(rdma.configureTcpExactRead(true).isOk());
    FramedDataSocket tls(cpnetflux::core::io::TlsConnection{});
    EXPECT_FALSE(tls.configureTcpExactRead(true).isOk());
    ASSERT_TRUE(pair.sender.configureDataFrameBytes(262144).isOk());
    ASSERT_TRUE(pair.sender.configureTcpExactRead(true).isOk());
    FramedDataSocket moved(std::move(pair.sender));
    EXPECT_FALSE(pair.sender.valid());EXPECT_TRUE(moved.tcpExactReadStats().enabled);
    std::vector<std::uint8_t> out(262149);
    EXPECT_FALSE(moved.readAll(out.data(),out.size()).isOk());
    EXPECT_FALSE(moved.readAll(nullptr,65540).isOk());
    EXPECT_EQ(moved.tcpExactReadStats().recvCalls,0U);
    moved.close();
    EXPECT_FALSE(moved.valid());EXPECT_FALSE(moved.tcpExactReadStats().enabled);
    EXPECT_EQ(moved.tcpExactReadStats().largeReadCalls,0U);
}
TEST(FramedDataSocketTest, ExactReadInvalidEnvironmentAndLaneCountFailBeforeReceive) {
    using namespace cpnetflux::core::io;
    for(const char* value:{"invalid","on"}) {
        ExactReadSetting setting(value);
        SocketPair pair;
        ASSERT_TRUE(pair.sender.configureDataFrameBytes(262144).isOk());
        PersistentTreeStats stats;
        const auto result=receivePersistentTree(&pair.sender,"/tmp",false,"",&stats,{},
            1,0,value==std::string_view("on")?5:1);
        EXPECT_EQ(result.code(),StatusCode::InvalidArgument);
        EXPECT_EQ(stats.files,0U);EXPECT_EQ(pair.sender.tcpExactReadStats().recvCalls,0U);
    }
}

TEST(FramedDataSocketTest, ExactReadRejectsOversizedHeaderBeforePayloadReceive) {
    using namespace cpnetflux::core::io;
    using namespace cpnetflux::core::protocol;
    SocketPair pair;
    ASSERT_TRUE(pair.sender.configureDataFrameBytes(262144).isOk());
    ASSERT_TRUE(pair.sender.configureTcpExactRead(true).isOk());
    FrameHeader header;header.type=FrameType::Data;header.streamId=1;header.chunkId=7;
    header.totalSize=1048576;header.flags=kDataChecksum;header.payloadSize=262149;
    const auto encoded=encodeFrameHeader(header);
    ASSERT_EQ(::send(pair.receiver.get(),encoded.data(),encoded.size(),MSG_NOSIGNAL),static_cast<ssize_t>(encoded.size()));
    PersistentFrame frame;
    const auto status=PersistentDataSession::readNext(&pair.sender,pair.sender.dataPayloadLimit(),&frame);
    EXPECT_EQ(status.code(),StatusCode::InvalidArgument);
    EXPECT_TRUE(frame.payload.empty());EXPECT_EQ(frame.header.streamId,0U);
    EXPECT_EQ(pair.sender.tcpExactReadStats().largeReadCalls,0U);
}
TEST(FramedDataSocketTest, ExactReadBadCrcCannotPublishDestination) {
    using namespace cpnetflux::core::io;
    using namespace cpnetflux::core::protocol;
    ExactReadSetting setting("on");
    char pattern[]="/tmp/cpnetflux-exact-crc-unit-XXXXXX";
    auto* created=::mkdtemp(pattern);ASSERT_NE(created,nullptr);
    const std::filesystem::path destination=created;
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}} cleanup{destination};
    int fds[2];ASSERT_EQ(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fds),0);
    FramedDataSocket sender{UniqueFd(fds[0])},receiver{UniqueFd(fds[1])};
    ASSERT_TRUE(sender.configureDataFrameBytes(262144).isOk());
    ASSERT_TRUE(receiver.configureDataFrameBytes(262144).isOk());
    PersistentTreeStats stats;Status receiveStatus;
    std::jthread thread([&]{receiveStatus=receivePersistentTree(&receiver,destination.string(),false,"",&stats);});
    PersistentFileIdentity id{1,7,1048576,"corrupt.bin","exact-bad-crc",1048576,0,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c};
    const auto began=PersistentDataSession::writeBegin(&sender,id);
    auto bytes=testBytes(262148);
    cpnetflux::checksum::ChecksumComputer crc(id.checksumAlgorithm);
    crc.update(bytes.data()+4,bytes.size()-4);
    const auto invalid=crc.finalize().value^1U;
    bytes[0]=invalid>>24;bytes[1]=invalid>>16;bytes[2]=invalid>>8;bytes[3]=invalid;
    FrameHeader header;header.type=FrameType::Data;header.streamId=1;header.chunkId=7;
    header.totalSize=id.totalSize;header.payloadSize=bytes.size();header.flags=kDataChecksum;
    const auto encoded=encodeFrameHeader(header);
    const auto sent=sender.writeFrame(encoded.data(),encoded.size(),bytes.data(),bytes.size());
    sender.shutdown();thread.join();
    ASSERT_TRUE(began.isOk())<<began.message();ASSERT_TRUE(sent.isOk())<<sent.message();
    EXPECT_EQ(receiveStatus.code(),StatusCode::InvalidArgument);
    EXPECT_NE(receiveStatus.message().find("checksum mismatch before write"),std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(destination/"corrupt.bin"));
    EXPECT_EQ(receiver.tcpExactReadStats().bytes,bytes.size());
}
