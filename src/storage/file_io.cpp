#include "cpnetflux/storage/file_io.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>

namespace cpnetflux::storage {
namespace {

std::uint64_t nanosFromDuration(std::chrono::steady_clock::duration duration) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

int toPosixAdvice(FileIoAdvice advice) noexcept {
    switch (advice) {
        case FileIoAdvice::Off:
            return POSIX_FADV_NORMAL;
        case FileIoAdvice::Sequential:
            return POSIX_FADV_SEQUENTIAL;
        case FileIoAdvice::Noreuse:
            return POSIX_FADV_NOREUSE;
        case FileIoAdvice::DontNeed:
            return POSIX_FADV_DONTNEED;
        case FileIoAdvice::SequentialDontNeed:
            return POSIX_FADV_SEQUENTIAL;
    }
    return POSIX_FADV_NORMAL;
}

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

}  // namespace

void FileIoStats::recordRead(std::uint64_t bytes,
                             std::chrono::steady_clock::duration duration) noexcept {
    readCalls_.fetch_add(1, std::memory_order_relaxed);
    readBytes_.fetch_add(bytes, std::memory_order_relaxed);
    waitNanos_.fetch_add(nanosFromDuration(duration), std::memory_order_relaxed);
}

void FileIoStats::recordWrite(std::uint64_t bytes,
                              std::chrono::steady_clock::duration duration) noexcept {
    writeCalls_.fetch_add(1, std::memory_order_relaxed);
    writeBytes_.fetch_add(bytes, std::memory_order_relaxed);
    waitNanos_.fetch_add(nanosFromDuration(duration), std::memory_order_relaxed);
}

void FileIoStats::recordIoUringSubmit(std::uint64_t sqes) noexcept {
    ioUringSubmitCount_.fetch_add(1, std::memory_order_relaxed);
    ioUringSqeCount_.fetch_add(sqes, std::memory_order_relaxed);
}

void FileIoStats::recordIoUringWait() noexcept {
    ioUringWaitCount_.fetch_add(1, std::memory_order_relaxed);
}

void FileIoStats::recordIoUringCompletion(std::uint64_t bytes) noexcept {
    ioUringCompletionCount_.fetch_add(1, std::memory_order_relaxed);
    ioUringCompletionBytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void FileIoStats::recordIoUringPartialCompletion() noexcept {
    ioUringPartialCompletionCount_.fetch_add(1, std::memory_order_relaxed);
}

void FileIoStats::recordIoUringRetry() noexcept {
    ioUringRetryCount_.fetch_add(1, std::memory_order_relaxed);
}

void FileIoStats::recordPosixWriteSyscall(std::uint64_t bytes) noexcept {
    posixWriteSyscallCount_.fetch_add(1, std::memory_order_relaxed);
    posixWriteSyscallBytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void FileIoStats::recordPosixWriteRetry() noexcept {
    posixWriteRetryCount_.fetch_add(1, std::memory_order_relaxed);
}

void FileIoStats::recordPosixWriteShort() noexcept {
    posixWriteShortCount_.fetch_add(1, std::memory_order_relaxed);
}

void FileIoStats::recordPosixWriteZero() noexcept {
    posixWriteZeroCount_.fetch_add(1, std::memory_order_relaxed);
}

void FileIoStats::mergeFrom(const FileIoStats& other) noexcept {
    readCalls_.fetch_add(other.readCalls(), std::memory_order_relaxed);
    writeCalls_.fetch_add(other.writeCalls(), std::memory_order_relaxed);
    readBytes_.fetch_add(other.readBytes(), std::memory_order_relaxed);
    writeBytes_.fetch_add(other.writeBytes(), std::memory_order_relaxed);
    waitNanos_.fetch_add(
        static_cast<std::uint64_t>(other.waitSeconds() * 1000000000.0),
        std::memory_order_relaxed);
    ioUringSubmitCount_.fetch_add(other.ioUringSubmitCount(), std::memory_order_relaxed);
    ioUringWaitCount_.fetch_add(other.ioUringWaitCount(), std::memory_order_relaxed);
    ioUringCompletionCount_.fetch_add(other.ioUringCompletionCount(), std::memory_order_relaxed);
    ioUringSqeCount_.fetch_add(other.ioUringSqeCount(), std::memory_order_relaxed);
    ioUringCompletionBytes_.fetch_add(other.ioUringCompletionBytes(), std::memory_order_relaxed);
    ioUringPartialCompletionCount_.fetch_add(other.ioUringPartialCompletionCount(),
                                             std::memory_order_relaxed);
    ioUringRetryCount_.fetch_add(other.ioUringRetryCount(), std::memory_order_relaxed);
    posixWriteSyscallCount_.fetch_add(other.posixWriteSyscallCount(), std::memory_order_relaxed);
    posixWriteSyscallBytes_.fetch_add(other.posixWriteSyscallBytes(), std::memory_order_relaxed);
    posixWriteRetryCount_.fetch_add(other.posixWriteRetryCount(), std::memory_order_relaxed);
    posixWriteShortCount_.fetch_add(other.posixWriteShortCount(), std::memory_order_relaxed);
    posixWriteZeroCount_.fetch_add(other.posixWriteZeroCount(), std::memory_order_relaxed);
}

FileIoContext::FileIoContext(FileIoConfig config) noexcept : config_(config) {}

const FileIoConfig& FileIoContext::config() const noexcept { return config_; }

FileIoBackendKind FileIoContext::backend() const noexcept { return config_.backend; }

common::Status FileIoContext::validateAvailable() const {
    const common::Status configStatus = validateFileIoConfig(config_);
    if (!configStatus.isOk()) {
        return configStatus;
    }
    if (fileIoBackendAvailable(config_.backend)) {
        return common::Status::ok();
    }
    return common::Status::runtimeError(std::string("file IO backend unavailable: ") +
                                        fileIoBackendName(config_.backend));
}

std::uint64_t FileIoStats::readCalls() const noexcept {
    return readCalls_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::writeCalls() const noexcept {
    return writeCalls_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::readBytes() const noexcept {
    return readBytes_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::writeBytes() const noexcept {
    return writeBytes_.load(std::memory_order_relaxed);
}

double FileIoStats::waitSeconds() const noexcept {
    return static_cast<double>(waitNanos_.load(std::memory_order_relaxed)) / 1000000000.0;
}

double FileIoStats::averageReadBytesPerCall() const noexcept {
    const std::uint64_t calls = readCalls();
    return calls == 0 ? 0.0 : static_cast<double>(readBytes()) / static_cast<double>(calls);
}

double FileIoStats::averageWriteBytesPerCall() const noexcept {
    const std::uint64_t calls = writeCalls();
    return calls == 0 ? 0.0 : static_cast<double>(writeBytes()) / static_cast<double>(calls);
}

std::uint64_t FileIoStats::ioUringSubmitCount() const noexcept {
    return ioUringSubmitCount_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::ioUringWaitCount() const noexcept {
    return ioUringWaitCount_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::ioUringCompletionCount() const noexcept {
    return ioUringCompletionCount_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::ioUringSqeCount() const noexcept {
    return ioUringSqeCount_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::ioUringCompletionBytes() const noexcept {
    return ioUringCompletionBytes_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::ioUringPartialCompletionCount() const noexcept {
    return ioUringPartialCompletionCount_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::ioUringRetryCount() const noexcept {
    return ioUringRetryCount_.load(std::memory_order_relaxed);
}

double FileIoStats::ioUringAverageBytesPerSqe() const noexcept {
    const std::uint64_t sqes = ioUringSqeCount();
    return sqes == 0 ? 0.0
                     : static_cast<double>(ioUringCompletionBytes_.load(std::memory_order_relaxed)) /
                           static_cast<double>(sqes);
}

std::uint64_t FileIoStats::posixWriteSyscallCount() const noexcept {
    return posixWriteSyscallCount_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::posixWriteSyscallBytes() const noexcept {
    return posixWriteSyscallBytes_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::posixWriteRetryCount() const noexcept {
    return posixWriteRetryCount_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::posixWriteShortCount() const noexcept {
    return posixWriteShortCount_.load(std::memory_order_relaxed);
}

std::uint64_t FileIoStats::posixWriteZeroCount() const noexcept {
    return posixWriteZeroCount_.load(std::memory_order_relaxed);
}

double FileIoStats::posixAverageBytesPerWriteSyscall() const noexcept {
    const std::uint64_t calls = posixWriteSyscallCount();
    return calls == 0 ? 0.0 : static_cast<double>(posixWriteSyscallBytes()) /
                                 static_cast<double>(calls);
}

common::Result<FileIoBackendKind> parseFileIoBackendKind(std::string_view value) {
    if (value == "posix") {
        return FileIoBackendKind::Posix;
    }
    if (value == "io_uring") {
        return FileIoBackendKind::IoUring;
    }
    return common::Status::invalidArgument("file IO backend must be posix or io_uring");
}

const char* fileIoBackendName(FileIoBackendKind backend) {
    switch (backend) {
        case FileIoBackendKind::Posix:
            return "posix";
        case FileIoBackendKind::IoUring:
            return "io_uring";
    }
    return "posix";
}

common::Result<FileIoAdvice> parseFileIoAdvice(std::string_view value) {
    if (value == "off") {
        return FileIoAdvice::Off;
    }
    if (value == "sequential") {
        return FileIoAdvice::Sequential;
    }
    if (value == "noreuse") {
        return FileIoAdvice::Noreuse;
    }
    if (value == "dontneed") {
        return FileIoAdvice::DontNeed;
    }
    if (value == "sequential_dontneed") {
        return FileIoAdvice::SequentialDontNeed;
    }
    return common::Status::invalidArgument(
        "file IO advice must be off, sequential, noreuse, dontneed, or sequential_dontneed");
}

const char* fileIoAdviceName(FileIoAdvice advice) {
    switch (advice) {
        case FileIoAdvice::Off:
            return "off";
        case FileIoAdvice::Sequential:
            return "sequential";
        case FileIoAdvice::Noreuse:
            return "noreuse";
        case FileIoAdvice::DontNeed:
            return "dontneed";
        case FileIoAdvice::SequentialDontNeed:
            return "sequential_dontneed";
    }
    return "off";
}

common::Result<PosixWriteStrategy> parsePosixWriteStrategy(std::string_view value) {
    if (value == "auto") {
        return PosixWriteStrategy::Auto;
    }
    if (value == "direct") {
        return PosixWriteStrategy::Direct;
    }
    if (value == "coalesced") {
        return PosixWriteStrategy::Coalesced;
    }
    return common::Status::invalidArgument(
        "posix write strategy must be auto, direct, or coalesced");
}

const char* posixWriteStrategyName(PosixWriteStrategy strategy) {
    switch (strategy) {
        case PosixWriteStrategy::Auto:
            return "auto";
        case PosixWriteStrategy::Direct:
            return "direct";
        case PosixWriteStrategy::Coalesced:
            return "coalesced";
    }
    return "auto";
}

PosixWriteStrategy effectivePosixWriteStrategy(const FileIoConfig& config) noexcept {
    if (config.posixWriteStrategy == PosixWriteStrategy::Direct) {
        return PosixWriteStrategy::Direct;
    }
    if (config.posixWriteStrategy == PosixWriteStrategy::Coalesced) {
        return PosixWriteStrategy::Coalesced;
    }
    return config.bufferSize > 0 ? PosixWriteStrategy::Coalesced : PosixWriteStrategy::Direct;
}

bool fileIoBackendAvailable(FileIoBackendKind backend) noexcept {
    switch (backend) {
        case FileIoBackendKind::Posix:
            return true;
        case FileIoBackendKind::IoUring:
            return CPNETFLUX_HAS_IO_URING != 0;
    }
    return false;
}

common::Status validateFileIoConfig(const FileIoConfig& config) {
    if (config.queueDepth == 0 || config.queueDepth > 256) {
        return common::Status::invalidArgument("file IO queue depth must be in range 1..256");
    }
    if (config.batchSize == 0 || config.batchSize > 256) {
        return common::Status::invalidArgument("file IO batch size must be in range 1..256");
    }
    if (config.posixWriteStrategy == PosixWriteStrategy::Coalesced && config.bufferSize == 0) {
        return common::Status::invalidArgument(
            "posix write strategy coalesced requires --file-io-buffer-size > 0");
    }
    return common::Status::ok();
}

common::Status applyFileIoAdvice(const PosixFile& file, FileIoAdvice advice, std::uint64_t offset,
                                 std::uint64_t length) {
    if (advice == FileIoAdvice::Off || !file.isValid()) {
        return common::Status::ok();
    }
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return common::Status::invalidArgument("file IO advice range exceeds off_t range");
    }
    const int firstResult =
        ::posix_fadvise(file.fd(), static_cast<off_t>(offset), static_cast<off_t>(length),
                        toPosixAdvice(advice));
    if (firstResult != 0) {
        return systemStatus("posix_fadvise", firstResult);
    }
    if (advice == FileIoAdvice::SequentialDontNeed) {
        const int secondResult =
            ::posix_fadvise(file.fd(), static_cast<off_t>(offset), static_cast<off_t>(length),
                            POSIX_FADV_DONTNEED);
        if (secondResult != 0) {
            return systemStatus("posix_fadvise", secondResult);
        }
    }
    return common::Status::ok();
}

common::Status posixWriteAtAllWithStats(const PosixFile& file, std::uint64_t offset,
                                        const std::uint8_t* data, std::size_t length,
                                        FileIoStats* stats) {
    if (length == 0) {
        return common::Status::ok();
    }
    if (!file.isValid()) {
        return common::Status::invalidArgument("file is not open");
    }
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return common::Status::invalidArgument("write offset exceeds off_t range");
    }

    std::size_t completed = 0;
    while (completed < length) {
        const std::uint64_t currentOffset = offset + completed;
        if (currentOffset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
            return common::Status::invalidArgument("write offset exceeds off_t range");
        }
        const std::size_t requested = length - completed;
        const ssize_t bytes =
            ::pwrite(file.fd(), data + completed, requested, static_cast<off_t>(currentOffset));
        if (bytes > 0) {
            const auto written = static_cast<std::size_t>(bytes);
            if (stats != nullptr) {
                stats->recordPosixWriteSyscall(written);
                if (written < requested) {
                    stats->recordPosixWriteShort();
                }
            }
            completed += written;
            continue;
        }
        if (bytes < 0 && (errno == EINTR || errno == EAGAIN)) {
            if (stats != nullptr) {
                stats->recordPosixWriteRetry();
            }
            continue;
        }
        if (bytes < 0) {
            return systemStatus("pwrite", errno);
        }
        if (stats != nullptr) {
            stats->recordPosixWriteZero();
        }
        return common::Status::runtimeError("pwrite returned zero bytes");
    }
    return common::Status::ok();
}

common::Status readAtAll(const PosixFile& file, std::uint64_t offset, std::uint8_t* data,
                         std::size_t length, FileIoStats* stats) {
    FileIoContext context;
    return readAtAll(file, offset, data, length, context, stats);
}

common::Status readAtAll(const PosixFile& file, std::uint64_t offset, std::uint8_t* data,
                         std::size_t length, const FileIoContext& context, FileIoStats* stats) {
    const common::Status available = context.validateAvailable();
    if (!available.isOk()) {
        return available;
    }
    const auto start = std::chrono::steady_clock::now();
    common::Status status;
    switch (context.backend()) {
        case FileIoBackendKind::Posix:
            status = file.readAtAll(offset, data, length);
            break;
        case FileIoBackendKind::IoUring:
            status = ioUringReadAtAll(file, offset, data, length, context.config(), stats);
            break;
    }
    const auto end = std::chrono::steady_clock::now();
    if (stats != nullptr && length > 0) {
        stats->recordRead(length, end - start);
    }
    return status;
}

common::Status writeAtAll(const PosixFile& file, std::uint64_t offset, const std::uint8_t* data,
                          std::size_t length, FileIoStats* stats) {
    FileIoContext context;
    return writeAtAll(file, offset, data, length, context, stats);
}

common::Status writeAtAll(const PosixFile& file, std::uint64_t offset, const std::uint8_t* data,
                          std::size_t length, const FileIoContext& context, FileIoStats* stats) {
    const common::Status available = context.validateAvailable();
    if (!available.isOk()) {
        return available;
    }
    const auto start = std::chrono::steady_clock::now();
    common::Status status;
    switch (context.backend()) {
        case FileIoBackendKind::Posix:
            status = posixWriteAtAllWithStats(file, offset, data, length, stats);
            break;
        case FileIoBackendKind::IoUring:
            status = ioUringWriteAtAll(file, offset, data, length, context.config(), stats);
            break;
    }
    const auto end = std::chrono::steady_clock::now();
    if (stats != nullptr && length > 0) {
        stats->recordWrite(length, end - start);
    }
    return status;
}

BufferedFileWriter::BufferedFileWriter(const PosixFile& file, const FileIoConfig& config,
                                       FileIoStats* stats)
    : file_(file), context_(config), stats_(stats) {
    if (effectivePosixWriteStrategy(config) == PosixWriteStrategy::Coalesced) {
        buffer_.resize(static_cast<std::size_t>(config.bufferSize));
    }
}

BufferedFileWriter::BufferedFileWriter(const PosixFile& file, const FileIoContext& context,
                                       FileIoStats* stats)
    : file_(file), context_(context), stats_(stats) {
    if (effectivePosixWriteStrategy(context.config()) == PosixWriteStrategy::Coalesced) {
        buffer_.resize(static_cast<std::size_t>(context.config().bufferSize));
    }
}

bool BufferedFileWriter::canAppend(std::uint64_t offset, std::size_t length) const noexcept {
    return !buffer_.empty() && buffered_ > 0 && offset == bufferOffset_ + buffered_ &&
           buffered_ + length <= buffer_.size();
}

common::Status BufferedFileWriter::write(std::uint64_t offset, const std::uint8_t* data,
                                         std::size_t length) {
    if (length == 0) {
        return common::Status::ok();
    }
    if (buffer_.empty() || length > buffer_.size()) {
        const common::Status flushStatus = flush();
        if (!flushStatus.isOk()) {
            return flushStatus;
        }
        return writeAtAll(file_, offset, data, length, context_, stats_);
    }
    if (!canAppend(offset, length)) {
        const common::Status flushStatus = flush();
        if (!flushStatus.isOk()) {
            return flushStatus;
        }
        bufferOffset_ = offset;
        buffered_ = 0;
    }
    std::copy_n(data, length, buffer_.data() + buffered_);
    buffered_ += length;
    if (buffered_ == buffer_.size()) {
        return flush();
    }
    return common::Status::ok();
}

common::Status BufferedFileWriter::flush() {
    if (buffered_ == 0) {
        return common::Status::ok();
    }
    const common::Status status =
        writeAtAll(file_, bufferOffset_, buffer_.data(), buffered_, context_, stats_);
    if (!status.isOk()) {
        return status;
    }
    buffered_ = 0;
    return common::Status::ok();
}

void appendFileIoStats(std::ostream& stream, const FileIoStats& stats) {
    stream << " stage_read_calls=" << stats.readCalls()
           << " stage_write_calls=" << stats.writeCalls()
           << " stage_read_avg_bytes_per_call=" << stats.averageReadBytesPerCall()
           << " stage_write_avg_bytes_per_call=" << stats.averageWriteBytesPerCall()
           << " file_io_wait_seconds=" << stats.waitSeconds()
           << " file_io_wait_bytes=" << (stats.readBytes() + stats.writeBytes())
           << " write_call_count=" << stats.writeCalls()
           << " write_syscall_count=" << stats.posixWriteSyscallCount()
           << " write_retry_count=" << stats.posixWriteRetryCount()
           << " write_short_count=" << stats.posixWriteShortCount()
           << " write_zero_count=" << stats.posixWriteZeroCount()
           << " write_total_bytes=" << stats.writeBytes()
           << " write_avg_bytes_per_call=" << stats.averageWriteBytesPerCall()
           << " write_avg_bytes_per_syscall=" << stats.posixAverageBytesPerWriteSyscall()
           << " io_uring_submit_count=" << stats.ioUringSubmitCount()
           << " io_uring_wait_count=" << stats.ioUringWaitCount()
           << " io_uring_completion_count=" << stats.ioUringCompletionCount()
           << " io_uring_sqe_count=" << stats.ioUringSqeCount()
           << " io_uring_partial_completion_count=" << stats.ioUringPartialCompletionCount()
           << " io_uring_retry_count=" << stats.ioUringRetryCount()
           << " io_uring_avg_bytes_per_sqe=" << stats.ioUringAverageBytesPerSqe();
}

}  // namespace cpnetflux::storage
