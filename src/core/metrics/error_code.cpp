#include "gridflux/core/metrics/error_code.h"

#include <algorithm>
#include <cctype>

namespace gridflux::core::metrics {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

const char* errorCodeName(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:
            return "ok";
        case ErrorCode::AuthRequired:
            return "auth_required";
        case ErrorCode::AuthFailed:
            return "auth_failed";
        case ErrorCode::TlsRequired:
            return "tls_required";
        case ErrorCode::TlsFailed:
            return "tls_failed";
        case ErrorCode::DataTlsRequired:
            return "data_tls_required";
        case ErrorCode::DataTlsFailed:
            return "data_tls_failed";
        case ErrorCode::PathRejected:
            return "path_rejected";
        case ErrorCode::ManifestCorrupt:
            return "manifest_corrupt";
        case ErrorCode::ChecksumMismatch:
            return "checksum_mismatch";
        case ErrorCode::ChangedFile:
            return "changed_file";
        case ErrorCode::RemoteSyncFailed:
            return "remote_sync_failed";
        case ErrorCode::IoError:
            return "io_error";
        case ErrorCode::ProtocolError:
            return "protocol_error";
        case ErrorCode::ConfigError:
            return "config_error";
        case ErrorCode::UnknownError:
            return "unknown_error";
    }
    return "unknown_error";
}

ErrorCode classifyMessage(const std::string& message) {
    const std::string text = lower(message);
    if (text.empty()) {
        return ErrorCode::Ok;
    }
    if (contains(text, "please login") || contains(text, "auth required")) {
        return ErrorCode::AuthRequired;
    }
    if (contains(text, "login incorrect") || contains(text, "invalid user") ||
        contains(text, "pass rejected") || contains(text, "user rejected") ||
        contains(text, "auth failed") || contains(text, "token")) {
        return ErrorCode::AuthFailed;
    }
    if (contains(text, "tls") && contains(text, "required")) {
        if (contains(text, "data")) {
            return ErrorCode::DataTlsRequired;
        }
        return ErrorCode::TlsRequired;
    }
    if (contains(text, "data") && (contains(text, "tls") || contains(text, "ssl"))) {
        return ErrorCode::DataTlsFailed;
    }
    if (contains(text, "tls") || contains(text, "ssl") || contains(text, "certificate") ||
        contains(text, "private key")) {
        return ErrorCode::TlsFailed;
    }
    if (contains(text, "path") &&
        (contains(text, "reject") || contains(text, "escape") || contains(text, "relative") ||
         contains(text, "outside") || contains(text, "directory") || contains(text, "file"))) {
        return ErrorCode::PathRejected;
    }
    if (contains(text, "manifest") &&
        (contains(text, "corrupt") || contains(text, "checksum") || contains(text, "invalid") ||
         contains(text, "required") || contains(text, "modified"))) {
        return ErrorCode::ManifestCorrupt;
    }
    if (contains(text, "checksum") && (contains(text, "mismatch") || contains(text, "failed"))) {
        return ErrorCode::ChecksumMismatch;
    }
    if (contains(text, "changed")) {
        return ErrorCode::ChangedFile;
    }
    if (contains(text, "remote") && (contains(text, "sync") || contains(text, "artifact"))) {
        return ErrorCode::RemoteSyncFailed;
    }
    if (contains(text, "protocol") || contains(text, "frame") || contains(text, "unexpected") ||
        contains(text, "rejected") || contains(text, "closed connection")) {
        return ErrorCode::ProtocolError;
    }
    if (contains(text, "unknown option") || contains(text, "requires a value") ||
        contains(text, "must be") || contains(text, "invalid")) {
        return ErrorCode::ConfigError;
    }
    if (contains(text, "io") || contains(text, "read") || contains(text, "write") ||
        contains(text, "open") || contains(text, "stat") || contains(text, "rename") ||
        contains(text, "connect") || contains(text, "recv") || contains(text, "send") ||
        contains(text, "socket") || contains(text, "filesystem") || contains(text, "directory")) {
        return ErrorCode::IoError;
    }
    return ErrorCode::UnknownError;
}

ErrorCode classifyStatus(const common::Status& status) {
    if (status.isOk()) {
        return ErrorCode::Ok;
    }
    if (status.code() == common::StatusCode::SystemError) {
        return ErrorCode::IoError;
    }
    return classifyMessage(status.message());
}

}  // namespace gridflux::core::metrics
