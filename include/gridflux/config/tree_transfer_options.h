#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "gridflux/checksum/checksum.h"
#include "gridflux/common/status.h"
#include "gridflux/core/io/tls_socket.h"

namespace gridflux::config {

enum class TreeTransferRole {
    Upload,
    Download,
};

enum class ControlReuseMode {
    Off,
    Worker,
};

[[nodiscard]] common::Result<ControlReuseMode> parseControlReuseMode(std::string_view value);
[[nodiscard]] const char* controlReuseModeName(ControlReuseMode mode) noexcept;

struct TreeTransferOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2121;
    std::string sourceDir;
    std::string destDir;
    std::uint32_t connections = 1;
    std::uint32_t fileParallelism = 1;
    ControlReuseMode controlReuseMode = ControlReuseMode::Off;
    std::uint64_t chunkSize = 1048576;
    std::uint32_t dataFinalStatusTimeoutSeconds = 60;
    std::uint32_t bufferSize = 65536;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto;
    bool resume = false;
    std::uint64_t maxFiles = 0;
    std::string authMode = "anonymous";
    std::string authTokenFile;
    std::string user = "gridflux";
    std::string password = "gridflux";
    std::string plannerPreset;
    std::string jsonSummaryPath;
    std::string eventLogPath;
    core::io::TlsConfig tls;
    core::io::DataTlsMode dataTlsMode = core::io::DataTlsMode::Off;
};

[[nodiscard]] common::Result<TreeTransferOptions> parseTreeTransferOptions(
    int argc, const char* const* argv, TreeTransferRole role);
[[nodiscard]] std::string treeTransferUsage(const char* programName, TreeTransferRole role);

}  // namespace gridflux::config
