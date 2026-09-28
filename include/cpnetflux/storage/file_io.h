#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/storage/posix_file.h"

namespace cpnetflux::storage {

enum class FileIoBackendKind {
    Posix,
    IoUring,
};

enum class FileIoAdvice {
    Off,
    Sequential,
    Noreuse,
    DontNeed,
    SequentialDontNeed,
};

enum class PosixWriteStrategy {
    Auto,
    Direct,
    Coalesced,
};

struct FileIoConfig {
    FileIoBackendKind backend = FileIoBackendKind::Posix;
    std::uint64_t bufferSize = 0;
    std::uint64_t queueDepth = 1;
    std::uint64_t batchSize = 1;
    FileIoAdvice advice = FileIoAdvice::Off;
    PosixWriteStrategy posixWriteStrategy = PosixWriteStrategy::Auto;
};

class FileIoContext {
   public:
    FileIoContext() = default;
    explicit FileIoContext(FileIoConfig config) noexcept;

    [[nodiscard]] const FileIoConfig& config() const noexcept;
    [[nodiscard]] FileIoBackendKind backend() const noexcept;
    [[nodiscard]] common::Status validateAvailable() const;

   private:
    FileIoConfig config_;
};

class FileIoStats {
   public:
    void recordRead(std::uint64_t bytes, std::chrono::steady_clock::duration duration) noexcept;
    void recordWrite(std::uint64_t bytes, std::chrono::steady_clock::duration duration) noexcept;
    void recordIoUringSubmit(std::uint64_t sqes) noexcept;
    void recordIoUringWait() noexcept;
    void recordIoUringCompletion(std::uint64_t bytes) noexcept;
    void recordIoUringPartialCompletion() noexcept;
    void recordIoUringRetry() noexcept;
    void recordPosixWriteSyscall(std::uint64_t bytes) noexcept;
    void recordPosixWriteRetry() noexcept;
    void recordPosixWriteShort() noexcept;
    void recordPosixWriteZero() noexcept;
    void mergeFrom(const FileIoStats& other) noexcept;

    [[nodiscard]] std::uint64_t readCalls() const noexcept;
    [[nodiscard]] std::uint64_t writeCalls() const noexcept;
    [[nodiscard]] std::uint64_t readBytes() const noexcept;
    [[nodiscard]] std::uint64_t writeBytes() const noexcept;
    [[nodiscard]] double waitSeconds() const noexcept;
    [[nodiscard]] double averageReadBytesPerCall() const noexcept;
    [[nodiscard]] double averageWriteBytesPerCall() const noexcept;
    [[nodiscard]] std::uint64_t ioUringSubmitCount() const noexcept;
    [[nodiscard]] std::uint64_t ioUringWaitCount() const noexcept;
    [[nodiscard]] std::uint64_t ioUringCompletionCount() const noexcept;
    [[nodiscard]] std::uint64_t ioUringSqeCount() const noexcept;
    [[nodiscard]] std::uint64_t ioUringCompletionBytes() const noexcept;
    [[nodiscard]] std::uint64_t ioUringPartialCompletionCount() const noexcept;
    [[nodiscard]] std::uint64_t ioUringRetryCount() const noexcept;
    [[nodiscard]] double ioUringAverageBytesPerSqe() const noexcept;
    [[nodiscard]] std::uint64_t posixWriteSyscallCount() const noexcept;
    [[nodiscard]] std::uint64_t posixWriteSyscallBytes() const noexcept;
    [[nodiscard]] std::uint64_t posixWriteRetryCount() const noexcept;
    [[nodiscard]] std::uint64_t posixWriteShortCount() const noexcept;
    [[nodiscard]] std::uint64_t posixWriteZeroCount() const noexcept;
    [[nodiscard]] double posixAverageBytesPerWriteSyscall() const noexcept;

