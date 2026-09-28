#include "cpnetflux/config/tree_transfer_options.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

#include "cpnetflux/core/metrics/event_log.h"
#include "cpnetflux/core/tree/tree_scan.h"
#include "cpnetflux/protocol/control/control_auth.h"

namespace cpnetflux::config {
namespace {

constexpr std::uint32_t kMaxConnections = 64;
constexpr std::uint32_t kMaxFileParallelism = 16;
constexpr std::uint32_t kMaxBufferSize = 16 * 1024 * 1024;
constexpr std::uint64_t kMaxChunkSize = 1024ULL * 1024ULL * 1024ULL * 1024ULL;

common::Result<std::uint64_t> parseUnsigned(std::string_view value, std::string_view name) {
    if (value.empty()) {
        return common::Status::invalidArgument(std::string(name) + " must not be empty");
    }
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end) {
        return common::Status::invalidArgument(std::string(name) + " must be a decimal integer");
    }
    return parsed;
}

common::Result<double> parsePositiveDouble(std::string_view value, std::string_view name) {
    if (value.empty()) {
        return common::Status::invalidArgument(std::string(name) + " must not be empty");
    }
    std::string text(value);
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return common::Status::invalidArgument(std::string(name) + " must be a decimal number");
    }
    if (parsed <= 0.0) {
        return common::Status::invalidArgument(std::string(name) + " must be greater than zero");
    }
    return parsed;
}

common::Status requireValue(int argc, int index, std::string_view option) {
    if (index + 1 >= argc) {
        return common::Status::invalidArgument(std::string(option) + " requires a value");
    }
    return common::Status::ok();
}

common::Status validateLocalDirectory(const std::string& path, std::string_view option) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return common::Status::invalidArgument(std::string(option) + " must exist");
    }
    if (!std::filesystem::is_directory(path, error) || error) {
        return common::Status::invalidArgument(std::string(option) + " must be a directory");
    }
    return common::Status::ok();
}

common::Status validateRemoteDirArgument(const std::string& path, std::string_view option) {
    if (path.empty()) {
        return common::Status::invalidArgument(std::string(option) + " must not be empty");
    }
    if (path == "/") {
        return common::Status::ok();
    }
    return core::tree::validateTreeRelativePath(path);
}

std::filesystem::path absoluteLexical(const std::string& path) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = std::filesystem::path(path);
    }
    return absolute.lexically_normal();
}

bool pathInsideOrEqual(const std::filesystem::path& candidate,
                       const std::filesystem::path& root) {
    auto candIt = candidate.begin();
    auto rootIt = root.begin();
    for (; rootIt != root.end(); ++rootIt, ++candIt) {
        if (candIt == candidate.end() || *candIt != *rootIt) {
            return false;
        }
    }
    return true;
}

common::Status validateSchedulerMetricsDir(const TreeTransferOptions& options,
                                           TreeTransferRole role) {
    if (options.schedulerMetricsDir.empty()) {
        return common::Status::ok();
    }
    const std::string& localRoot =
        role == TreeTransferRole::Upload ? options.sourceDir : options.destDir;
    const std::filesystem::path metrics = absoluteLexical(options.schedulerMetricsDir);
    const std::filesystem::path root = absoluteLexical(localRoot);
    if (pathInsideOrEqual(metrics, root)) {
        return common::Status::invalidArgument(
            "--scheduler-metrics-dir must be outside the local transfer root");
    }
    return common::Status::ok();
}

}  // namespace

common::Result<ControlReuseMode> parseControlReuseMode(std::string_view value) {
    if (value == "off") {
        return ControlReuseMode::Off;
    }
    if (value == "worker") {
        return ControlReuseMode::Worker;
    }
    return common::Status::invalidArgument("--control-reuse must be off or worker");
}

const char* controlReuseModeName(ControlReuseMode mode) noexcept {
    switch (mode) {
        case ControlReuseMode::Off:
            return "off";
        case ControlReuseMode::Worker:
            return "worker";
    }
    return "off";
}

