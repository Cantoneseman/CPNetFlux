#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/hot_path_compression.h"
#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/core/scheduler/feedback_controller.h"

namespace cpnetflux::config {

enum class TreeTransferRole {
    Upload,
    Download,
};

enum class ControlReuseMode {
    Off,
    Worker,
};

enum class TreeSchedulerMode {
    Off,
    Global,
};

enum class CompressionMode {
    Off,
    Auto,
};

[[nodiscard]] common::Result<ControlReuseMode> parseControlReuseMode(std::string_view value);
[[nodiscard]] const char* controlReuseModeName(ControlReuseMode mode) noexcept;
[[nodiscard]] common::Result<TreeSchedulerMode> parseTreeSchedulerMode(std::string_view value);
[[nodiscard]] const char* treeSchedulerModeName(TreeSchedulerMode mode) noexcept;

struct TreeTransferOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2121;
    std::string sourceDir;
    std::string destDir;
    std::uint32_t connections = 1;
    std::uint32_t fileParallelism = 1;
    ControlReuseMode controlReuseMode = ControlReuseMode::Off;
    std::uint64_t chunkSize = 1048576;
    std::uint32_t bufferSize = 65536;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto;
    bool resume = false;
    std::uint64_t maxFiles = 0;
    std::string authMode = "anonymous";
    std::string authTokenFile;
    std::string user = "cpnetflux";
    std::string password = "cpnetflux";
    std::string plannerPreset;
    std::string jsonSummaryPath;
    std::string eventLogPath;
    TreeSchedulerMode schedulerMode = TreeSchedulerMode::Off;
    core::scheduler::SchedulerPolicy schedulerPolicy =
        core::scheduler::SchedulerPolicy::Fixed;
    std::string schedulerMetricsDir;
    std::string schedulerLinkId = "link0";
    double schedulerCapacityGbps = 0.01;
    std::uint64_t schedulerWorkItemMinBytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t schedulerWorkItemMaxBytes = 256ULL * 1024ULL * 1024ULL;
    std::uint64_t schedulerDefaultRttMs = 10;
    double schedulerMinCompressGbps = 1.0;
    CompressionMode compressionMode = CompressionMode::Off;
    core::io::HotPathCompressionOptions hotPathCompression;
    core::io::TlsConfig tls;
    core::io::DataTlsMode dataTlsMode = core::io::DataTlsMode::Off;
};

[[nodiscard]] common::Result<TreeTransferOptions> parseTreeTransferOptions(
    int argc, const char* const* argv, TreeTransferRole role);
[[nodiscard]] std::string treeTransferUsage(const char* programName, TreeTransferRole role);

}  // namespace cpnetflux::config
