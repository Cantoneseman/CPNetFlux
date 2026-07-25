#include "gridflux/core/io/tls_socket.h"

#include <cerrno>
#include <cstring>
#include <utility>

#include <sys/socket.h>

namespace gridflux::core::io {
namespace {

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

}  // namespace

struct TlsConnection::Impl {};
struct TlsServerContext::Impl {};
struct TlsClientContext::Impl {};

common::Result<TlsMode> parseTlsMode(std::string_view value) {
    if (value == "off") {
        return TlsMode::Off;
    }
    if (value == "explicit") {
        return TlsMode::Explicit;
    }
    if (value == "required") {
        return TlsMode::Required;
    }
    return common::Status::invalidArgument("--tls-mode must be off, explicit, or required");
}

const char* tlsModeName(TlsMode mode) noexcept {
    switch (mode) {
        case TlsMode::Off:
            return "off";
        case TlsMode::Explicit:
            return "explicit";
        case TlsMode::Required:
            return "required";
    }
    return "off";
}

common::Result<DataTlsMode> parseDataTlsMode(std::string_view value) {
    if (value == "off") {
        return DataTlsMode::Off;
    }
    if (value == "required") {
        return DataTlsMode::Required;
    }
    return common::Status::invalidArgument("--data-tls-mode must be off or required");
}

const char* dataTlsModeName(DataTlsMode mode) noexcept {
    switch (mode) {
        case DataTlsMode::Off:
            return "off";
        case DataTlsMode::Required:
            return "required";
    }
    return "off";
}

bool tlsSupportAvailable() noexcept { return false; }

common::Status validateTlsServerConfig(const TlsConfig& config) {
    if (config.mode == TlsMode::Off) {
        return common::Status::ok();
    }
    if (config.mode == TlsMode::Explicit) {
        return common::Status::invalidArgument(
            "TLS explicit mode is reserved for a later phase");
    }
    return common::Status::invalidArgument(
        "TLS support unavailable: OpenSSL development files were not found at build time");
}

common::Status validateTlsClientConfig(const TlsConfig& config) {
    if (config.mode == TlsMode::Off) {
        return common::Status::ok();
    }
    if (config.mode == TlsMode::Explicit) {
        return common::Status::invalidArgument(
            "TLS explicit mode is reserved for a later phase");
    }
    return common::Status::invalidArgument(
        "TLS support unavailable: OpenSSL development files were not found at build time");
}

common::Status validateDataTlsServerConfig(TlsMode controlMode, DataTlsMode dataMode,
                                           const TlsConfig& tlsConfig) {
    if (dataMode == DataTlsMode::Off) {
        return common::Status::ok();
    }
    if (controlMode != TlsMode::Required) {
        return common::Status::invalidArgument(
            "--data-tls-mode required requires --tls-mode required");
    }
    return validateTlsServerConfig(tlsConfig);
}

common::Status validateDataTlsClientConfig(DataTlsMode dataMode, const TlsConfig& tlsConfig) {
    if (dataMode == DataTlsMode::Off) {
        return common::Status::ok();
    }
    TlsConfig clientConfig = tlsConfig;
    clientConfig.mode = TlsMode::Required;
    clientConfig.certFile.clear();
    clientConfig.keyFile.clear();
    return validateTlsClientConfig(clientConfig);
}

TlsConnection::TlsConnection() = default;
TlsConnection::~TlsConnection() = default;
TlsConnection::TlsConnection(UniqueFd fd) : fd_(std::move(fd)) {}
TlsConnection::TlsConnection(UniqueFd fd, std::unique_ptr<Impl> impl)
    : fd_(std::move(fd)), impl_(std::move(impl)) {}
TlsConnection::TlsConnection(TlsConnection&& other) noexcept = default;
TlsConnection& TlsConnection::operator=(TlsConnection&& other) noexcept = default;

TlsConnection TlsConnection::plain(UniqueFd fd) { return TlsConnection(std::move(fd)); }

int TlsConnection::fd() const noexcept { return fd_.get(); }
bool TlsConnection::valid() const noexcept { return fd_.isValid(); }
bool TlsConnection::tlsEnabled() const noexcept { return impl_ != nullptr; }

common::Status TlsConnection::writeAll(const char* data, std::size_t size) {
    std::size_t completed = 0;
    while (completed < size) {
        const ssize_t sent =
            ::send(fd_.get(), data + completed, size - completed, MSG_NOSIGNAL);
        if (sent > 0) {
            completed += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0) {
            return systemStatus("send control", errno);
        }
        return common::Status::runtimeError("control send returned zero bytes");
    }
    return common::Status::ok();
}

common::Result<std::size_t> TlsConnection::readSome(char* data, std::size_t size) {
    const ssize_t received = ::recv(fd_.get(), data, size, 0);
    if (received > 0) {
        return static_cast<std::size_t>(received);
    }
    if (received == 0) {
        return common::Status::runtimeError("control connection closed");
    }
    if (errno == EINTR) {
        return std::size_t{0};
    }
    return systemStatus("recv control", errno);
}

TlsServerContext::TlsServerContext() = default;
TlsServerContext::~TlsServerContext() = default;
TlsServerContext::TlsServerContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TlsServerContext::TlsServerContext(TlsServerContext&& other) noexcept = default;
TlsServerContext& TlsServerContext::operator=(TlsServerContext&& other) noexcept = default;

common::Result<TlsServerContext> TlsServerContext::create(const TlsConfig& config) {
    const common::Status status = validateTlsServerConfig(config);
    if (!status.isOk()) {
        return status;
    }
    return TlsServerContext();
}

common::Result<TlsConnection> TlsServerContext::accept(UniqueFd fd) const {
    return TlsConnection::plain(std::move(fd));
}

TlsClientContext::TlsClientContext() = default;
TlsClientContext::~TlsClientContext() = default;
TlsClientContext::TlsClientContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TlsClientContext::TlsClientContext(TlsClientContext&& other) noexcept = default;
TlsClientContext& TlsClientContext::operator=(TlsClientContext&& other) noexcept = default;

common::Result<TlsClientContext> TlsClientContext::create(const TlsConfig& config) {
    const common::Status status = validateTlsClientConfig(config);
    if (!status.isOk()) {
        return status;
    }
    return TlsClientContext();
}

common::Result<TlsConnection> TlsClientContext::connect(UniqueFd fd, const std::string&) const {
    return TlsConnection::plain(std::move(fd));
}

}  // namespace gridflux::core::io