common::Result<TreeSchedulerMode> parseTreeSchedulerMode(std::string_view value) {
    if (value == "off") {
        return TreeSchedulerMode::Off;
    }
    if (value == "global") {
        return TreeSchedulerMode::Global;
    }
    return common::Status::invalidArgument("--scheduler must be off or global");
}

const char* treeSchedulerModeName(TreeSchedulerMode mode) noexcept {
    switch (mode) {
        case TreeSchedulerMode::Off:
            return "off";
        case TreeSchedulerMode::Global:
            return "global";
    }
    return "off";
}

common::Result<TreeTransferOptions> parseTreeTransferOptions(int argc, const char* const* argv,
                                                             TreeTransferRole role) {
    TreeTransferOptions options;
    bool hasSourceDir = false;
    bool hasDestDir = false;
    int index = 1;
    while (index < argc) {
        const std::string_view option(argv[index]);
        if (option == "--resume") {
            options.resume = true;
            index += 1;
            continue;
        }

        const common::Status valueStatus = requireValue(argc, index, option);
        if (!valueStatus.isOk()) {
            return valueStatus;
        }
        const std::string_view value(argv[index + 1]);

        if (option == "--host") {
            if (value.empty()) {
                return common::Status::invalidArgument("--host must not be empty");
            }
            options.host = std::string(value);
        } else if (option == "--port") {
            auto parsed = parseUnsigned(value, "--port");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > std::numeric_limits<std::uint16_t>::max()) {
                return common::Status::invalidArgument("--port must be in range 1..65535");
            }
            options.port = static_cast<std::uint16_t>(parsed.value());
        } else if (option == "--source-dir") {
            if (value.empty()) {
                return common::Status::invalidArgument("--source-dir must not be empty");
            }
            options.sourceDir = std::string(value);
            hasSourceDir = true;
        } else if (option == "--dest-dir") {
            if (value.empty()) {
                return common::Status::invalidArgument("--dest-dir must not be empty");
            }
            options.destDir = std::string(value);
            hasDestDir = true;
        } else if (option == "--connections") {
            auto parsed = parseUnsigned(value, "--connections");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxConnections) {
                return common::Status::invalidArgument("--connections must be in range 1..64");
            }
            options.connections = static_cast<std::uint32_t>(parsed.value());
        } else if (option == "--file-parallelism") {
            auto parsed = parseUnsigned(value, "--file-parallelism");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxFileParallelism) {
                return common::Status::invalidArgument(
                    "--file-parallelism must be in range 1..16");
            }
            options.fileParallelism = static_cast<std::uint32_t>(parsed.value());
        } else if (option == "--control-reuse") {
            auto parsed = parseControlReuseMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.controlReuseMode = parsed.value();
        } else if (option == "--compression") {
            if (value == "off") {
                options.compressionMode = CompressionMode::Off;
            } else if (value == "auto") {
                options.compressionMode = CompressionMode::Auto;
            } else {
                return common::Status::invalidArgument("--compression must be off or auto");
            }
        } else if (option == "--chunk-size") {
            auto parsed = parseUnsigned(value, "--chunk-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxChunkSize) {
                return common::Status::invalidArgument(
                    "--chunk-size must be in range 1..1099511627776");
            }
            options.chunkSize = parsed.value();
        } else if (option == "--buffer-size") {
            auto parsed = parseUnsigned(value, "--buffer-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxBufferSize) {
                return common::Status::invalidArgument(
                    "--buffer-size must be in range 1..16777216");
            }
            options.bufferSize = static_cast<std::uint32_t>(parsed.value());
        } else if (option == "--checksum") {
            auto parsed = checksum::parseChecksumAlgorithm(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.checksumAlgorithm = parsed.value();
        } else if (option == "--checksum-backend") {
            auto parsed = checksum::parseChecksumBackend(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.checksumBackend = parsed.value();
        } else if (option == "--max-files") {
            auto parsed = parseUnsigned(value, "--max-files");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0) {
                return common::Status::invalidArgument("--max-files must be greater than zero");
            }
            options.maxFiles = parsed.value();
        } else if (option == "--auth-mode") {
            auto parsed = protocol::control::parseAuthMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.authMode = protocol::control::authModeName(parsed.value());
        } else if (option == "--auth-token-file") {
            if (value.empty()) {
                return common::Status::invalidArgument("--auth-token-file must not be empty");
            }
            options.authTokenFile = std::string(value);
        } else if (option == "--user") {
            if (value.empty()) {
                return common::Status::invalidArgument("--user must not be empty");
            }
            options.user = std::string(value);
        } else if (option == "--password") {
            if (value.empty()) {
                return common::Status::invalidArgument("--password must not be empty");
            }
            options.password = std::string(value);
        } else if (option == "--planner-preset") {
            if (value.empty()) {
                return common::Status::invalidArgument("--planner-preset must not be empty");
            }
            options.plannerPreset = std::string(value);
        } else if (option == "--json-summary" || option == "--summary-json") {
            if (value.empty()) {
                return common::Status::invalidArgument(std::string(option) + " must not be empty");
            }
            options.jsonSummaryPath = std::string(value);
        } else if (option == "--event-log") {
            if (value.empty()) {
                return common::Status::invalidArgument("--event-log must not be empty");
            }
            options.eventLogPath = std::string(value);
        } else if (option == "--phase-timing") {
            if (value != "on" && value != "off") {
                return common::Status::invalidArgument("--phase-timing must be on or off");
            }
            options.phaseTiming = value == "on";
        } else if (option == "--scheduler") {
            auto parsed = parseTreeSchedulerMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.schedulerMode = parsed.value();
        } else if (option == "--scheduler-policy") {
            auto parsed = core::scheduler::parseSchedulerPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.schedulerPolicy = parsed.value();
        } else if (option == "--scheduler-metrics-dir") {
            if (value.empty()) {
                return common::Status::invalidArgument("--scheduler-metrics-dir must not be empty");
            }
            options.schedulerMetricsDir = std::string(value);
        } else if (option == "--scheduler-link-id") {
            if (value.empty()) {
                return common::Status::invalidArgument("--scheduler-link-id must not be empty");
            }
            options.schedulerLinkId = std::string(value);
        } else if (option == "--scheduler-capacity-gbps") {
            auto parsed = parsePositiveDouble(value, "--scheduler-capacity-gbps");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.schedulerCapacityGbps = parsed.value();
        } else if (option == "--scheduler-workitem-min-bytes") {
            auto parsed = parseUnsigned(value, "--scheduler-workitem-min-bytes");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0) {
                return common::Status::invalidArgument(
                    "--scheduler-workitem-min-bytes must be greater than zero");
            }
            options.schedulerWorkItemMinBytes = parsed.value();
        } else if (option == "--scheduler-workitem-max-bytes") {
            auto parsed = parseUnsigned(value, "--scheduler-workitem-max-bytes");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0) {
                return common::Status::invalidArgument(
                    "--scheduler-workitem-max-bytes must be greater than zero");
            }
            options.schedulerWorkItemMaxBytes = parsed.value();
        } else if (option == "--scheduler-default-rtt-ms") {
            auto parsed = parseUnsigned(value, "--scheduler-default-rtt-ms");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0) {
                return common::Status::invalidArgument(
                    "--scheduler-default-rtt-ms must be greater than zero");
            }
            options.schedulerDefaultRttMs = parsed.value();
        } else if (option == "--scheduler-min-compress-gbps") {
            auto parsed = parsePositiveDouble(value, "--scheduler-min-compress-gbps");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.schedulerMinCompressGbps = parsed.value();
        } else if (option == "--tls-mode") {
            auto parsed = core::io::parseTlsMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.tls.mode = parsed.value();
        } else if (option == "--tls-ca-file") {
            if (value.empty()) {
                return common::Status::invalidArgument("--tls-ca-file must not be empty");
            }
            options.tls.caFile = std::string(value);
        } else if (option == "--data-tls-mode") {
            auto parsed = core::io::parseDataTlsMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.dataTlsMode = parsed.value();
        } else {
            return common::Status::invalidArgument("unknown option: " + std::string(option));
        }
        index += 2;
    }

    if (!hasSourceDir) {
        return common::Status::invalidArgument("--source-dir is required");
    }
    if (!hasDestDir) {
        return common::Status::invalidArgument("--dest-dir is required");
    }
    if (options.authMode == "token") {
        if (options.authTokenFile.empty()) {
            return common::Status::invalidArgument("--auth-token-file is required in token mode");
        }
        auto token = protocol::control::loadTokenFile(options.authTokenFile);
        if (!token.isOk()) {
            return token.status();
        }
    }
    if (options.schedulerWorkItemMinBytes > options.schedulerWorkItemMaxBytes) {
        return common::Status::invalidArgument(
            "--scheduler-workitem-min-bytes must be <= --scheduler-workitem-max-bytes");
    }
    if (role == TreeTransferRole::Upload) {
        const common::Status sourceStatus = validateLocalDirectory(options.sourceDir, "--source-dir");
        if (!sourceStatus.isOk()) {
            return sourceStatus;
        }
        const common::Status destStatus = validateRemoteDirArgument(options.destDir, "--dest-dir");
        if (!destStatus.isOk()) {
            return destStatus;
        }
    } else {
        const common::Status sourceStatus = validateRemoteDirArgument(options.sourceDir, "--source-dir");
        if (!sourceStatus.isOk()) {
            return sourceStatus;
        }
        if (!options.resume) {
            std::error_code error;
            std::filesystem::create_directories(options.destDir, error);
            if (error) {
                return common::Status::systemError("create destination directory failed: " +
                                                       error.message(),
                                                   error.value());
            }
        }
    }
    const common::Status schedulerMetricsStatus = validateSchedulerMetricsDir(options, role);
    if (!schedulerMetricsStatus.isOk()) {
        return schedulerMetricsStatus;
    }
    const common::Status eventLogStatus =
        core::metrics::validateEventLogPath(options.eventLogPath);
    if (!eventLogStatus.isOk()) {
        return eventLogStatus;
    }
    const common::Status tlsStatus = core::io::validateTlsClientConfig(options.tls);
    if (!tlsStatus.isOk()) {
        return tlsStatus;
    }
    const common::Status dataTlsStatus =
        core::io::validateDataTlsClientConfig(options.dataTlsMode, options.tls);
    if (!dataTlsStatus.isOk()) {
        return dataTlsStatus;
    }
    return options;
}

