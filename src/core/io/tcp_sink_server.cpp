#include "cpnetflux/core/io/tcp_sink_server.h"

#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "cpnetflux/common/throughput_counter.h"
#include "cpnetflux/core/io/connection_context.h"
#include "cpnetflux/core/io/socket_utils.h"

namespace cpnetflux::core::io {
namespace {

struct ListenerToken {};

struct ThreadTransferResult {
    common::Status status = common::Status::ok();
    std::uint64_t bytes = 0;
    common::ThroughputCounter::TimePoint start{};
    common::ThroughputCounter::TimePoint end{};
    bool started = false;
};

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

void printServerResult(std::uint64_t bytes, common::ThroughputCounter::TimePoint start,
                       common::ThroughputCounter::TimePoint end) {
    common::ThroughputCounter counter;
    counter.start(start);
    counter.addBytes(bytes);
    counter.stop(end);
    std::cout << "server received_bytes=" << bytes
              << " elapsed_seconds=" << counter.elapsedSeconds(end)
              << " throughput_gbps=" << counter.gigabitsPerSecond(end) << '\n';
}

common::Status runTcpSinkServerEpoll(const config::SinkOptions& options) {
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
                        ::accept4(listener.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
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

common::Status runTcpSinkServerThreaded(const config::SinkOptions& options) {
    auto listenerResult =
        createListener(options.host.c_str(), options.port, static_cast<int>(options.connections));
    if (!listenerResult.isOk()) {
        return listenerResult.status();
    }
    UniqueFd listener = std::move(listenerResult.value());

    std::vector<UniqueFd> connections;
    connections.reserve(options.connections);
    while (connections.size() < options.connections) {
        pollfd descriptor{listener.get(), POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("poll", errno);
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return common::Status::runtimeError("listener failed while accepting threaded sink");
        }

        const int acceptedFd = ::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC);
        if (acceptedFd >= 0) {
            connections.emplace_back(acceptedFd);
            continue;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        return systemStatus("accept4", errno);
    }
    listener.reset();

    std::vector<ThreadTransferResult> results(options.connections);
    std::vector<std::thread> workers;
    workers.reserve(options.connections);
    for (std::uint32_t index = 0; index < options.connections; ++index) {
        workers.emplace_back([&, index] {
            auto& result = results[index];
            std::vector<char> buffer(options.bufferSize);
            for (;;) {
                const ssize_t received =
                    ::recv(connections[index].get(), buffer.data(), buffer.size(), 0);
                if (received > 0) {
                    if (!result.started) {
                        result.start = common::ThroughputCounter::Clock::now();
                        result.started = true;
                    }
                    result.bytes += static_cast<std::uint64_t>(received);
                    continue;
                }
                if (received == 0) {
                    result.end = common::ThroughputCounter::Clock::now();
                    return;
                }
                if (errno == EINTR) {
                    continue;
                }
                result.status = systemStatus("threaded recv", errno);
                result.end = common::ThroughputCounter::Clock::now();
                return;
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    std::uint64_t totalReceived = 0;
    bool hasPayload = false;
    auto first = common::ThroughputCounter::Clock::now();
    auto last = first;
    for (const auto& result : results) {
        if (!result.status.isOk()) {
            return result.status;
        }
        totalReceived += result.bytes;
        if (result.bytes > 0) {
            if (!hasPayload || result.start < first) {
                first = result.start;
            }
            if (!hasPayload || result.end > last) {
                last = result.end;
            }
            hasPayload = true;
        }
    }
    if (totalReceived != options.bytes) {
        return common::Status::runtimeError("threaded sink received " + std::to_string(totalReceived) +
                                            " bytes, expected " + std::to_string(options.bytes));
    }
    if (!hasPayload) {
        return common::Status::runtimeError("threaded sink received no payload");
    }

    printServerResult(totalReceived, first, last);
    return common::Status::ok();
}

}  // namespace

common::Status runTcpSinkServer(const config::SinkOptions& options) {
    if (options.ioMode == config::SinkIoMode::Threaded) {
        return runTcpSinkServerThreaded(options);
    }
    return runTcpSinkServerEpoll(options);
}

}  // namespace cpnetflux::core::io
