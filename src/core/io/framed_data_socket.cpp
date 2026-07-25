#include "gridflux/core/io/framed_data_socket.h"

#include <netdb.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

namespace gridflux::core::io {
namespace {

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

common::Result<UniqueFd> createBlockingConnection(const char* host, std::uint16_t port) {
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
        UniqueFd candidate(
            ::socket(item->ai_family, item->ai_socktype | SOCK_CLOEXEC, item->ai_protocol));
        if (!candidate.isValid()) {
            lastError = errno;
            continue;
        }
        if (::connect(candidate.get(), item->ai_addr, item->ai_addrlen) == 0) {
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

}  // namespace

FramedDataSocket::FramedDataSocket() = default;

FramedDataSocket::FramedDataSocket(UniqueFd fd) : tlsMode_(false), fd_(std::move(fd)) {}

FramedDataSocket::FramedDataSocket(TlsConnection connection)
    : tlsMode_(connection.tlsEnabled()), tls_(std::move(connection)) {}

FramedDataSocket::FramedDataSocket(FramedDataSocket&& other) noexcept = default;
FramedDataSocket& FramedDataSocket::operator=(FramedDataSocket&& other) noexcept = default;

int FramedDataSocket::fd() const noexcept { return tlsMode_ ? tls_.fd() : fd_.get(); }

bool FramedDataSocket::valid() const noexcept {
    return tlsMode_ ? tls_.valid() : fd_.isValid();
}

bool FramedDataSocket::tlsEnabled() const noexcept { return tlsMode_; }

common::Status FramedDataSocket::writeAll(const std::uint8_t* data, std::size_t length) {
    if (tlsMode_) {
        common::Status status =
            tls_.writeAll(reinterpret_cast<const char*>(data), length);
        if (!status.isOk()) {
            return common::Status::runtimeError("data TLS write failed: " + status.message());
        }
        return status;
    }
    std::size_t completed = 0;
    while (completed < length) {
        const ssize_t sent =
            ::send(fd_.get(), data + completed, length - completed, MSG_NOSIGNAL);
        if (sent > 0) {
            completed += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0) {
            return systemStatus("send", errno);
        }
        return common::Status::runtimeError("send returned zero bytes");
    }
    return common::Status::ok();
}

common::Result<std::size_t> FramedDataSocket::readSome(std::uint8_t* data, std::size_t length) {
    if (tlsMode_) {
        auto result = tls_.readSome(reinterpret_cast<char*>(data), length);
        if (!result.isOk()) {
            return common::Status::runtimeError("data TLS read failed: " +
                                                result.status().message());
        }
        return result;
    }
    const ssize_t received = ::recv(fd_.get(), data, length, 0);
    if (received > 0) {
        return static_cast<std::size_t>(received);
    }
    if (received == 0) {
        return common::Status::runtimeError("data connection closed");
    }
    if (errno == EINTR) {
        return std::size_t{0};
    }
    return systemStatus("recv", errno);
}

common::Status FramedDataSocket::readAll(std::uint8_t* data, std::size_t length) {
    std::size_t completed = 0;
    while (completed < length) {
        auto received = readSome(data + completed, length - completed);
        if (!received.isOk()) {
            return received.status();
        }
        if (received.value() == 0) {
            continue;
        }
        completed += received.value();
    }
    return common::Status::ok();
}

common::Result<FramedDataSocket> connectFramedDataSocket(const char* host, std::uint16_t port,
                                                         DataTlsMode dataTlsMode,
                                                         const TlsConfig& tlsConfig) {
    auto connection = createBlockingConnection(host, port);
    if (!connection.isOk()) {
        return connection.status();
    }
    if (dataTlsMode == DataTlsMode::Off) {
        return FramedDataSocket(std::move(connection.value()));
    }
    TlsConfig clientConfig = tlsConfig;
    clientConfig.mode = TlsMode::Required;
    clientConfig.certFile.clear();
    clientConfig.keyFile.clear();
    const common::Status validation = validateTlsClientConfig(clientConfig);
    if (!validation.isOk()) {
        return validation;
    }
    auto context = TlsClientContext::create(clientConfig);
    if (!context.isOk()) {
        return context.status();
    }
    auto tlsConnection = context.value().connect(std::move(connection.value()), host);
    if (!tlsConnection.isOk()) {
        return common::Status::runtimeError("data TLS handshake failed: " +
                                            tlsConnection.status().message());
    }
    return FramedDataSocket(std::move(tlsConnection.value()));
}

common::Result<FramedDataSocket> acceptFramedDataSocket(UniqueFd fd, DataTlsMode dataTlsMode,
                                                        const TlsConfig& tlsConfig) {
    if (dataTlsMode == DataTlsMode::Off) {
        return FramedDataSocket(std::move(fd));
    }
    TlsConfig serverConfig = tlsConfig;
    serverConfig.mode = TlsMode::Required;
    const common::Status validation = validateTlsServerConfig(serverConfig);
    if (!validation.isOk()) {
        return validation;
    }
    auto context = TlsServerContext::create(serverConfig);
    if (!context.isOk()) {
        return context.status();
    }
    auto tlsConnection = context.value().accept(std::move(fd));
    if (!tlsConnection.isOk()) {
        return common::Status::runtimeError("data TLS handshake failed: " +
                                            tlsConnection.status().message());
    }
    return FramedDataSocket(std::move(tlsConnection.value()));
}

}  // namespace gridflux::core::io
