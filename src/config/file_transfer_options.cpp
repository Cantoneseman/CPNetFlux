#include "gridflux/config/file_transfer_options.h"

#include <charconv>
#include <limits>
#include <string_view>

#include "gridflux/core/io/tls_socket.h"
#include "gridflux/core/metrics/event_log.h"

namespace gridflux::config {
namespace {

constexpr std::uint32_t kMaxConnections = 64;
constexpr std::uint32_t kMaxBufferSize = 16 * 1024 * 1024;
constexpr std::uint64_t kMaxManifestFlushIntervalChunks = 65536;
constexpr std::uint64_t kMaxChunkSize = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
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

common::Result<FileTransferOptions> parseFileTransferOptions(int argc, const char* const* argv,
                                                             FileTransferRole role) {
    FileTransferOptions options;
    options.host = role == FileTransferRole::Server ? "0.0.0.0" : "127.0.0.1";

    bool hasPath = false;
    bool hasFileIoQueueDepth = false;
    bool hasFileIoBatchSize = false;
    int index = 1;
    while (index < argc) {
        const std::string_view option(argv[index]);
        if (option == "--overwrite") {
            if (role != FileTransferRole::Server) {
                return common::Status::invalidArgument("--overwrite is server-only");
            }
            options.overwrite = true;
            index += 1;
            continue;
        }
        if (option == "--keep-partial") {
            if (role != FileTransferRole::Server) {
                return common::Status::invalidArgument("--keep-partial is server-only");
            }
            options.keepPartial = true;
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
            index += 2;
            continue;
        }

        if (option == "--port") {
            auto parsed = parseUnsigned(value, "--port");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > std::numeric_limits<std::uint16_t>::max()) {
                return common::Status::invalidArgument("--port must be in range 1..65535");
            }
            options.port = static_cast<std::uint16_t>(parsed.value());
            index += 2;
            continue;
        }

        if (option == "--connections") {
            auto parsed = parseUnsigned(value, "--connections");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxConnections) {
                return common::Status::invalidArgument("--connections must be in range 1..64");
            }
            options.connections = static_cast<std::uint32_t>(parsed.value());
            index += 2;
            continue;
        }

        if (option == "--buffer-size") {
            auto parsed = parseUnsigned(value, "--buffer-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxBufferSize) {
                return common::Status::invalidArgument(
                    "--buffer-size must be in range 1..16777216");
            }
            options.bufferSize = static_cast<std::uint32_t>(parsed.value());
            index += 2;
            continue;
        }

        if (option == "--data-final-status-timeout-seconds") {
            if (role != FileTransferRole::Client) {
                return common::Status::invalidArgument(
                    "--data-final-status-timeout-seconds is client-only");
            }
            auto parsed = parseUnsigned(value, "--data-final-status-timeout-seconds");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 ||
                parsed.value() > std::numeric_limits<std::uint32_t>::max()) {
                return common::Status::invalidArgument(
                    "--data-final-status-timeout-seconds must be in range 1..4294967295");
            }
            options.dataFinalStatusTimeoutSeconds =
                static_cast<std::uint32_t>(parsed.value());
            index += 2;
            continue;
        }

