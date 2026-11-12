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

namespace {
std::uint16_t localPort(int fd) {
 sockaddr_in a{};socklen_t n=sizeof(a);if (::getsockname(fd,reinterpret_cast<sockaddr*>(&a),&n))return 0;return ntohs(a.sin_port);
}
UniqueFd boundSocket() {
 UniqueFd fd(::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0));sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if (::bind(fd.get(),reinterpret_cast<sockaddr*>(&a),sizeof(a)))return {};return fd;
}
}
TEST(SocketBindTest, PlannedPortSkipsOccupiedCandidateWithoutEphemeralFallback) {
 auto listener=boundSocket();ASSERT_TRUE(listener.isValid());ASSERT_EQ(::listen(listener.get(),4),0);
 auto busy=boundSocket();auto spare=boundSocket();ASSERT_TRUE(busy.isValid());ASSERT_TRUE(spare.isValid());
 std::uint16_t candidates[]{localPort(busy.get()),localPort(spare.get())};spare.reset();std::uint32_t attempts=0;
 auto connection=connectBlockingSocket("127.0.0.1",localPort(listener.get()),std::chrono::steady_clock::now()+std::chrono::seconds(2),"127.0.0.1",candidates,&attempts);
 ASSERT_TRUE(connection.isOk())<<connection.status().message();EXPECT_EQ(attempts,2U);EXPECT_EQ(localPort(connection.value().get()),candidates[1]);
 sockaddr_in peer{};socklen_t n=sizeof(peer);UniqueFd accepted(::accept4(listener.get(),reinterpret_cast<sockaddr*>(&peer),&n,SOCK_CLOEXEC));
 ASSERT_TRUE(accepted.isValid());EXPECT_EQ(ntohs(peer.sin_port),candidates[1]);
 std::uint16_t exhausted[]{candidates[0]};auto failed=connectBlockingSocket("127.0.0.1",localPort(listener.get()),{},"127.0.0.1",exhausted,&attempts);
 ASSERT_FALSE(failed.isOk());EXPECT_EQ(failed.status().errorNumber(),EADDRINUSE);EXPECT_EQ(attempts,1U);
}
TEST(SocketBindTest, PlannedPortDeadlineAndNetworkFailureDoNotTryMoreCandidates) {
 auto a=boundSocket();auto b=boundSocket();auto target=boundSocket();ASSERT_TRUE(a.isValid());ASSERT_TRUE(b.isValid());ASSERT_TRUE(target.isValid());
 std::uint16_t ports[]{localPort(a.get()),localPort(b.get())};std::uint32_t attempts=999;
 auto expired=connectBlockingSocket("127.0.0.1",localPort(target.get()),std::chrono::steady_clock::now()-std::chrono::seconds(1),"127.0.0.1",ports,&attempts);
 EXPECT_FALSE(expired.isOk());EXPECT_EQ(expired.status().errorNumber(),ETIMEDOUT);EXPECT_EQ(attempts,0U);
 a.reset();b.reset(); // target is bound without listen: actual connect must fail, not scan as a retry.
 auto refused=connectBlockingSocket("127.0.0.1",localPort(target.get()),std::chrono::steady_clock::now()+std::chrono::seconds(1),"127.0.0.1",ports,&attempts);
 EXPECT_FALSE(refused.isOk());EXPECT_EQ(attempts,1U);
 auto unsupported=connectBlockingSocket("::1",9,{},"::1",ports,&attempts);EXPECT_FALSE(unsupported.isOk());EXPECT_EQ(attempts,0U);
}

TEST(SocketBindTest, PlannedPortsRespectRealClientTimeWait) {
 auto listener=boundSocket();ASSERT_TRUE(listener.isValid());ASSERT_EQ(::listen(listener.get(),4),0);
 auto a=boundSocket();auto b=boundSocket();std::uint16_t ports[]{localPort(a.get()),localPort(b.get())};a.reset();b.reset();std::uint32_t attempts=0;
 auto first=connectBlockingSocket("127.0.0.1",localPort(listener.get()),std::chrono::steady_clock::now()+std::chrono::seconds(2),"127.0.0.1",ports,&attempts);
 ASSERT_TRUE(first.isOk());UniqueFd accepted(::accept4(listener.get(),nullptr,nullptr,SOCK_CLOEXEC));ASSERT_TRUE(accepted.isValid());
 ASSERT_EQ(::shutdown(first.value().get(),SHUT_WR),0);char byte=0;ASSERT_EQ(::recv(accepted.get(),&byte,1,0),0);
 accepted.reset();ASSERT_EQ(::recv(first.value().get(),&byte,1,0),0);first.value().reset();
 auto second=connectBlockingSocket("127.0.0.1",localPort(listener.get()),std::chrono::steady_clock::now()+std::chrono::seconds(2),"127.0.0.1",ports,&attempts);
 ASSERT_TRUE(second.isOk())<<second.status().message();EXPECT_EQ(attempts,2U);EXPECT_EQ(localPort(second.value().get()),ports[1]);
}
