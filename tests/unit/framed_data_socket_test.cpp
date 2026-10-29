#include "cpnetflux/core/io/framed_data_socket.h"

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using cpnetflux::common::Status;
using cpnetflux::common::StatusCode;
using cpnetflux::core::io::FramedDataSocket;
using cpnetflux::core::io::UniqueFd;

struct SendScript {
    std::vector<ssize_t> steps;
    std::size_t next = 0;
    std::size_t sendCalls = 0, sendmsgCalls = 0;
    bool noSignal = true;
    std::promise<void>* firstSendReturned = nullptr;
    bool timeoutCleared = false;
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

extern "C" ssize_t __wrap_send(int fd, const void* bytes, std::size_t length, int flags) {
    if (currentScript) ++currentScript->sendCalls;
    return __real_send(fd, bytes, length, flags);
}

extern "C" ssize_t __wrap_sendmsg(int fd, const msghdr* message, int flags) {
    if (!currentScript) return __real_sendmsg(fd, message, flags);
    auto& script = *currentScript;
    ++script.sendmsgCalls;
    script.noSignal = script.noSignal && (flags & MSG_NOSIGNAL) != 0;
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
