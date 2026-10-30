#include "cpnetflux/core/io/data_transport.h"
#include "cpnetflux/core/io/rdma_data_socket.h"

#include <gtest/gtest.h>

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
