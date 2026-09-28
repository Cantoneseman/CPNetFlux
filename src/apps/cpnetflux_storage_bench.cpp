#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/storage/file_io.h"
#include "cpnetflux/storage/posix_file.h"
#include "cpnetflux/storage/preallocate_mode.h"

namespace {

enum class BenchMode {
    Write,
    Read,
    Rewrite,
    All,
};

cpnetflux::common::Result<std::uint64_t> parseUnsigned(std::string_view value,
                                                      std::string_view name) {
    if (value.empty()) {
        return cpnetflux::common::Status::invalidArgument(std::string(name) + " must not be empty");
    }
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end) {
        return cpnetflux::common::Status::invalidArgument(std::string(name) +
                                                         " must be a decimal integer");
    }
    return parsed;
}

cpnetflux::common::Result<BenchMode> parseMode(std::string_view value) {
    if (value == "write") {
        return BenchMode::Write;
    }
    if (value == "read") {
        return BenchMode::Read;
    }
    if (value == "rewrite") {
        return BenchMode::Rewrite;
    }
    if (value == "all") {
        return BenchMode::All;
    }
    return cpnetflux::common::Status::invalidArgument("mode must be write, read, rewrite, or all");
}

std::string_view modeName(BenchMode mode) {
    switch (mode) {
        case BenchMode::Write:
            return "write";
        case BenchMode::Read:
            return "read";
        case BenchMode::Rewrite:
            return "rewrite";
        case BenchMode::All:
            return "all";
    }
    return "all";
}

void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " --path <file> --mode <write|read|rewrite|all> --bytes <N> "
                 "--buffer-size <N> --iterations <N> --preallocate <off|full> "
                 "[--file-io-backend <posix|io_uring>] "
                 "[--file-io-buffer-size <N>] "
                 "[--file-io-queue-depth <N>] [--file-io-batch-size <N>] "
                 "[--file-io-advice <off|sequential|noreuse|dontneed|sequential_dontneed>] "
                 "[--posix-write-strategy <auto|direct|coalesced>] "
                 "[--keep-file]\n";
}

struct Options {
    std::string path;
    BenchMode mode = BenchMode::All;
    std::uint64_t bytes = 1024ULL * 1024ULL * 1024ULL;
    std::uint64_t bufferSize = 1024ULL * 1024ULL;
    std::uint64_t iterations = 1;
    cpnetflux::storage::PreallocateMode preallocateMode = cpnetflux::storage::PreallocateMode::Off;
    cpnetflux::storage::FileIoBackendKind fileIoBackend = cpnetflux::storage::FileIoBackendKind::Posix;
    std::uint64_t fileIoBufferSize = 0;
    std::uint64_t fileIoQueueDepth = 1;
    std::uint64_t fileIoBatchSize = 1;
    cpnetflux::storage::FileIoAdvice fileIoAdvice = cpnetflux::storage::FileIoAdvice::Off;
    cpnetflux::storage::PosixWriteStrategy posixWriteStrategy =
        cpnetflux::storage::PosixWriteStrategy::Auto;
    bool keepFile = false;
    bool hasFileIoQueueDepth = false;
    bool hasFileIoBatchSize = false;
};