        if (option == "--checksum") {
            auto parsed = checksum::parseChecksumAlgorithm(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.checksumAlgorithm = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--checksum-backend") {
            auto parsed = checksum::parseChecksumBackend(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.checksumBackend = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--manifest-flush-interval-chunks") {
            if (role != FileTransferRole::Server) {
                return common::Status::invalidArgument(
                    "--manifest-flush-interval-chunks is server-only");
            }
            auto parsed = parseUnsigned(value, "--manifest-flush-interval-chunks");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxManifestFlushIntervalChunks) {
                return common::Status::invalidArgument(
                    "--manifest-flush-interval-chunks must be in range 1..65536");
            }
            options.manifestFlushIntervalChunks = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--manifest-flush-policy") {
            if (role != FileTransferRole::Server) {
                return common::Status::invalidArgument("--manifest-flush-policy is server-only");
            }
            auto parsed = core::session::parseManifestFlushPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.manifestFlushPolicy = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--final-verify-policy") {
            if (role != FileTransferRole::Server) {
                return common::Status::invalidArgument("--final-verify-policy is server-only");
            }
            auto parsed = core::session::parseFinalVerifyPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.finalVerifyPolicy = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--commit-sync-policy") {
            if (role != FileTransferRole::Server) {
                return common::Status::invalidArgument("--commit-sync-policy is server-only");
            }
            auto parsed = core::session::parseCommitSyncPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.commitSyncPolicy = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--preallocate") {
            if (role != FileTransferRole::Server) {
                return common::Status::invalidArgument("--preallocate is server-only");
            }
            auto parsed = storage::parsePreallocateMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.preallocateMode = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--file-io-backend") {
            auto parsed = storage::parseFileIoBackendKind(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.backend = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--file-io-buffer-size") {
            auto parsed = parseUnsigned(value, "--file-io-buffer-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() > kMaxFileIoBufferSize) {
                return common::Status::invalidArgument(
                    "--file-io-buffer-size must be in range 0..67108864");
            }
            options.fileIo.bufferSize = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--file-io-queue-depth") {
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
            index += 2;
            continue;
        }

        if (option == "--file-io-batch-size") {
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
            index += 2;
            continue;
        }

        if (option == "--file-io-advice") {
            auto parsed = storage::parseFileIoAdvice(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.advice = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--posix-write-strategy") {
            auto parsed = storage::parsePosixWriteStrategy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.posixWriteStrategy = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--event-log") {
            if (value.empty()) {
                return common::Status::invalidArgument("--event-log must not be empty");
            }
            options.eventLogPath = std::string(value);
            index += 2;
            continue;
        }

        if (option == "--data-tls-mode") {
            auto parsed = core::io::parseDataTlsMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.dataTlsMode = parsed.value();
            options.dataTls.mode = parsed.value() == core::io::DataTlsMode::Required
                                       ? core::io::TlsMode::Required
                                       : core::io::TlsMode::Off;
            index += 2;
            continue;
        }

        if (option == "--tls-ca-file") {
            if (value.empty()) {
                return common::Status::invalidArgument("--tls-ca-file must not be empty");
            }
            options.dataTls.caFile = std::string(value);
            index += 2;
            continue;
        }

        if (option == "--chunk-size") {
            if (role != FileTransferRole::Client) {
                return common::Status::invalidArgument("--chunk-size is client-only");
            }
            auto parsed = parseUnsigned(value, "--chunk-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxChunkSize) {
                return common::Status::invalidArgument(
                    "--chunk-size must be in range 1..1099511627776");
            }
            options.chunkSize = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--transfer-id") {
            if (role != FileTransferRole::Client) {
                return common::Status::invalidArgument("--transfer-id is client-only");
            }
            if (value.empty()) {
                return common::Status::invalidArgument("--transfer-id must not be empty");
            }
            options.transferId = std::string(value);
            index += 2;
            continue;
        }

        if (option == "--max-chunks") {
            if (role != FileTransferRole::Client) {
                return common::Status::invalidArgument("--max-chunks is client-only");
            }
            auto parsed = parseUnsigned(value, "--max-chunks");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0) {
                return common::Status::invalidArgument("--max-chunks must be greater than zero");
            }
            options.maxChunks = parsed.value();
            index += 2;
            continue;
        }

        if (option == "--corrupt-chunk") {
            if (role != FileTransferRole::Client) {
                return common::Status::invalidArgument("--corrupt-chunk is client-only");
            }
            auto parsed = parseUnsigned(value, "--corrupt-chunk");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.corruptChunk = parsed.value();
            options.hasCorruptChunk = true;
            index += 2;
            continue;
        }

        if (option == "--duplicate-corrupt-chunk") {
            if (role != FileTransferRole::Client) {
                return common::Status::invalidArgument("--duplicate-corrupt-chunk is client-only");
            }
            auto parsed = parseUnsigned(value, "--duplicate-corrupt-chunk");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.duplicateCorruptChunk = parsed.value();
            options.hasDuplicateCorruptChunk = true;
            index += 2;
            continue;
        }

        if (option == "--input") {
            if (role != FileTransferRole::Client) {
                return common::Status::invalidArgument("--input is client-only");
            }
            if (value.empty()) {
                return common::Status::invalidArgument("--input must not be empty");
            }
            options.path = std::string(value);
            hasPath = true;
            index += 2;
            continue;
        }

        if (option == "--output") {
            if (role != FileTransferRole::Server) {
                return common::Status::invalidArgument("--output is server-only");
            }
            if (value.empty()) {
                return common::Status::invalidArgument("--output must not be empty");
            }
            options.path = std::string(value);
            hasPath = true;
            index += 2;
            continue;
        }

        return common::Status::invalidArgument("unknown option: " + std::string(option));
    }

    if (!hasPath) {
        return common::Status::invalidArgument(
            role == FileTransferRole::Server ? "--output is required" : "--input is required");
    }
    if (options.resume && role == FileTransferRole::Client && options.transferId.empty()) {
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
    if (role == FileTransferRole::Server &&
        options.dataTlsMode == core::io::DataTlsMode::Required) {
        return common::Status::invalidArgument(
            "--data-tls-mode required on receive side is provided by gridflux-gridftp-server");
    }
    const common::Status dataTlsStatus =
        core::io::validateDataTlsClientConfig(options.dataTlsMode, options.dataTls);
    if (!dataTlsStatus.isOk()) {
        return dataTlsStatus;
    }

    return options;
}

std::string fileTransferUsage(const char* programName, FileTransferRole role) {
    if (role == FileTransferRole::Server) {
        return std::string("Usage: ") + programName +
               " --host <bind-ip> --port <port> --output <path> --connections <N> "
           "--buffer-size <bytes> [--checksum <crc32c|none>] "
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
               "[--event-log <path>] [--data-tls-mode off|required] [--tls-ca-file <path>] "
               "[--overwrite] "
               "[--keep-partial] [--resume]";
    }

    return std::string("Usage: ") + programName +
           " --host <server-ip> --port <port> --input <path> --connections <N> "
           "--chunk-size <bytes> --buffer-size <bytes> [--checksum <crc32c|none>] "
           "[--checksum-backend <auto|software|hardware>] "
           "[--file-io-backend <posix|io_uring>] [--file-io-buffer-size <bytes>] "
           "[--file-io-queue-depth <N>] [--file-io-batch-size <N>] "
           "[--file-io-advice <off|sequential|noreuse|dontneed|sequential_dontneed>] "
           "[--posix-write-strategy <auto|direct|coalesced>] "
           "[--event-log <path>] [--data-tls-mode off|required] [--tls-ca-file <path>] "
           "[--transfer-id <id>] [--resume] [--max-chunks <N>] "
           "[--data-final-status-timeout-seconds <N>] "
           "[--corrupt-chunk <chunk-id>] [--duplicate-corrupt-chunk <chunk-id>]";
}

}  // namespace gridflux::config