std::string treeTransferUsage(const char* programName, TreeTransferRole role) {
    const char* sourceText = role == TreeTransferRole::Upload ? "<local_dir>" : "<remote_dir>";
    const char* destText = role == TreeTransferRole::Upload ? "<remote_dir>" : "<local_dir>";
    return std::string("Usage: ") + programName +
           " --host <server-ip> --port <port> --source-dir " + sourceText + " --dest-dir " +
           destText +
           " [--connections <N>] [--file-parallelism <N>] [--chunk-size <bytes>] "
           "[--buffer-size <bytes>] [--checksum <crc32c|none>] "
           "[--checksum-backend <auto|software|hardware>] [--resume] [--max-files <N>] "
           "[--control-reuse off|worker] [--compression off|auto] [--planner-preset <name>] "
           "[--auth-mode anonymous|token] [--auth-token-file <path>] "
           "[--user <name>] [--password <password>] [--json-summary <path>] "
           "[--event-log <path>] [--phase-timing on|off] [--scheduler off|global] "
           "[--scheduler-policy fixed|adaptive] [--scheduler-metrics-dir <dir>] "
           "[--scheduler-link-id <id>] [--scheduler-capacity-gbps <float>] "
           "[--scheduler-workitem-min-bytes <N>] [--scheduler-workitem-max-bytes <N>] "
           "[--scheduler-default-rtt-ms <N>] [--scheduler-min-compress-gbps <float>] "
           "[--tls-mode off|required] [--tls-ca-file <path>] "
           "[--data-tls-mode off|required]";
}

}  // namespace cpnetflux::config
