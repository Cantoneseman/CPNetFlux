#include "gridflux/core/io/tls_socket.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <string>
#include <utility>

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gridflux::core::io {
namespace {

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

common::Status requireRegularReadableFile(const std::string& path, const char* label) {
    if (path.empty()) {
        return common::Status::invalidArgument(std::string(label) + " file is required");
    }
    struct stat statBuffer {};
    if (::stat(path.c_str(), &statBuffer) != 0) {
        return common::Status::invalidArgument(std::string(label) + " file cannot be inspected");
    }
    if (!S_ISREG(statBuffer.st_mode)) {
        return common::Status::invalidArgument(std::string(label) + " file must be a regular file");
    }
    if (::access(path.c_str(), R_OK) != 0) {
        return common::Status::invalidArgument(std::string(label) + " file cannot be read");
    }
    return common::Status::ok();
}

common::Status validatePrivateKeyPermissions(const std::string& path) {
    struct stat statBuffer {};
    if (::stat(path.c_str(), &statBuffer) != 0) {
        return common::Status::invalidArgument("TLS private key file cannot be inspected");
    }
    if ((statBuffer.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        return common::Status::invalidArgument(
            "TLS private key permissions must not allow group or other access");
    }
    return common::Status::ok();
}

common::Status opensslStatus(const char* operation) {
    unsigned long error = ERR_get_error();
    if (error == 0) {
        return common::Status::runtimeError(std::string(operation) + " failed");
    }
    char buffer[256];
    ERR_error_string_n(error, buffer, sizeof(buffer));
    return common::Status::runtimeError(std::string(operation) + " failed: " + buffer);
}

common::Status tlsIoStatus(SSL* ssl, int result, const char* operation) {
    const int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
        return common::Status::runtimeError(std::string(operation) + " would block");
    }
    if (error == SSL_ERROR_ZERO_RETURN) {
        return common::Status::runtimeError("control connection closed");
    }
    if (error == SSL_ERROR_SYSCALL && errno != 0) {
        return systemStatus(operation, errno);
    }
    return opensslStatus(operation);
}

void initializeOpenSsl() { (void)OPENSSL_init_ssl(0, nullptr); }

}  // namespace

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

bool tlsSupportAvailable() noexcept { return true; }

common::Status validateTlsServerConfig(const TlsConfig& config) {
    if (config.mode == TlsMode::Off) {
        return common::Status::ok();
    }
    if (config.mode == TlsMode::Explicit) {
        return common::Status::invalidArgument(
            "TLS explicit mode is reserved for a later phase");
    }
    common::Status status = requireRegularReadableFile(config.certFile, "TLS certificate");
    if (!status.isOk()) {
        return status;
    }
    status = requireRegularReadableFile(config.keyFile, "TLS private key");
    if (!status.isOk()) {
        return status;
    }
    status = validatePrivateKeyPermissions(config.keyFile);
    if (!status.isOk()) {
        return status;
    }
    if (!config.caFile.empty()) {
        status = requireRegularReadableFile(config.caFile, "TLS CA");
        if (!status.isOk()) {
            return status;
        }
    }
    return common::Status::ok();
}

common::Status validateTlsClientConfig(const TlsConfig& config) {
    if (config.mode == TlsMode::Off) {
        return common::Status::ok();
    }
    if (config.mode == TlsMode::Explicit) {
        return common::Status::invalidArgument(
            "TLS explicit mode is reserved for a later phase");
    }
    if (!config.caFile.empty()) {
        return requireRegularReadableFile(config.caFile, "TLS CA");
    }
    return common::Status::ok();
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

struct TlsConnection::Impl {
    explicit Impl(SSL* sslValue) : ssl(sslValue) {}
    ~Impl() {
        if (ssl != nullptr) {
            (void)SSL_shutdown(ssl);
            SSL_free(ssl);
        }
    }
    SSL* ssl = nullptr;
};

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
        if (impl_ == nullptr) {
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

        const std::size_t remaining = size - completed;
        const int chunk = remaining > static_cast<std::size_t>(INT_MAX)
                              ? INT_MAX
                              : static_cast<int>(remaining);
        const int written = SSL_write(impl_->ssl, data + completed, chunk);
        if (written > 0) {
            completed += static_cast<std::size_t>(written);
            continue;
        }
        const int error = SSL_get_error(impl_->ssl, written);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE ||
            (error == SSL_ERROR_SYSCALL && errno == EINTR)) {
            continue;
        }
        return tlsIoStatus(impl_->ssl, written, "TLS write control");
    }
    return common::Status::ok();
}

common::Result<std::size_t> TlsConnection::readSome(char* data, std::size_t size) {
    if (impl_ == nullptr) {
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

    const int request = size > static_cast<std::size_t>(INT_MAX) ? INT_MAX : static_cast<int>(size);
    const int received = SSL_read(impl_->ssl, data, request);
    if (received > 0) {
        return static_cast<std::size_t>(received);
    }
    const int error = SSL_get_error(impl_->ssl, received);
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE ||
        (error == SSL_ERROR_SYSCALL && errno == EINTR)) {
        return std::size_t{0};
    }
    return tlsIoStatus(impl_->ssl, received, "TLS read control");
}

