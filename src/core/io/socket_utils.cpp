#include "cpnetflux/core/io/socket_utils.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <algorithm>
#include <climits>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace cpnetflux::core::io {
namespace {

common::Status lastSystemError(const char* operation) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errno), errno);
}

}  // namespace

UniqueFd::UniqueFd(int fd) noexcept : fd_(fd) {}

UniqueFd::~UniqueFd() { reset(); }

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

int UniqueFd::get() const noexcept { return fd_; }

bool UniqueFd::isValid() const noexcept { return fd_ >= 0; }

int UniqueFd::release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

void UniqueFd::reset(int fd) noexcept {
    if (fd_ >= 0) {
        (void)::close(fd_);
    }
    fd_ = fd;
}

common::Status setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return lastSystemError("fcntl(F_GETFL)");
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return lastSystemError("fcntl(F_SETFL)");
    }

    return common::Status::ok();
}

common::Result<UniqueFd> createListener(const char* host, std::uint16_t port, int backlog) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    const int gaiStatus = ::getaddrinfo(host, portText.c_str(), &hints, &results);
    if (gaiStatus != 0) {
        return common::Status::runtimeError(std::string("getaddrinfo: ") + gai_strerror(gaiStatus));
    }

    UniqueFd listener;
    for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
        UniqueFd candidate(::socket(item->ai_family, item->ai_socktype, item->ai_protocol));
        if (!candidate.isValid()) {
            continue;
        }

        int enabled = 1;
        (void)::setsockopt(candidate.get(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

        if (::bind(candidate.get(), item->ai_addr, item->ai_addrlen) != 0) {
            continue;
        }

        if (::listen(candidate.get(), backlog) != 0) {
            continue;
        }

        auto nonBlocking = setNonBlocking(candidate.get());
        if (!nonBlocking.isOk()) {
            ::freeaddrinfo(results);
            return nonBlocking;
        }

        listener = std::move(candidate);
        break;
    }

    ::freeaddrinfo(results);
    if (!listener.isValid()) {
        return lastSystemError("bind/listen");
    }

    return listener;
}

common::Result<UniqueFd> createNonBlockingConnection(const char* host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    const int gaiStatus = ::getaddrinfo(host, portText.c_str(), &hints, &results);
    if (gaiStatus != 0) {
        return common::Status::runtimeError(std::string("getaddrinfo: ") + gai_strerror(gaiStatus));
    }

    UniqueFd connection;
    int lastError = 0;
    for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
        UniqueFd candidate(::socket(item->ai_family, item->ai_socktype, item->ai_protocol));
        if (!candidate.isValid()) {
            lastError = errno;
            continue;
        }

        auto nonBlocking = setNonBlocking(candidate.get());
        if (!nonBlocking.isOk()) {
            ::freeaddrinfo(results);
            return nonBlocking;
        }

        if (::connect(candidate.get(), item->ai_addr, item->ai_addrlen) == 0 ||
            errno == EINPROGRESS) {
            connection = std::move(candidate);
            break;
        }

        lastError = errno;
    }

    ::freeaddrinfo(results);
    if (!connection.isValid()) {
        return common::Status::systemError("connect: " + std::string(std::strerror(lastError)),
                                           lastError);
    }

    return connection;
}

common::Result<UniqueFd> connectBlockingSocket(const char* host, std::uint16_t port,
                                               SocketDeadline deadline) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const auto portText = std::to_string(port);
    const int resolved = ::getaddrinfo(host, portText.c_str(), &hints, &addresses);
    if (resolved != 0)
        return common::Status::runtimeError(std::string("getaddrinfo: ") + gai_strerror(resolved));
    struct AddressGuard { addrinfo* value; ~AddressGuard() { ::freeaddrinfo(value); } } guard{addresses};
    UniqueFd fd;
    int lastError = ECONNREFUSED;
    for (auto* address = addresses; address; address = address->ai_next) {
        UniqueFd candidate(::socket(address->ai_family, address->ai_socktype | SOCK_CLOEXEC, address->ai_protocol));
        if (!candidate.isValid()) { lastError = errno; continue; }
        auto nonblocking = setNonBlocking(candidate.get());
        if (!nonblocking.isOk()) return nonblocking;
        if (::connect(candidate.get(), address->ai_addr, address->ai_addrlen) != 0) {
            if (errno != EINPROGRESS) { lastError = errno; continue; }
            for (;;) {
                int timeout = -1;
                if (deadline) {
                    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                        *deadline - std::chrono::steady_clock::now()).count();
                    if (left <= 0) return common::Status::transportError("connection deadline exceeded", ETIMEDOUT);
                    timeout = static_cast<int>(std::min<std::int64_t>(left, INT_MAX));
                }
                pollfd descriptor{candidate.get(), POLLOUT, 0};
                const int ready = ::poll(&descriptor, 1, timeout);
                if (ready < 0 && errno == EINTR) continue;
                if (ready <= 0) return common::Status::transportError("connection timed out or poll failed",
                                                                      ready == 0 ? ETIMEDOUT : errno);
                auto error = getSocketError(candidate.get());
                if (!error.isOk()) return error.status();
                lastError = error.value();
                break;
            }
            if (lastError) continue;
        }
        fd = std::move(candidate);
        break;
    }
    if (!fd.isValid()) return common::Status::transportError(
        "connect: " + std::string(std::strerror(lastError)), lastError);
    const int flags = ::fcntl(fd.get(), F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd.get(), F_SETFL, flags & ~O_NONBLOCK) < 0)
        return lastSystemError("fcntl blocking socket");
    if (deadline) {
        auto left = std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::microseconds>(
            *deadline - std::chrono::steady_clock::now()).count());
        timeval timeout{left / 1000000, left % 1000000};
        if (::setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
            ::setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
            return lastSystemError("socket deadline");
    }
    return fd;
}

common::Result<UniqueFd> createEpoll() {
    UniqueFd epollFd(::epoll_create1(EPOLL_CLOEXEC));
    if (!epollFd.isValid()) {
        return lastSystemError("epoll_create1");
    }

    return epollFd;
}

common::Status addEpollFd(int epollFd, int fd, std::uint32_t events, void* data) {
    epoll_event event{};
    event.events = events;
    event.data.ptr = data;
    if (::epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) != 0) {
        return lastSystemError("epoll_ctl(ADD)");
    }
    return common::Status::ok();
}

common::Status modifyEpollFd(int epollFd, int fd, std::uint32_t events, void* data) {
    epoll_event event{};
    event.events = events;
    event.data.ptr = data;
    if (::epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event) != 0) {
        return lastSystemError("epoll_ctl(MOD)");
    }
    return common::Status::ok();
}

common::Status deleteEpollFd(int epollFd, int fd) {
    if (::epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) != 0 && errno != EBADF &&
        errno != ENOENT) {
        return lastSystemError("epoll_ctl(DEL)");
    }
    return common::Status::ok();
}

common::Result<int> getSocketError(int fd) {
    int socketError = 0;
    socklen_t length = sizeof(socketError);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) != 0) {
        return lastSystemError("getsockopt(SO_ERROR)");
    }

    return socketError;
}

}  // namespace cpnetflux::core::io