cpnetflux::common::Result<Options> parseOptions(int argc, char** argv) {
    Options options;
    bool hasPath = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "-h" || option == "--help") {
            usage(argv[0]);
            std::exit(0);
        }
        if (option == "--keep-file") {
            options.keepFile = true;
            continue;
        }
        if (index + 1 >= argc) {
            return cpnetflux::common::Status::invalidArgument(std::string(option) +
                                                             " requires a value");
        }
        const std::string_view value(argv[++index]);
        if (option == "--path") {
            if (value.empty()) {
                return cpnetflux::common::Status::invalidArgument("--path must not be empty");
            }
            options.path = std::string(value);
            hasPath = true;
        } else if (option == "--mode") {
            auto parsed = parseMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.mode = parsed.value();
        } else if (option == "--bytes") {
            auto parsed = parseUnsigned(value, "--bytes");
            if (!parsed.isOk() || parsed.value() == 0) {
                return parsed.isOk()
                           ? cpnetflux::common::Status::invalidArgument(
                                 "--bytes must be greater than zero")
                           : parsed.status();
            }
            options.bytes = parsed.value();
        } else if (option == "--buffer-size") {
            auto parsed = parseUnsigned(value, "--buffer-size");
            if (!parsed.isOk() || parsed.value() == 0) {
                return parsed.isOk()
                           ? cpnetflux::common::Status::invalidArgument(
                                 "--buffer-size must be greater than zero")
                           : parsed.status();
            }
            options.bufferSize = parsed.value();
        } else if (option == "--iterations") {
            auto parsed = parseUnsigned(value, "--iterations");
            if (!parsed.isOk() || parsed.value() == 0) {
                return parsed.isOk()
                           ? cpnetflux::common::Status::invalidArgument(
                                 "--iterations must be greater than zero")
                           : parsed.status();
            }
            options.iterations = parsed.value();
        } else if (option == "--preallocate") {
            auto parsed = cpnetflux::storage::parsePreallocateMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.preallocateMode = parsed.value();
        } else if (option == "--file-io-backend") {
            auto parsed = cpnetflux::storage::parseFileIoBackendKind(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIoBackend = parsed.value();
        } else if (option == "--file-io-buffer-size") {
            auto parsed = parseUnsigned(value, "--file-io-buffer-size");
            if (!parsed.isOk() || parsed.value() > 64ULL * 1024ULL * 1024ULL) {
                return parsed.isOk()
                           ? cpnetflux::common::Status::invalidArgument(
                                 "--file-io-buffer-size must be in range 0..67108864")
                           : parsed.status();
            }
            options.fileIoBufferSize = parsed.value();
        } else if (option == "--file-io-queue-depth") {
            auto parsed = parseUnsigned(value, "--file-io-queue-depth");
            if (!parsed.isOk() || parsed.value() == 0 || parsed.value() > 256) {
                return parsed.isOk()
                           ? cpnetflux::common::Status::invalidArgument(
                                 "--file-io-queue-depth must be in range 1..256")
                           : parsed.status();
            }
            options.fileIoQueueDepth = parsed.value();
            options.hasFileIoQueueDepth = true;
        } else if (option == "--file-io-batch-size") {
            auto parsed = parseUnsigned(value, "--file-io-batch-size");
            if (!parsed.isOk() || parsed.value() == 0 || parsed.value() > 256) {
                return parsed.isOk()
                           ? cpnetflux::common::Status::invalidArgument(
                                 "--file-io-batch-size must be in range 1..256")
                           : parsed.status();
            }
            options.fileIoBatchSize = parsed.value();
            options.hasFileIoBatchSize = true;
        } else if (option == "--file-io-advice") {
            auto parsed = cpnetflux::storage::parseFileIoAdvice(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIoAdvice = parsed.value();
        } else if (option == "--posix-write-strategy") {
            auto parsed = cpnetflux::storage::parsePosixWriteStrategy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.posixWriteStrategy = parsed.value();
        } else {
            return cpnetflux::common::Status::invalidArgument("unknown option: " +
                                                             std::string(option));
        }
    }
    if (!hasPath) {
        return cpnetflux::common::Status::invalidArgument("--path is required");
    }
    if (options.hasFileIoQueueDepth && !options.hasFileIoBatchSize) {
        options.fileIoBatchSize = options.fileIoQueueDepth;
    }
    cpnetflux::storage::FileIoConfig fileIoConfig;
    fileIoConfig.backend = options.fileIoBackend;
    fileIoConfig.bufferSize = options.fileIoBufferSize;
    fileIoConfig.queueDepth = options.fileIoQueueDepth;
    fileIoConfig.batchSize = options.fileIoBatchSize;
    fileIoConfig.advice = options.fileIoAdvice;
    fileIoConfig.posixWriteStrategy = options.posixWriteStrategy;
    const cpnetflux::common::Status fileIoStatus =
        cpnetflux::storage::validateFileIoConfig(fileIoConfig);
    if (!fileIoStatus.isOk()) {
        return fileIoStatus;
    }
    return options;
}

std::vector<std::uint8_t> makeBuffer(std::uint64_t bufferSize) {
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(bufferSize));
    for (std::size_t index = 0; index < buffer.size(); ++index) {
        buffer[index] = static_cast<std::uint8_t>((index * 17U + 3U) % 251U);
    }
    return buffer;
}

