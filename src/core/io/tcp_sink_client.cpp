#include "cpnetflux/core/io/tcp_sink_client.h"

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

struct ClientConnection {
    UniqueFd fd;
    ConnectionContext context;
    std::uint64_t targetBytes = 0;
};

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

void printClientResult(std::uint64_t bytes, common::ThroughputCounter::TimePoint start,
                       common::ThroughputCounter::TimePoint end) {
    common::ThroughputCounter counter;
    counter.start(start);
    counter.addBytes(bytes);
    counter.stop(end);
    std::cout << "client sent_bytes=" << bytes
              << " elapsed_seconds=" << counter.elapsedSeconds(end)
              << " throughput_gbps=" << counter.gigabitsPerSecond(end) << '\n';
}

common::Status runTcpSinkClientEpoll(const config::SinkOptions& options) {
    auto epollResult = createEpoll();
    if (!epollResult.isOk()) {
        return epollResult.status();
    }
    UniqueFd epollFd = std::move(epollResult.value());

    std::vector<ClientConnection> connections(options.connections);
    std::vector<epoll_event> events(options.connections);
    std::vector<char> buffer(options.bufferSize, 'g');

    const std::uint64_t baseBytes = options.bytes / options.connections;
    const std::uint64_t remainder = options.bytes % options.connections;

    for (std::uint32_t index = 0; index < options.connections; ++index) {
        auto connectionResult = createNonBlockingConnection(options.host.c_str(), options.port);
        if (!connectionResult.isOk()) {
            return connectionResult.status();
        }

        connections[index].fd = std::move(connectionResult.value());
        connections[index].context.setFd(connections[index].fd.get());
        connections[index].context.markConnecting();
        connections[index].targetBytes = baseBytes + (index < remainder ? 1 : 0);

        auto addConnection = addEpollFd(epollFd.get(), connections[index].fd.get(),
                                        EPOLLOUT | EPOLLERR | EPOLLHUP, &connections[index]);
        if (!addConnection.isOk()) {
            return addConnection;
        }
    }

    common::ThroughputCounter counter;
    counter.start(common::ThroughputCounter::Clock::now());

    std::uint32_t completedConnections = 0;
    while (completedConnections < options.connections) {
        const int eventCount =
            ::epoll_wait(epollFd.get(), events.data(), static_cast<int>(events.size()), -1);
        if (eventCount < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("epoll_wait", errno);
        }

        for (int index = 0; index < eventCount; ++index) {
            auto* connection = static_cast<ClientConnection*>(events[index].data.ptr);
            ConnectionContext& context = connection->context;

            if ((events[index].events & (EPOLLERR | EPOLLHUP)) != 0U) {
                auto socketError = getSocketError(context.fd());
                const int errorNumber = socketError.isOk() ? socketError.value() : errno;
                context.markError(errorNumber);
                return systemStatus("client socket", errorNumber);
            }

            if (context.state() == ConnectionState::Connecting) {
                auto socketError = getSocketError(context.fd());
                if (!socketError.isOk()) {
                    return socketError.status();
                }
                if (socketError.value() != 0) {
                    context.markError(socketError.value());
                    return systemStatus("connect", socketError.value());
                }
                context.markConnected();
            }

            while (context.bytesSent() < connection->targetBytes) {
                const std::uint64_t remaining = connection->targetBytes - context.bytesSent();
                const std::size_t chunk =
                    static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                const ssize_t sent = ::send(context.fd(), buffer.data(), chunk, MSG_NOSIGNAL);
                if (sent > 0) {
                    const auto bytes = static_cast<std::uint64_t>(sent);
                    context.addBytesSent(bytes);
                    counter.addBytes(bytes);
                    continue;
                }

                if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;
                }
                if (sent < 0 && errno == EINTR) {
                    continue;
                }

                context.markError(errno);
                return systemStatus("send", errno);
            }

            if (context.bytesSent() >= connection->targetBytes &&
                context.state() != ConnectionState::Closed) {
                context.markClosed();
                auto deleteConnection = deleteEpollFd(epollFd.get(), context.fd());
                if (!deleteConnection.isOk()) {
                    return deleteConnection;
                }
                connection->fd.reset();
                completedConnections += 1;
            }
        }
    }

    const auto end = common::ThroughputCounter::Clock::now();
    counter.stop(end);

    std::cout << "client sent_bytes=" << counter.bytes()
              << " elapsed_seconds=" << counter.elapsedSeconds(end)
              << " throughput_gbps=" << counter.gigabitsPerSecond(end) << '\n';

    return common::Status::ok();
}

common::Status runTcpSinkClientThreaded(const config::SinkOptions& options) {
    std::vector<UniqueFd> connections;
    connections.reserve(options.connections);
    for (std::uint32_t index = 0; index < options.connections; ++index) {
        auto connectionResult = connectBlockingSocket(options.host.c_str(), options.port);
        if (!connectionResult.isOk()) {
            return connectionResult.status();
        }
        connections.emplace_back(std::move(connectionResult.value()));
    }

    const std::uint64_t baseBytes = options.bytes / options.connections;
    const std::uint64_t remainder = options.bytes % options.connections;
    std::vector<ThreadTransferResult> results(options.connections);
    std::vector<std::thread> workers;
    workers.reserve(options.connections);
    for (std::uint32_t index = 0; index < options.connections; ++index) {
        const std::uint64_t targetBytes = baseBytes + (index < remainder ? 1 : 0);
        workers.emplace_back([&, index, targetBytes, fd = std::move(connections[index])] {
            auto& result = results[index];
            std::vector<char> buffer(options.bufferSize, 'g');
            while (result.bytes < targetBytes) {
                const auto remaining = targetBytes - result.bytes;
                const std::size_t chunk =
                    static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                const ssize_t sent = ::send(fd.get(), buffer.data(), chunk, MSG_NOSIGNAL);
                if (sent > 0) {
                    if (!result.started) {
                        result.start = common::ThroughputCounter::Clock::now();
                        result.started = true;
                    }
                    result.bytes += static_cast<std::uint64_t>(sent);
                    continue;
                }
                if (sent < 0 && errno == EINTR) {
                    continue;
                }
                if (sent == 0) {
                    result.status = common::Status::runtimeError("threaded send returned zero");
                } else {
                    result.status = systemStatus("threaded send", errno);
                }
                result.end = common::ThroughputCounter::Clock::now();
                return;
            }
            result.end = common::ThroughputCounter::Clock::now();
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    std::uint64_t totalSent = 0;
    bool hasPayload = false;
    auto first = common::ThroughputCounter::Clock::now();
    auto last = first;
    for (const auto& result : results) {
        if (!result.status.isOk()) {
            return result.status;
        }
        totalSent += result.bytes;
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
    if (totalSent != options.bytes) {
        return common::Status::runtimeError("threaded sink sent " + std::to_string(totalSent) +
                                            " bytes, expected " + std::to_string(options.bytes));
    }
    if (!hasPayload) {
        return common::Status::runtimeError("threaded sink sent no payload");
    }

    printClientResult(totalSent, first, last);
    return common::Status::ok();
}

}  // namespace

common::Status runTcpSinkClient(const config::SinkOptions& options) {
    if (options.ioMode == config::SinkIoMode::Threaded) {
        return runTcpSinkClientThreaded(options);
    }
    return runTcpSinkClientEpoll(options);
}

}  // namespace cpnetflux::core::io
