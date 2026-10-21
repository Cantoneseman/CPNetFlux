#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <chrono>

#include "cpnetflux/core/io/socket_utils.h"

using cpnetflux::core::io::UniqueFd;
using cpnetflux::core::io::connectBlockingSocket;

TEST(SocketBindTest, PeerObservesSelectedLocalSourceAddress) {
    UniqueFd listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    ASSERT_TRUE(listener.isValid());
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(listener.get(), reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)), 0);
    ASSERT_EQ(::listen(listener.get(), 1), 0);
    socklen_t length = sizeof(endpoint);
    ASSERT_EQ(::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&endpoint), &length), 0);
    auto connection = connectBlockingSocket("localhost", ntohs(endpoint.sin_port),
        std::chrono::steady_clock::now() + std::chrono::seconds(2), "127.0.0.2");
    ASSERT_TRUE(connection.isOk()) << connection.status().message();
    sockaddr_in peer{};
    length = sizeof(peer);
    UniqueFd accepted(::accept4(listener.get(), reinterpret_cast<sockaddr*>(&peer),
                                &length, SOCK_CLOEXEC));
    ASSERT_TRUE(accepted.isValid());
    EXPECT_EQ(ntohl(peer.sin_addr.s_addr), 0x7f000002U);
}

TEST(SocketBindTest, InvalidOrUnavailableSourceAddressDoesNotConnect) {
    for (const auto* address : {"eth0", "192.0.2.123"}) {
        auto connection = connectBlockingSocket("127.0.0.1", 9,
            std::chrono::steady_clock::now() + std::chrono::seconds(1), address);
        EXPECT_FALSE(connection.isOk());
    }
}
