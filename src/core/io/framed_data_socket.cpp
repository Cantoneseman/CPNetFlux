#include "cpnetflux/core/io/framed_data_socket.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace cpnetflux::core::io {
namespace {

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::transportError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}


}  // namespace

FramedDataSocket::FramedDataSocket() = default;
FramedDataSocket::~FramedDataSocket() = default;

FramedDataSocket::FramedDataSocket(UniqueFd fd) : tlsMode_(false), fd_(std::move(fd)) {}

FramedDataSocket::FramedDataSocket(TlsConnection connection)
    : tlsMode_(connection.tlsEnabled()), tls_(std::move(connection)) {}

FramedDataSocket::FramedDataSocket(RdmaDataSocket connection)
    : rdmaMode_(true), rdma_(std::move(connection)) {}

FramedDataSocket::FramedDataSocket(FramedDataSocket&& other) noexcept = default;
FramedDataSocket& FramedDataSocket::operator=(FramedDataSocket&& other) noexcept = default;

int FramedDataSocket::fd() const noexcept {
    if (rdmaMode_) return rdma_.fd();
    return tlsMode_ ? tls_.fd() : fd_.get();
}

bool FramedDataSocket::valid() const noexcept {
    if (rdmaMode_) return rdma_.valid();
    return tlsMode_ ? tls_.valid() : fd_.isValid();
}

bool FramedDataSocket::tlsEnabled() const noexcept { return tlsMode_; }

DataTransport FramedDataSocket::transport() const noexcept {
    return rdmaMode_ ? DataTransport::Rdma : DataTransport::Tcp;
}

RdmaTransportStats FramedDataSocket::rdmaStats() const noexcept {
    return rdmaMode_ ? rdma_.stats() : RdmaTransportStats{};
}

void FramedDataSocket::shutdown() noexcept {
    if (rdmaMode_) {
        rdma_.shutdown();
    } else if (fd() >= 0) {
        (void)::shutdown(fd(), SHUT_RDWR);
    }
}

void FramedDataSocket::close() noexcept {
    if (rdmaMode_) {
        rdma_.close();
        return;
    }
    if (tlsMode_) {
        tls_ = TlsConnection{};
    } else {
        fd_.reset();
    }
}

