#include <gtest/gtest.h>

#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#include "cpnetflux/core/io/tree_pipeline_control_io.h"

namespace {

using namespace std::chrono_literals;
using cpnetflux::core::io::TlsConnection;
using cpnetflux::core::io::UniqueFd;
using cpnetflux::core::io::detail::readTreePipelineControlLine;

struct SocketPair {
    TlsConnection control;
    UniqueFd peer;

    SocketPair() {
        int descriptors[2]{-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, descriptors) != 0) {
            return;
        }
        control = TlsConnection::plain(UniqueFd(descriptors[0]));
        peer.reset(descriptors[1]);
    }

    [[nodiscard]] bool valid() const { return control.valid() && peer.isValid(); }
};

TEST(TreePipelineControlIoTest, DeadlineBoundsAnIncompleteReply) {
    SocketPair sockets;
    ASSERT_TRUE(sockets.valid());
    std::string buffer;
    std::atomic<bool> cancelled{false};
    constexpr char partialReply[] = "220-partial";
    ASSERT_EQ(::send(sockets.peer.get(), partialReply, sizeof(partialReply) - 1, 0),
              static_cast<ssize_t>(sizeof(partialReply) - 1));

    const auto started = std::chrono::steady_clock::now();
    auto result = readTreePipelineControlLine(
        &sockets.control, &buffer, &cancelled, started + 80ms);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.status().message().find("timed out"), std::string::npos);
    EXPECT_GE(elapsed, 40ms);
    EXPECT_LT(elapsed, 2s);
}

TEST(TreePipelineControlIoTest, ReadsOneLineAndKeepsBufferedRemainder) {
    SocketPair sockets;
    ASSERT_TRUE(sockets.valid());
    constexpr char replies[] = "220 ready\r\n226 done\r\n";
    ASSERT_EQ(::send(sockets.peer.get(), replies, sizeof(replies) - 1, 0),
              static_cast<ssize_t>(sizeof(replies) - 1));
    std::string buffer;
    std::atomic<bool> cancelled{false};

    auto result = readTreePipelineControlLine(
        &sockets.control, &buffer, &cancelled,
        std::chrono::steady_clock::now() + 1s);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value(), "220 ready");
    EXPECT_EQ(buffer, "226 done\r\n");
}

TEST(TreePipelineControlIoTest, PeerEofReturnsAnError) {
    SocketPair sockets;
    ASSERT_TRUE(sockets.valid());
    sockets.peer.reset();
    std::string buffer;
    std::atomic<bool> cancelled{false};

    auto result = readTreePipelineControlLine(
        &sockets.control, &buffer, &cancelled,
        std::chrono::steady_clock::now() + 1s);

    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.status().message().find("closed"), std::string::npos);
}

TEST(TreePipelineControlIoTest, ShutdownWakesCancelledReader) {
    SocketPair sockets;
    ASSERT_TRUE(sockets.valid());
    const int controlFd = sockets.control.fd();
    std::string buffer;
    std::atomic<bool> cancelled{false};
    std::promise<cpnetflux::common::Result<std::string>> promise;
    auto future = promise.get_future();

    std::thread reader([&] {
        promise.set_value(readTreePipelineControlLine(
            &sockets.control, &buffer, &cancelled,
            std::chrono::steady_clock::now() + 2s));
    });
    std::this_thread::sleep_for(20ms);
    cancelled.store(true, std::memory_order_release);
    (void)::shutdown(controlFd, SHUT_RDWR);
    const auto ready = future.wait_for(750ms);
    reader.join();

    EXPECT_EQ(ready, std::future_status::ready);
    ASSERT_EQ(ready, std::future_status::ready);
    auto result = future.get();
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.status().message().find("cancelled"), std::string::npos);
}


TEST(TreePipelineControlIoTest, ParsesOnlyTransferTerminalRepliesWithIdentity) {
    using cpnetflux::core::io::detail::parseTreePipelineTerminalReply;

    auto completed = parseTreePipelineTerminalReply(
        226, "226 Transfer complete transfer_id=GFID:abc-123_xyz");
    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->code, 226);
    EXPECT_EQ(completed->transferId, "abc-123_xyz");

    auto failed = parseTreePipelineTerminalReply(
        550, "550 Transfer failed transfer_id=GFID:file-2: disk full");
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->code, 550);
    EXPECT_EQ(failed->transferId, "file-2");

    EXPECT_FALSE(parseTreePipelineTerminalReply(
                     150, "150 Opening transfer_id=GFID:file-2").has_value());
    EXPECT_FALSE(parseTreePipelineTerminalReply(
                     550, "550 Transfer failed without identity").has_value());
    EXPECT_FALSE(parseTreePipelineTerminalReply(
                     226, "226 transfer_id=GFID:").has_value());
}

TEST(TreePipelineControlIoTest, PendingPipelineCommandRejectIsNotAccepted) {
    SocketPair sockets;
    ASSERT_TRUE(sockets.valid());
    constexpr char rejected[] = "500 OPTS PIPELINE=1 unsupported\r\n";
    ASSERT_EQ(::send(sockets.peer.get(), rejected, sizeof(rejected) - 1, 0),
              static_cast<ssize_t>(sizeof(rejected) - 1));
    std::string buffer;
    std::atomic<bool> cancelled{false};

    auto line = readTreePipelineControlLine(
        &sockets.control, &buffer, &cancelled,
        std::chrono::steady_clock::now() + 1s);
    ASSERT_TRUE(line.isOk()) << line.status().message();
    ASSERT_EQ(line.value(), "500 OPTS PIPELINE=1 unsupported");
    const auto code = cpnetflux::core::io::detail::parseControlReplyCode(line.value());
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, 500);
    const auto rejectedStatus =
        cpnetflux::core::io::detail::validatePipelineEnableReply(*code);
    EXPECT_FALSE(rejectedStatus.isOk());
    EXPECT_NE(rejectedStatus.message().find("rejected"), std::string::npos);
    EXPECT_TRUE(cpnetflux::core::io::detail::validatePipelineEnableReply(200).isOk());
}

}  // namespace