   private:
    std::atomic<std::uint64_t> readCalls_{0};
    std::atomic<std::uint64_t> writeCalls_{0};
    std::atomic<std::uint64_t> readBytes_{0};
    std::atomic<std::uint64_t> writeBytes_{0};
    std::atomic<std::uint64_t> waitNanos_{0};
    std::atomic<std::uint64_t> ioUringSubmitCount_{0};
    std::atomic<std::uint64_t> ioUringWaitCount_{0};
    std::atomic<std::uint64_t> ioUringCompletionCount_{0};
    std::atomic<std::uint64_t> ioUringSqeCount_{0};
    std::atomic<std::uint64_t> ioUringCompletionBytes_{0};
    std::atomic<std::uint64_t> ioUringPartialCompletionCount_{0};
    std::atomic<std::uint64_t> ioUringRetryCount_{0};
    std::atomic<std::uint64_t> posixWriteSyscallCount_{0};
    std::atomic<std::uint64_t> posixWriteSyscallBytes_{0};
    std::atomic<std::uint64_t> posixWriteRetryCount_{0};
    std::atomic<std::uint64_t> posixWriteShortCount_{0};
    std::atomic<std::uint64_t> posixWriteZeroCount_{0};
};

[[nodiscard]] common::Result<FileIoBackendKind> parseFileIoBackendKind(std::string_view value);
[[nodiscard]] const char* fileIoBackendName(FileIoBackendKind backend);
[[nodiscard]] common::Result<FileIoAdvice> parseFileIoAdvice(std::string_view value);
[[nodiscard]] const char* fileIoAdviceName(FileIoAdvice advice);
[[nodiscard]] common::Result<PosixWriteStrategy> parsePosixWriteStrategy(std::string_view value);
[[nodiscard]] const char* posixWriteStrategyName(PosixWriteStrategy strategy);
[[nodiscard]] PosixWriteStrategy effectivePosixWriteStrategy(const FileIoConfig& config) noexcept;
[[nodiscard]] common::Status validateFileIoConfig(const FileIoConfig& config);
[[nodiscard]] bool fileIoBackendAvailable(FileIoBackendKind backend) noexcept;
[[nodiscard]] common::Status applyFileIoAdvice(const PosixFile& file, FileIoAdvice advice,
                                               std::uint64_t offset, std::uint64_t length);

[[nodiscard]] common::Status readAtAll(const PosixFile& file, std::uint64_t offset,
                                       std::uint8_t* data, std::size_t length,
                                       FileIoStats* stats);
[[nodiscard]] common::Status readAtAll(const PosixFile& file, std::uint64_t offset,
                                       std::uint8_t* data, std::size_t length,
                                       const FileIoContext& context, FileIoStats* stats);
[[nodiscard]] common::Status writeAtAll(const PosixFile& file, std::uint64_t offset,
                                        const std::uint8_t* data, std::size_t length,
                                        FileIoStats* stats);
[[nodiscard]] common::Status writeAtAll(const PosixFile& file, std::uint64_t offset,
                                        const std::uint8_t* data, std::size_t length,
                                        const FileIoContext& context, FileIoStats* stats);

[[nodiscard]] common::Status ioUringReadAtAll(const PosixFile& file, std::uint64_t offset,
                                              std::uint8_t* data, std::size_t length,
                                              const FileIoConfig& config, FileIoStats* stats);
[[nodiscard]] common::Status ioUringWriteAtAll(const PosixFile& file, std::uint64_t offset,
                                               const std::uint8_t* data, std::size_t length,
                                               const FileIoConfig& config, FileIoStats* stats);

enum class IoUringOperation {
    Read,
    Write,
};

using IoUringCompletionFn = common::Status (*)(std::uint64_t offset, std::size_t length,
                                               std::size_t* completed, void* userData);

[[nodiscard]] common::Status ioUringRunCompletionLoopForTest(IoUringOperation operation,
                                                             std::uint64_t offset,
                                                             std::size_t length,
                                                             IoUringCompletionFn completion,
                                                             void* userData);
[[nodiscard]] common::Status ioUringRunBatchedCompletionLoopForTest(
    IoUringOperation operation, std::uint64_t offset, std::size_t length,
    std::uint64_t queueDepth, std::uint64_t batchSize, std::size_t maxBytesPerSqe,
    IoUringCompletionFn completion, void* userData, FileIoStats* stats);

class BufferedFileWriter {
   public:
    BufferedFileWriter(const PosixFile& file, const FileIoConfig& config, FileIoStats* stats);
    BufferedFileWriter(const PosixFile& file, const FileIoContext& context, FileIoStats* stats);
    BufferedFileWriter(const BufferedFileWriter&) = delete;
    BufferedFileWriter& operator=(const BufferedFileWriter&) = delete;

    [[nodiscard]] common::Status write(std::uint64_t offset, const std::uint8_t* data,
                                       std::size_t length);
    [[nodiscard]] common::Status flush();

   private:
    [[nodiscard]] bool canAppend(std::uint64_t offset, std::size_t length) const noexcept;

    const PosixFile& file_;
    FileIoContext context_;
    FileIoStats* stats_;
    std::vector<std::uint8_t> buffer_;
    std::uint64_t bufferOffset_ = 0;
    std::size_t buffered_ = 0;
};

void appendFileIoStats(std::ostream& stream, const FileIoStats& stats);

}  // namespace cpnetflux::storage