common::Status FramedDataSocket::writeAll(const std::uint8_t* data, std::size_t length) {
    if (rdmaMode_) return rdma_.writeAll(data, length);
    if (tlsMode_) {
        common::Status status =
            tls_.writeAll(reinterpret_cast<const char*>(data), length);
        if (!status.isOk()) {
            return status.code() == common::StatusCode::TransportError
                ? common::Status::transportError("data TLS write failed: " + status.message(), status.errorNumber())
                : common::Status::runtimeError("data TLS write failed: " + status.message());
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
        return common::Status::transportError("send returned zero bytes");
    }
    return common::Status::ok();
}

common::Status FramedDataSocket::writeFrame(const std::uint8_t* header,
                                           std::size_t headerLength,
                                           const std::uint8_t* payload,
                                           std::size_t payloadLength) {
    return writeFrame(header, headerLength, nullptr, 0, payload, payloadLength);
}

common::Status FramedDataSocket::writeFrame(const std::uint8_t* header,
                                           std::size_t headerLength,
                                           const std::uint8_t* prefix,
                                           std::size_t prefixLength,
                                           const std::uint8_t* payload,
                                           std::size_t payloadLength) {
    const auto maxLength = static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
    if ((headerLength != 0 && header == nullptr) ||
        (prefixLength != 0 && prefix == nullptr) ||
        (payloadLength != 0 && payload == nullptr) ||
        headerLength > maxLength || prefixLength > maxLength - headerLength ||
        payloadLength > maxLength - headerLength - prefixLength) {
        return common::Status::invalidArgument("invalid frame output spans");
    }
    if (rdmaMode_) {
        return rdma_.writeFrame(header, headerLength, prefix, prefixLength,
                                payload, payloadLength);
    }
    if (tlsMode_) {
        constexpr std::size_t kTlsCombinedPrefixCapacity = 68;
        if (headerLength + prefixLength <= kTlsCombinedPrefixCapacity) {
            std::array<std::uint8_t, kTlsCombinedPrefixCapacity> combined{};
            if (headerLength != 0)
                std::memcpy(combined.data(), header, headerLength);
            if (prefixLength != 0)
                std::memcpy(combined.data() + headerLength, prefix, prefixLength);
            auto status = writeAll(combined.data(), headerLength + prefixLength);
            if (!status.isOk()) return status;
        } else {
            auto status = writeAll(header, headerLength);
            if (!status.isOk()) return status;
            status = writeAll(prefix, prefixLength);
            if (!status.isOk()) return status;
        }
        return writeAll(payload, payloadLength);
    }
    iovec buffers[3]{};
    std::size_t count = 0;
    if (headerLength != 0) buffers[count++] = {const_cast<std::uint8_t*>(header), headerLength};
    if (prefixLength != 0) buffers[count++] = {const_cast<std::uint8_t*>(prefix), prefixLength};
    if (payloadLength != 0) buffers[count++] = {const_cast<std::uint8_t*>(payload), payloadLength};
    std::size_t current = 0;
    while (current < count) {
        msghdr message{};
        message.msg_iov = buffers + current;
        message.msg_iovlen = count - current;
        const auto sent = ::sendmsg(fd_.get(), &message, MSG_NOSIGNAL);
        if (sent > 0) {
            auto completed = static_cast<std::size_t>(sent);
            // A short write may end inside either span or exactly between them.
            while (completed != 0) {
                if (completed >= buffers[current].iov_len) {
                    completed -= buffers[current].iov_len;
                    ++current;
                } else {
                    buffers[current].iov_base = static_cast<std::uint8_t*>(buffers[current].iov_base) + completed;
                    buffers[current].iov_len -= completed;
                    completed = 0;
                }
            }
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0) return systemStatus("sendmsg", errno);
        return common::Status::transportError("sendmsg returned zero bytes");
    }
    return common::Status::ok();
}

common::Status FramedDataSocket::writeFrameBuffered(const std::uint8_t* header,
                                                   std::size_t headerLength,
                                                   const std::uint8_t* prefix,
                                                   std::size_t prefixLength,
                                                   const std::uint8_t* payload,
                                                   std::size_t payloadLength) {
    if (rdmaMode_)
        return rdma_.writeFrameBuffered(header, headerLength, prefix, prefixLength,
                                        payload, payloadLength);
    return writeFrame(header, headerLength, prefix, prefixLength, payload, payloadLength);
}

common::Result<std::size_t> FramedDataSocket::readSome(std::uint8_t* data, std::size_t length) {
    if (rdmaMode_) return rdma_.readSome(data, length);
    if (tlsMode_) {
        auto result = tls_.readSome(reinterpret_cast<char*>(data), length);
        if (!result.isOk()) {
            const auto& status = result.status();
            return status.code() == common::StatusCode::TransportError
                ? common::Status::transportError("data TLS read failed: " + status.message(), status.errorNumber())
                : common::Status::runtimeError("data TLS read failed: " + status.message());
        }
        return result;
    }
    const ssize_t received = ::recv(fd_.get(), data, length, 0);
    if (received > 0) {
        return static_cast<std::size_t>(received);
    }
    if (received == 0) {
        return common::Status::transportError("data connection closed");
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
                                                         const TlsConfig& tlsConfig,
                                                         const char* tlsServerName, SocketDeadline deadline,
                                                         std::string_view localBindAddress,
                                                         DataTransport transport) {
    if (transport == DataTransport::Rdma) {
        if (dataTlsMode != DataTlsMode::Off) {
            return common::Status::invalidArgument("RDMA data transport does not support data TLS");
        }
        auto connection = RdmaDataSocket::connect(host, port, deadline, localBindAddress);
        if (!connection.isOk()) return connection.status();
        return FramedDataSocket(std::move(connection.value()));
    }
    auto connection = connectBlockingSocket(host, port, deadline, localBindAddress);
    if (!connection.isOk()) {
        return connection.status();
    }
    if (dataTlsMode == DataTlsMode::Off) {
        return FramedDataSocket(std::move(connection.value()));
    }
    TlsConfig clientConfig = tlsConfig;
    clientConfig.mode = TlsMode::Required;
    clientConfig.clientAuth = TlsClientAuth::None;
    const common::Status validation = validateTlsClientConfig(clientConfig);
    if (!validation.isOk()) {
        return validation;
    }
    auto context = TlsClientContext::create(clientConfig);
    if (!context.isOk()) {
        return context.status();
    }
    const char* peerName = (tlsServerName != nullptr && *tlsServerName != '\0')
                                ? tlsServerName
                                : host;
    auto tlsConnection = context.value().connect(std::move(connection.value()), peerName, deadline);
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

}  // namespace cpnetflux::core::io
