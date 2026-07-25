#include "gridflux/core/io/tcp_sink_server.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <vector>

#include "gridflux/common/throughput_counter.h"
#include "gridflux/core/io/connection_context.h"
#include "gridflux/core/io/socket_utils.h"

namespace gridflux::core::io {
namespace {

struct ListenerToken {};

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

}  // namespace

common::Status runTcpSinkServer(const config::SinkOptions& options) {
    auto listenerResult =
        createListener(options.host.c_str(), options.port, static_cast<int>(options.connections));
    if (!listenerResult.isOk()) {
        return listenerResult.status();
    }
    UniqueFd listener = std::move(listenerResult.value());

    auto epollResult = createEpoll();
    if (!epollResult.isOk()) {
        return epollResult.status();
    }
    UniqueFd epollFd = std::move(epollResult.value());

    ListenerToken listenerToken;
    auto addListener = addEpollFd(epollFd.get(), listener.get(), EPOLLIN, &listenerToken);
    if (!addListener.isOk()) {
        return addListener;
    }

    std::vector<ConnectionContext> connections;
    connections.reserve(options.connections);
    std::vector<epoll_event> events(options.connections + 1);
    std::vector<char> buffer(options.bufferSize);

    common::ThroughputCounter counter;
    bool counterStarted = false;

    std::uint64_t totalReceived = 0;
    std::uint32_t closedConnections = 0;
    bool accepting = true;

    while (totalReceived < options.bytes) {
        const int eventCount =
            ::epoll_wait(epollFd.get(), events.data(), static_cast<int>(events.size()), -1);
        if (eventCount < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("epoll_wait", errno);
        }

        for (int index = 0; index < eventCount && totalReceived < options.bytes; ++index) {
            if (events[index].data.ptr == &listenerToken) {
                while (accepting) {
                    const int acceptedFd =
                        ::accept4(listener.get(), nullptr, nullptr, SOCK_NONBLOCK);
                    if (acceptedFd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        if (errno == EINTR) {
                            continue;
                        }
                        return systemStatus("accept4", errno);
                    }

                    connections.emplace_back(acceptedFd);
                    ConnectionContext& context = connections.back();
                    context.markConnected();

                    auto addConnection =
                        addEpollFd(epollFd.get(), context.fd(),
                                   EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP, &context);
                    if (!addConnection.isOk()) {
                        (void)::close(acceptedFd);
                        return addConnection;
                    }

                    if (connections.size() >= options.connections) {
                        accepting = false;
                        auto deleteListener = deleteEpollFd(epollFd.get(), listener.get());
                        if (!deleteListener.isOk()) {
                            return deleteListener;
                        }
                        listener.reset();
                        break;
                    }
                }
                continue;
            }

            auto* context = static_cast<ConnectionContext*>(events[index].data.ptr);
            if ((events[index].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U &&
                (events[index].events & EPOLLIN) == 0U) {
                context->markEof();
                (void)deleteEpollFd(epollFd.get(), context->fd());
                (void)::close(context->fd());
                closedConnections += 1;
                continue;
            }

            while (totalReceived < options.bytes) {
                const ssize_t received = ::recv(context->fd(), buffer.data(), buffer.size(), 0);
                if (received > 0) {
                    const auto bytes = static_cast<std::uint64_t>(received);
                    if (!counterStarted) {
                        counter.start(common::ThroughputCounter::Clock::now());
                        counterStarted = true;
                    }
                    context->addBytesReceived(bytes);
                    counter.addBytes(bytes);
                    totalReceived += bytes;
                    continue;
                }

                if (received == 0) {
                    context->markEof();
                    (void)deleteEpollFd(epollFd.get(), context->fd());
                    (void)::close(context->fd());
                    closedConnections += 1;
                    break;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }

                context->markError(errno);
                return systemStatus("recv", errno);
            }
        }

        if (totalReceived < options.bytes && !accepting &&
            closedConnections >= options.connections) {
            return common::Status::runtimeError("all connections closed before expected bytes");
        }
    }

    const auto end = common::ThroughputCounter::Clock::now();
    if (!counterStarted) {
        counter.start(end);
    }
    counter.stop(end);

    std::cout << "server received_bytes=" << totalReceived
              << " elapsed_seconds=" << counter.elapsedSeconds(end)
              << " throughput_gbps=" << counter.gigabitsPerSecond(end) << '\n';

    return common::Status::ok();
}

}  // namespace gridflux::core::io