struct TlsServerContext::Impl {
    explicit Impl(SSL_CTX* contextValue) : context(contextValue) {}
    ~Impl() {
        if (context != nullptr) {
            SSL_CTX_free(context);
        }
    }
    SSL_CTX* context = nullptr;
};

TlsServerContext::TlsServerContext() = default;
TlsServerContext::~TlsServerContext() = default;
TlsServerContext::TlsServerContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TlsServerContext::TlsServerContext(TlsServerContext&& other) noexcept = default;
TlsServerContext& TlsServerContext::operator=(TlsServerContext&& other) noexcept = default;

common::Result<TlsServerContext> TlsServerContext::create(const TlsConfig& config) {
    const common::Status validation = validateTlsServerConfig(config);
    if (!validation.isOk()) {
        return validation;
    }
    if (config.mode == TlsMode::Off) {
        return TlsServerContext();
    }
    initializeOpenSsl();
    SSL_CTX* context = SSL_CTX_new(TLS_server_method());
    if (context == nullptr) {
        return opensslStatus("TLS server context");
    }
    std::unique_ptr<Impl> impl = std::make_unique<Impl>(context);
    if (SSL_CTX_use_certificate_file(context, config.certFile.c_str(), SSL_FILETYPE_PEM) != 1) {
        return opensslStatus("TLS certificate load");
    }
    if (SSL_CTX_use_PrivateKey_file(context, config.keyFile.c_str(), SSL_FILETYPE_PEM) != 1) {
        return opensslStatus("TLS private key load");
    }
    if (SSL_CTX_check_private_key(context) != 1) {
        return opensslStatus("TLS private key check");
    }
    if (!config.caFile.empty() &&
        SSL_CTX_load_verify_locations(context, config.caFile.c_str(), nullptr) != 1) {
        return opensslStatus("TLS CA load");
    }
    return TlsServerContext(std::move(impl));
}

common::Result<TlsConnection> TlsServerContext::accept(UniqueFd fd) const {
    if (impl_ == nullptr || impl_->context == nullptr) {
        return TlsConnection::plain(std::move(fd));
    }
    SSL* ssl = SSL_new(impl_->context);
    if (ssl == nullptr) {
        return opensslStatus("TLS server connection");
    }
    std::unique_ptr<TlsConnection::Impl> connectionImpl =
        std::make_unique<TlsConnection::Impl>(ssl);
    if (SSL_set_fd(ssl, fd.get()) != 1) {
        return opensslStatus("TLS bind fd");
    }
    const int result = SSL_accept(ssl);
    if (result != 1) {
        return common::Status::runtimeError("TLS handshake failed");
    }
    return TlsConnection(std::move(fd), std::move(connectionImpl));
}

struct TlsClientContext::Impl {
    explicit Impl(SSL_CTX* contextValue) : context(contextValue) {}
    ~Impl() {
        if (context != nullptr) {
            SSL_CTX_free(context);
        }
    }
    SSL_CTX* context = nullptr;
};

TlsClientContext::TlsClientContext() = default;
TlsClientContext::~TlsClientContext() = default;
TlsClientContext::TlsClientContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TlsClientContext::TlsClientContext(TlsClientContext&& other) noexcept = default;
TlsClientContext& TlsClientContext::operator=(TlsClientContext&& other) noexcept = default;

common::Result<TlsClientContext> TlsClientContext::create(const TlsConfig& config) {
    const common::Status validation = validateTlsClientConfig(config);
    if (!validation.isOk()) {
        return validation;
    }
    if (config.mode == TlsMode::Off) {
        return TlsClientContext();
    }
    initializeOpenSsl();
    SSL_CTX* context = SSL_CTX_new(TLS_client_method());
    if (context == nullptr) {
        return opensslStatus("TLS client context");
    }
    std::unique_ptr<Impl> impl = std::make_unique<Impl>(context);
    if (!config.caFile.empty()) {
        if (SSL_CTX_load_verify_locations(context, config.caFile.c_str(), nullptr) != 1) {
            return opensslStatus("TLS CA load");
        }
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
    } else {
        SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
    }
    return TlsClientContext(std::move(impl));
}

common::Result<TlsConnection> TlsClientContext::connect(UniqueFd fd,
                                                        const std::string& host) const {
    if (impl_ == nullptr || impl_->context == nullptr) {
        return TlsConnection::plain(std::move(fd));
    }
    SSL* ssl = SSL_new(impl_->context);
    if (ssl == nullptr) {
        return opensslStatus("TLS client connection");
    }
    std::unique_ptr<TlsConnection::Impl> connectionImpl =
        std::make_unique<TlsConnection::Impl>(ssl);
    if (!host.empty()) {
        (void)SSL_set_tlsext_host_name(ssl, host.c_str());
    }
    if (SSL_set_fd(ssl, fd.get()) != 1) {
        return opensslStatus("TLS bind fd");
    }
    const int result = SSL_connect(ssl);
    if (result != 1) {
        return common::Status::runtimeError("TLS handshake failed");
    }
    return TlsConnection(std::move(fd), std::move(connectionImpl));
}

}  // namespace gridflux::core::io
