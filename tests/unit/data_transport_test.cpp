#include "cpnetflux/core/io/data_transport.h"
#include "cpnetflux/core/io/rdma_data_socket.h"
#include "cpnetflux/core/io/detail/rdma_completion_wait.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string>

using cpnetflux::core::io::DataTransport;

TEST(DataTransportTest, ParsesSupportedTransportsAndRejectsUnknownValues) {
    auto tcp = cpnetflux::core::io::parseDataTransport("tcp");
    auto rdma = cpnetflux::core::io::parseDataTransport("rdma");
    ASSERT_TRUE(tcp.isOk());
    ASSERT_TRUE(rdma.isOk());
    EXPECT_EQ(tcp.value(), DataTransport::Tcp);
    EXPECT_EQ(rdma.value(), DataTransport::Rdma);
    EXPECT_FALSE(cpnetflux::core::io::parseDataTransport("auto").isOk());
    EXPECT_STREQ(cpnetflux::core::io::dataTransportName(DataTransport::Tcp), "tcp");
    EXPECT_STREQ(cpnetflux::core::io::dataTransportName(DataTransport::Rdma), "rdma");
}

TEST(DataTransportTest, RdmaSelectionFailsExplicitlyWhenSupportWasNotBuilt) {
#if !CPNETFLUX_HAS_RDMA
    auto connection = cpnetflux::core::io::RdmaDataSocket::connect("127.0.0.1", 1);
    ASSERT_FALSE(connection.isOk());
    EXPECT_NE(connection.status().message().find("unavailable"), std::string::npos);
#endif
}

TEST(DataTransportTest, UnboundedRdmaCompletionWaitDoesNotExpireAfterThirtySeconds) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::time_point{};
    const cpnetflux::core::io::SocketDeadline noDeadline{};
    EXPECT_FALSE(cpnetflux::core::io::detail::rdmaCompletionDeadlineExpired(
        noDeadline, now + std::chrono::seconds(31)));
    EXPECT_EQ(cpnetflux::core::io::detail::rdmaCompletionEventPollTimeoutMs(
        noDeadline, now + std::chrono::seconds(31)), 1000);
}

TEST(DataTransportTest, ExplicitRdmaCompletionDeadlineAndPollSlicesAreBounded) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::time_point{};
    const auto deadline = now + std::chrono::milliseconds(2500);

    EXPECT_FALSE(cpnetflux::core::io::detail::rdmaCompletionDeadlineExpired(deadline, now));
    EXPECT_EQ(cpnetflux::core::io::detail::rdmaCompletionEventPollTimeoutMs(deadline, now), 1000);
    EXPECT_EQ(cpnetflux::core::io::detail::rdmaCompletionEventPollTimeoutMs(
        deadline, now + std::chrono::milliseconds(2200)), 300);
    EXPECT_TRUE(cpnetflux::core::io::detail::rdmaCompletionDeadlineExpired(deadline, deadline));
    EXPECT_EQ(cpnetflux::core::io::detail::rdmaCompletionEventPollTimeoutMs(deadline, deadline), 0);
}

TEST(DataTransportTest, NativeRdmaShutdownWakesIdleReceive) {
#if CPNETFLUX_HAS_RDMA
    using namespace cpnetflux::core::io;
    const auto* bind = std::getenv("CPNETFLUX_TEST_RDMA_BIND");
    if (!bind || !*bind) GTEST_SKIP() << "requires explicit local RDMA bind";
    auto probe = createListener(bind, 0, 1);
    ASSERT_TRUE(probe.isOk()) << probe.status().message();
    sockaddr_in address{};
    socklen_t addressSize = sizeof(address);
    ASSERT_EQ(::getsockname(probe.value().get(), reinterpret_cast<sockaddr*>(&address), &addressSize), 0);
    const auto port = ntohs(address.sin_port);
    probe.value().reset();
    auto listener = RdmaListener::listen(bind, port, 1);
    ASSERT_TRUE(listener.isOk()) << listener.status().message();
    auto accepted = std::async(std::launch::async, [&] {
        return listener.value().accept(std::chrono::steady_clock::now() + std::chrono::seconds(5));
    });
    auto client = RdmaDataSocket::connect(bind, listener.value().port(),
        std::chrono::steady_clock::now() + std::chrono::seconds(5), bind);
    ASSERT_TRUE(client.isOk()) << client.status().message();
    auto peer = accepted.get();
    ASSERT_TRUE(peer.isOk()) << peer.status().message();
    auto reader = std::async(std::launch::async, [&] {
        std::uint8_t byte = 0;
        return client.value().readSome(&byte, 1);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    client.value().shutdown();
    const auto ready = reader.wait_for(std::chrono::seconds(2));
    peer.value().shutdown();
    EXPECT_EQ(ready, std::future_status::ready);
    EXPECT_FALSE(reader.get().isOk());
#endif
}
