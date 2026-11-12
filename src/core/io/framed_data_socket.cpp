#include "cpnetflux/core/io/framed_data_socket.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <algorithm>
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

common::Status FramedDataSocket::configureDataFrameBytes(std::uint32_t logicalBytes) {
    if (dataLimitConfigured_ || (logicalBytes != 65532 && logicalBytes != 262144) ||
        (logicalBytes == 262144 && (tlsEnabled() || transport() != DataTransport::Tcp)))
        return common::Status::invalidArgument("unsupported or changed V3 DATA limit");
    dataPayloadLimit_ = logicalBytes + 4;
    dataLimitConfigured_ = true;
    return common::Status::ok();
}

DataTransport FramedDataSocket::transport() const noexcept {
    return rdmaMode_ ? DataTransport::Rdma : DataTransport::Tcp;
}

common::Status FramedDataSocket::configureTcpReceiveBuffer(std::uint32_t requestedBytes) {
    if (requestedBytes != 0 && requestedBytes != 1048576)
        return common::Status::invalidArgument("TCP receive buffer must be off or 1048576");
    if (tlsEnabled() || transport() != DataTransport::Tcp) {
        if (requestedBytes != 0)
            return common::Status::invalidArgument("TCP receive buffer requires plain TCP data");
        return common::Status::ok();
    }
    if (requestedBytes != 0) {
        const int value = static_cast<int>(requestedBytes);
        if (::setsockopt(fd(), SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) != 0)
            return systemStatus("setsockopt(SO_RCVBUF)",errno);
    }
    int effective = 0;
    socklen_t length = sizeof(effective);
    if (::getsockopt(fd(), SOL_SOCKET, SO_RCVBUF, &effective, &length) != 0)
        return systemStatus("getsockopt(SO_RCVBUF)",errno);
    if (requestedBytes != 0 && (effective < static_cast<int>(requestedBytes) ||
        effective > static_cast<int>(2*requestedBytes)))
        return common::Status::invalidArgument("TCP receive buffer outside bounded kernel capacity");
    tcpReceiveBufferBytes_ = static_cast<std::uint32_t>(effective);
    return common::Status::ok();
}

common::Status FramedDataSocket::configureTcpExactRead(bool enabled) {
    if (enabled && (!valid() || tlsEnabled() || transport() != DataTransport::Tcp ||
                    dataPayloadLimit_ != 262148))
        return common::Status::invalidArgument("TCP exact read requires plain TCP 256KiB DATA");
    tcpExactRead_ = {};
    tcpExactRead_.enabled = enabled;
    return common::Status::ok();
}