cpnetflux::common::Status writeSequential(const Options& options,
                                         const std::vector<std::uint8_t>& buffer,
                                         bool truncateFirst,
                                         cpnetflux::storage::FileIoStats* stats) {
    auto fileResult = truncateFirst ? cpnetflux::storage::PosixFile::openWriteTruncate(options.path)
                                    : cpnetflux::storage::PosixFile::openReadWrite(options.path);
    if (!fileResult.isOk()) {
        return fileResult.status();
    }
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());
    if (options.preallocateMode == cpnetflux::storage::PreallocateMode::Full) {
        const cpnetflux::common::Status preallocateStatus = file.preallocate(options.bytes);
        if (!preallocateStatus.isOk()) {
            return preallocateStatus;
        }
    }
    const cpnetflux::common::Status adviceStatus =
        cpnetflux::storage::applyFileIoAdvice(file, options.fileIoAdvice, 0, options.bytes);
    if (!adviceStatus.isOk()) {
        return adviceStatus;
    }
    std::uint64_t completed = 0;
    cpnetflux::storage::FileIoConfig config;
    config.backend = options.fileIoBackend;
    config.bufferSize = options.fileIoBufferSize;
    config.queueDepth = options.fileIoQueueDepth;
    config.batchSize = options.fileIoBatchSize;
    config.advice = options.fileIoAdvice;
    config.posixWriteStrategy = options.posixWriteStrategy;
    cpnetflux::storage::FileIoContext context(config);
    cpnetflux::storage::BufferedFileWriter writer(file, context, stats);
    while (completed < options.bytes) {
        const std::size_t size = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(), options.bytes - completed));
        const cpnetflux::common::Status status = writer.write(completed, buffer.data(), size);
        if (!status.isOk()) {
            return status;
        }
        completed += size;
    }
    const cpnetflux::common::Status flushStatus = writer.flush();
    if (!flushStatus.isOk()) {
        return flushStatus;
    }
    return file.resize(options.bytes);
}

cpnetflux::common::Status readSequential(const Options& options, std::vector<std::uint8_t>* buffer,
                                        cpnetflux::storage::FileIoStats* stats) {
    auto fileResult = cpnetflux::storage::PosixFile::openReadOnly(options.path);
    if (!fileResult.isOk()) {
        return fileResult.status();
    }
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());
    const cpnetflux::common::Status adviceStatus =
        cpnetflux::storage::applyFileIoAdvice(file, options.fileIoAdvice, 0, options.bytes);
    if (!adviceStatus.isOk()) {
        return adviceStatus;
    }
    std::uint64_t completed = 0;
    std::uint8_t sink = 0;
    cpnetflux::storage::FileIoConfig config;
    config.backend = options.fileIoBackend;
    config.bufferSize = options.fileIoBufferSize;
    config.queueDepth = options.fileIoQueueDepth;
    config.batchSize = options.fileIoBatchSize;
    config.advice = options.fileIoAdvice;
    config.posixWriteStrategy = options.posixWriteStrategy;
    cpnetflux::storage::FileIoContext context(config);
    while (completed < options.bytes) {
        const std::size_t size = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer->size(), options.bytes - completed));
        const cpnetflux::common::Status status =
            cpnetflux::storage::readAtAll(file, completed, buffer->data(), size, context, stats);
        if (!status.isOk()) {
            return status;
        }
        sink ^= (*buffer)[0];
        completed += size;
    }
    if (sink == 255U) {
        std::cout << "";
    }
    return cpnetflux::common::Status::ok();
}

cpnetflux::common::Status runOperation(const Options& options, BenchMode operation,
                                      std::vector<std::uint8_t>* buffer,
                                      cpnetflux::storage::FileIoStats* stats) {
    switch (operation) {
        case BenchMode::Write:
            return writeSequential(options, *buffer, true, stats);
        case BenchMode::Read:
            return readSequential(options, buffer, stats);
        case BenchMode::Rewrite:
            return writeSequential(options, *buffer, false, stats);
        case BenchMode::All:
            break;
    }
    return cpnetflux::common::Status::invalidArgument("all is not a concrete operation");
}

