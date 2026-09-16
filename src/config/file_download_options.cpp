#include "cpnetflux/config/file_download_options.h"

#include <charconv>
#include <limits>
#include <string_view>

#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/core/metrics/event_log.h"

namespace cpnetflux::config {
namespace {

constexpr std::uint32_t kMaxConnections = 64;
constexpr std::uint32_t kMaxBufferSize = 16 * 1024 * 1024;
constexpr std::uint64_t kMaxManifestFlushIntervalChunks = 65536;
constexpr std::uint64_t kMaxFileIoBufferSize = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxFileIoQueueDepth = 256;

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

common::Status requireValue(int argc, int index, std::string_view option) {
    if (index + 1 >= argc) {
        return common::Status::invalidArgument(std::string(option) + " requires a value");
    }
    return common::Status::ok();
}

}  // namespace

common::Result<FileDownloadOptions> parseFileDownloadOptions(int argc, const char* const* argv) {
    FileDownloadOptions options;
    bool hasOutput = false;
    bool hasTransferId = false;
    bool hasFileIoQueueDepth = false;
    bool hasFileIoBatchSize = false;

    int index = 1;
    while (index < argc) {
        const std::string_view option(argv[index]);
        if (option == "--overwrite") {
            options.overwrite = true;
            index += 1;
            continue;
        }
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
        } else if (option == "--connections") {
            auto parsed = parseUnsigned(value, "--connections");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxConnections) {
                return common::Status::invalidArgument("--connections must be in range 1..64");
            }
            options.connections = static_cast<std::uint32_t>(parsed.value());
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
        } else if (option == "--manifest-flush-interval-chunks") {
            auto parsed = parseUnsigned(value, "--manifest-flush-interval-chunks");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxManifestFlushIntervalChunks) {
                return common::Status::invalidArgument(
                    "--manifest-flush-interval-chunks must be in range 1..65536");
            }
            options.manifestFlushIntervalChunks = parsed.value();
        } else if (option == "--manifest-flush-policy") {
            auto parsed = core::session::parseManifestFlushPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.manifestFlushPolicy = parsed.value();
        } else if (option == "--final-verify-policy") {
            auto parsed = core::session::parseFinalVerifyPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.finalVerifyPolicy = parsed.value();
        } else if (option == "--commit-sync-policy") {
            auto parsed = core::session::parseCommitSyncPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.commitSyncPolicy = parsed.value();
        } else if (option == "--preallocate") {
            auto parsed = storage::parsePreallocateMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.preallocateMode = parsed.value();
        } else if (option == "--file-io-backend") {
            auto parsed = storage::parseFileIoBackendKind(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.backend = parsed.value();
        } else if (option == "--file-io-buffer-size") {
            auto parsed = parseUnsigned(value, "--file-io-buffer-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() > kMaxFileIoBufferSize) {
                return common::Status::invalidArgument(
                    "--file-io-buffer-size must be in range 0..67108864");
            }
            options.fileIo.bufferSize = parsed.value();
        } else if (option == "--file-io-queue-depth") {
            auto parsed = parseUnsigned(value, "--file-io-queue-depth");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxFileIoQueueDepth) {
                return common::Status::invalidArgument(
                    "--file-io-queue-depth must be in range 1..256");
            }
            options.fileIo.queueDepth = parsed.value();
            hasFileIoQueueDepth = true;
        } else if (option == "--file-io-batch-size") {
            auto parsed = parseUnsigned(value, "--file-io-batch-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxFileIoQueueDepth) {
                return common::Status::invalidArgument(
                    "--file-io-batch-size must be in range 1..256");
            }
            options.fileIo.batchSize = parsed.value();
            hasFileIoBatchSize = true;
        } else if (option == "--file-io-advice") {
            auto parsed = storage::parseFileIoAdvice(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.advice = parsed.value();
        } else if (option == "--posix-write-strategy") {
            auto parsed = storage::parsePosixWriteStrategy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.posixWriteStrategy = parsed.value();
        } else if (option == "--event-log") {
            if (value.empty()) {
                return common::Status::invalidArgument("--event-log must not be empty");
            }
            options.eventLogPath = std::string(value);
        } else if (option == "--data-tls-mode") {
            auto parsed = core::io::parseDataTlsMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.dataTlsMode = parsed.value();
            options.dataTls.mode = parsed.value() == core::io::DataTlsMode::Required
                                       ? core::io::TlsMode::Required
                                       : core::io::TlsMode::Off;
        } else if (option == "--tls-ca-file") {
            if (value.empty()) {
                return common::Status::invalidArgument("--tls-ca-file must not be empty");
            }
            options.dataTls.caFile = std::string(value);
        } else if (option == "--transfer-id") {
            if (value.empty()) {
                return common::Status::invalidArgument("--transfer-id must not be empty");
            }
            options.transferId = std::string(value);
            hasTransferId = true;
        } else if (option == "--max-chunks") {
            auto parsed = parseUnsigned(value, "--max-chunks");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0) {
                return common::Status::invalidArgument("--max-chunks must be greater than zero");
            }
            options.maxChunks = parsed.value();
        } else if (option == "--output") {
            if (value.empty()) {
                return common::Status::invalidArgument("--output must not be empty");
            }
            options.path = std::string(value);
            hasOutput = true;
        } else {
            return common::Status::invalidArgument("unknown option: " + std::string(option));
        }
        index += 2;
    }

    if (!hasOutput) {
        return common::Status::invalidArgument("--output is required");
    }
    if (!hasTransferId) {
        return common::Status::invalidArgument("--transfer-id is required");
    }
    if (!checkpoint::isValidTransferId(options.transferId)) {
        return common::Status::invalidArgument("invalid transfer_id");
    }
    if (options.resume && !hasTransferId) {
        return common::Status::invalidArgument("--resume requires --transfer-id");
    }
    if (hasFileIoQueueDepth && !hasFileIoBatchSize) {
        options.fileIo.batchSize = options.fileIo.queueDepth;
    }
    const common::Status fileIoStatus = storage::validateFileIoConfig(options.fileIo);
    if (!fileIoStatus.isOk()) {
        return fileIoStatus;
    }
    const common::Status eventLogStatus =
        core::metrics::validateEventLogPath(options.eventLogPath);
    if (!eventLogStatus.isOk()) {
        return eventLogStatus;
    }
    const common::Status dataTlsStatus =
        core::io::validateDataTlsClientConfig(options.dataTlsMode, options.dataTls);
    if (!dataTlsStatus.isOk()) {
        return dataTlsStatus;
    }
    return options;
}

std::string fileDownloadUsage(const char* programName) {
    return std::string("Usage: ") + programName +
           " --host <server-ip> --port <port> --output <path> --connections <N> "
           "--buffer-size <bytes> --transfer-id <id> [--checksum <crc32c|none>] "
           "[--checksum-backend <auto|software|hardware>] "
           "[--manifest-flush-policy <every_n_chunks|final_only>] "
           "[--manifest-flush-interval-chunks <N>] "
           "[--final-verify-policy <full|verified_chunks>] "
           "[--commit-sync-policy <none|fsync_file|fsync_file_and_dir>] "
           "[--preallocate <off|full>] "
           "[--file-io-backend <posix|io_uring>] [--file-io-buffer-size <bytes>] "
           "[--file-io-queue-depth <N>] [--file-io-batch-size <N>] "
           "[--file-io-advice <off|sequential|noreuse|dontneed|sequential_dontneed>] "
           "[--posix-write-strategy <auto|direct|coalesced>] "
           "[--event-log <path>] [--data-tls-mode <off|required>] [--tls-ca-file <path>] "
           "[--overwrite] [--resume] "
           "[--max-chunks <N>]";
}

}  // namespace cpnetflux::config
