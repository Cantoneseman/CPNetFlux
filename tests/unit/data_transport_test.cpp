#include "cpnetflux/core/io/data_transport.h"
#include "cpnetflux/core/io/rdma_data_socket.h"
#include "cpnetflux/core/io/detail/rdma_completion_wait.h"

#include <gtest/gtest.h>

#include <chrono>
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