common::Status FramedDataSocket::configureTcpKernelPacing(std::uint64_t bytesPerSecond) {
    if (!bytesPerSecond && !tcpKernelPacing_.enabled) return common::Status::ok();
    if (bytesPerSecond != 0 && bytesPerSecond != 2000000000ULL)
        return common::Status::invalidArgument("TCP kernel pacing must be off or 2000000000");
    if (!valid() || tlsMode_ || rdmaMode_ || dataPayloadLimit_ != 262148)
        return common::Status::invalidArgument("TCP kernel pacing requires plain TCP 256KiB DATA");
#if defined(__linux__) && defined(SO_MAX_PACING_RATE)
    int protocol = 0;
    socklen_t protocolLength = sizeof(protocol);
    if (::getsockopt(fd(), SOL_SOCKET, SO_PROTOCOL, &protocol, &protocolLength) != 0)
        return systemStatus("getsockopt(SO_PROTOCOL)", errno);
    if (protocolLength != sizeof(protocol) || protocol != IPPROTO_TCP)
        return common::Status::invalidArgument("TCP kernel pacing rejects non-TCP sockets");
    std::uint64_t previous = 0;
    socklen_t length = sizeof(previous);
    if (::getsockopt(fd(), SOL_SOCKET, SO_MAX_PACING_RATE, &previous, &length) != 0)
        return systemStatus("getsockopt(SO_MAX_PACING_RATE)", errno);
    if (length != sizeof(previous))
        return common::Status::invalidArgument("unexpected TCP pacing option width");
    const auto requested = bytesPerSecond ? bytesPerSecond : tcpKernelPacingOriginalLimit_;
    if (::setsockopt(fd(), SOL_SOCKET, SO_MAX_PACING_RATE, &requested, sizeof(requested)) != 0)
        return systemStatus("setsockopt(SO_MAX_PACING_RATE)", errno);
    std::uint64_t effective = 0;
    length = sizeof(effective);
    const int readback = ::getsockopt(fd(), SOL_SOCKET, SO_MAX_PACING_RATE, &effective, &length);
    const int error = errno;
    if (readback != 0 || length != sizeof(effective) || effective != requested) {
        // Do not silently leave a partial configuration selected.
        if (::setsockopt(fd(), SOL_SOCKET, SO_MAX_PACING_RATE, &previous, sizeof(previous)) != 0)
            close();
        if (readback != 0) return systemStatus("TCP pacing readback", error);
        return common::Status::runtimeError("TCP kernel pacing ceiling readback mismatch");
    }
    if (!tcpKernelPacing_.enabled && bytesPerSecond) tcpKernelPacingOriginalLimit_ = previous;
    tcpKernelPacing_ = {bytesPerSecond != 0, bytesPerSecond, bytesPerSecond ? effective : 0};
    if (!bytesPerSecond) tcpKernelPacingOriginalLimit_ = 0;
    return common::Status::ok();
#else
    return common::Status::invalidArgument("TCP kernel pacing unavailable in this build");
#endif
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
    tcpExactRead_ = {};
    tcpKernelPacing_ = {};
    tcpKernelPacingOriginalLimit_ = 0;
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
    if (tcpExactRead_.enabled && length > 65536) {
        if (!valid() || tlsMode_ || rdmaMode_ || !data || length > dataPayloadLimit_)
            return common::Status::invalidArgument("invalid TCP exact-read span");
        ++tcpExactRead_.largeReadCalls;
        std::size_t completed = 0;
        while (completed < length) {
            ++tcpExactRead_.recvCalls;
            const auto requested = length - completed;
            const auto received = ::recv(fd_.get(), data + completed, requested, MSG_WAITALL);
            // WAITALL may return short on signal, timeout or disconnect. Never
            // publish a partial frame, and never reuse bytes before completion.
            if (received > 0) {
                tcpExactRead_.bytes += static_cast<std::size_t>(received);
                tcpExactRead_.shortReturns += static_cast<std::size_t>(received) < requested;
                completed += static_cast<std::size_t>(received);
                continue;
            }
            if (received < 0 && errno == EINTR) {
                ++tcpExactRead_.interruptions;
                continue;
            }
            if (received < 0) return systemStatus("recv(MSG_WAITALL)", errno);
            return common::Status::transportError("data connection closed during TCP exact read");
        }
        return common::Status::ok();
    }
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

common::Status FramedDataSocket::readAllV(std::uint8_t* prefix, std::size_t prefixLength,
                                         std::uint8_t* payload, std::size_t payloadLength) {
    if (tlsMode_ || rdmaMode_ || !fd_.isValid())
        return common::Status::invalidArgument("scatter receive requires a plain TCP socket");
    if ((prefixLength && !prefix) || (payloadLength && !payload) ||
        prefixLength > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()) ||
        payloadLength > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()) - prefixLength)
        return common::Status::invalidArgument("invalid scatter receive spans");
    std::array<iovec, 2> spans{{{prefix, prefixLength}, {payload, payloadLength}}};
    std::size_t first = 0;
    while (first < spans.size()) {
        while (first < spans.size() && spans[first].iov_len == 0) ++first;
        if (first == spans.size()) break;
        const auto count = ::readv(fd_.get(), spans.data() + first,
                                   static_cast<int>(spans.size() - first));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return common::Status::systemError("TCP scatter receive failed", errno);
        if (count == 0) return common::Status::transportError("TCP scatter receive closed");
        auto remaining = static_cast<std::size_t>(count);
        while (remaining) {
            const auto consumed = std::min(remaining, spans[first].iov_len);
            spans[first].iov_base = static_cast<std::uint8_t*>(spans[first].iov_base) + consumed;
            spans[first].iov_len -= consumed;
            remaining -= consumed;
            if (!spans[first].iov_len) ++first;
        }
    }
    return common::Status::ok();
}

common::Result<FramedDataSocket> connectFramedDataSocket(const char* host, std::uint16_t port,
                                                         DataTlsMode dataTlsMode,
                                                         const TlsConfig& tlsConfig,
                                                         const char* tlsServerName, SocketDeadline deadline,
                                                         std::string_view localBindAddress,
                                                         DataTransport transport, std::span<const std::uint16_t> sourcePorts,
                                                         std::uint32_t* sourceBindAttempts) {
    if (transport == DataTransport::Rdma) {
        if (!sourcePorts.empty()) return common::Status::invalidArgument("RDMA does not support TCP source port plans");
        if (dataTlsMode != DataTlsMode::Off) {
            return common::Status::invalidArgument("RDMA data transport does not support data TLS");
        }
        auto connection = RdmaDataSocket::connect(host, port, deadline, localBindAddress);
        if (!connection.isOk()) return connection.status();
        return FramedDataSocket(std::move(connection.value()));
    }
    auto connection = connectBlockingSocket(host, port, deadline, localBindAddress, sourcePorts, sourceBindAttempts);
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
