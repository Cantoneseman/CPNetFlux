#pragma once

#include <string>

#include "gridflux/common/status.h"

namespace gridflux::core::metrics {

enum class ErrorCode {
    Ok,
    AuthRequired,
    AuthFailed,
    TlsRequired,
    TlsFailed,
    DataTlsRequired,
    DataTlsFailed,
    PathRejected,
    ManifestCorrupt,
    ChecksumMismatch,
    ChangedFile,
    RemoteSyncFailed,
    IoError,
    ProtocolError,
    ConfigError,
    UnknownError,
};

[[nodiscard]] const char* errorCodeName(ErrorCode code) noexcept;
[[nodiscard]] ErrorCode classifyStatus(const common::Status& status);
[[nodiscard]] ErrorCode classifyMessage(const std::string& message);

}  // namespace gridflux::core::metrics