void printResult(BenchMode operation, const Options& options, std::uint64_t iteration,
                 double seconds, const cpnetflux::common::Status& status,
                 const cpnetflux::storage::FileIoStats& stats, bool aggregate) {
    const double bytesProcessed = static_cast<double>(options.bytes) *
                                  static_cast<double>(aggregate ? options.iterations : 1);
    const double gbps = seconds > 0.0 ? (bytesProcessed * 8.0 / seconds) / 1'000'000'000.0 : 0.0;
    cpnetflux::storage::FileIoConfig fileIoConfig;
    fileIoConfig.backend = options.fileIoBackend;
    fileIoConfig.bufferSize = options.fileIoBufferSize;
    fileIoConfig.queueDepth = options.fileIoQueueDepth;
    fileIoConfig.batchSize = options.fileIoBatchSize;
    fileIoConfig.advice = options.fileIoAdvice;
    fileIoConfig.posixWriteStrategy = options.posixWriteStrategy;
    std::cout << "storage_bench operation=" << modeName(operation) << " bytes=" << options.bytes
              << " iterations=" << options.iterations << " buffer_size=" << options.bufferSize
              << " iteration=" << iteration << " aggregate=" << (aggregate ? "true" : "false")
              << " elapsed_seconds=" << seconds << " throughput_gbps=" << gbps
              << " preallocate="
              << cpnetflux::storage::preallocateModeName(options.preallocateMode)
              << " file_io_backend="
              << cpnetflux::storage::fileIoBackendName(options.fileIoBackend)
              << " file_io_buffer_size=" << options.fileIoBufferSize
              << " file_io_queue_depth=" << options.fileIoQueueDepth
              << " file_io_batch_size=" << options.fileIoBatchSize
              << " file_io_advice=" << cpnetflux::storage::fileIoAdviceName(options.fileIoAdvice)
              << " posix_write_strategy="
              << cpnetflux::storage::posixWriteStrategyName(options.posixWriteStrategy)
              << " posix_write_strategy_effective="
              << cpnetflux::storage::posixWriteStrategyName(
                     cpnetflux::storage::effectivePosixWriteStrategy(fileIoConfig))
              << " read_call_count=" << stats.readCalls()
              << " write_call_count=" << stats.writeCalls()
              << " write_syscall_count=" << stats.posixWriteSyscallCount()
              << " write_retry_count=" << stats.posixWriteRetryCount()
              << " write_short_count=" << stats.posixWriteShortCount()
              << " write_zero_count=" << stats.posixWriteZeroCount()
              << " write_total_bytes=" << stats.writeBytes()
              << " avg_read_bytes_per_call=" << stats.averageReadBytesPerCall()
              << " avg_write_bytes_per_call=" << stats.averageWriteBytesPerCall()
              << " write_avg_bytes_per_syscall="
              << stats.posixAverageBytesPerWriteSyscall()
              << " file_io_wait_seconds=" << stats.waitSeconds()
              << " io_uring_submit_count=" << stats.ioUringSubmitCount()
              << " io_uring_wait_count=" << stats.ioUringWaitCount()
              << " io_uring_completion_count=" << stats.ioUringCompletionCount()
              << " io_uring_sqe_count=" << stats.ioUringSqeCount()
              << " io_uring_partial_completion_count=" << stats.ioUringPartialCompletionCount()
              << " io_uring_retry_count=" << stats.ioUringRetryCount()
              << " io_uring_avg_bytes_per_sqe=" << stats.ioUringAverageBytesPerSqe()
              << " result=" << (status.isOk() ? "pass" : "fail");
    if (!status.isOk()) {
        std::cout << " error=" << status.message();
    }
    std::cout << '\n';
}

int runConcreteOperation(const Options& options, BenchMode operation,
                         std::vector<std::uint8_t>* buffer) {
    const auto aggregateStart = std::chrono::steady_clock::now();
    cpnetflux::common::Status status = cpnetflux::common::Status::ok();
    cpnetflux::storage::FileIoStats aggregateStats;
    for (std::uint64_t iteration = 0; iteration < options.iterations; ++iteration) {
        cpnetflux::storage::FileIoStats iterationStats;
        const auto start = std::chrono::steady_clock::now();
        status = runOperation(options, operation, buffer, &iterationStats);
        const auto end = std::chrono::steady_clock::now();
        printResult(operation, options, iteration, std::chrono::duration<double>(end - start).count(),
                    status, iterationStats, false);
        aggregateStats.mergeFrom(iterationStats);
        if (!status.isOk()) {
            break;
        }
    }
    const auto aggregateEnd = std::chrono::steady_clock::now();
    printResult(operation, options, options.iterations,
                std::chrono::duration<double>(aggregateEnd - aggregateStart).count(), status,
                aggregateStats, true);
    return status.isOk() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    auto parsed = parseOptions(argc, argv);
    if (!parsed.isOk()) {
        std::cerr << parsed.status().message() << '\n';
        usage(argv[0]);
        return 2;
    }
    Options options = parsed.value();
    std::vector<std::uint8_t> buffer = makeBuffer(options.bufferSize);

    int result = 0;
    if (options.mode == BenchMode::All) {
        result |= runConcreteOperation(options, BenchMode::Write, &buffer);
        result |= runConcreteOperation(options, BenchMode::Read, &buffer);
        result |= runConcreteOperation(options, BenchMode::Rewrite, &buffer);
    } else {
        result = runConcreteOperation(options, options.mode, &buffer);
    }

    if (!options.keepFile) {
        (void)cpnetflux::storage::PosixFile::removePath(options.path);
    }
    return result;
}
