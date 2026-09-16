#include "cpnetflux/core/io/connection_context.h"

#include <gtest/gtest.h>

TEST(ConnectionContextTest, TracksStateTransitions) {
    cpnetflux::core::io::ConnectionContext context(42);

    EXPECT_EQ(context.fd(), 42);
    EXPECT_EQ(context.state(), cpnetflux::core::io::ConnectionState::Created);

    context.markConnecting();
    EXPECT_EQ(context.state(), cpnetflux::core::io::ConnectionState::Connecting);

    context.markConnected();
    EXPECT_EQ(context.state(), cpnetflux::core::io::ConnectionState::Connected);

    context.markClosing();
    EXPECT_EQ(context.state(), cpnetflux::core::io::ConnectionState::Closing);

    context.markClosed();
    EXPECT_EQ(context.state(), cpnetflux::core::io::ConnectionState::Closed);
}

TEST(ConnectionContextTest, TracksEofAndErrors) {
    cpnetflux::core::io::ConnectionContext context;

    context.markEof();
    EXPECT_TRUE(context.eof());
    EXPECT_EQ(context.state(), cpnetflux::core::io::ConnectionState::Closed);

    context.markError(104);
    EXPECT_EQ(context.errorNumber(), 104);
    EXPECT_EQ(context.state(), cpnetflux::core::io::ConnectionState::Error);
}

TEST(ConnectionContextTest, TracksByteCounters) {
    cpnetflux::core::io::ConnectionContext context;

    context.addBytesReceived(10);
    context.addBytesReceived(32);
    context.addBytesSent(7);
    context.addBytesSent(11);

    EXPECT_EQ(context.bytesReceived(), 42U);
    EXPECT_EQ(context.bytesSent(), 18U);
}
