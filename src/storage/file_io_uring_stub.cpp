#include "gridflux/storage/file_io.h"

#include <cerrno>
#include <vector>

namespace gridflux::storage {

common::Status ioUringRunCompletionLoopForTest(IoUringOperation operation, std::uint64_t offset,
                                               std::size_t length,
                                               IoUringCompletionFn completion,
                                               void* userData) {
    return ioUringRunBatchedCompletionLoopForTest(operation, offset, length, 1, 1, length,
                                                  completion, userData, nullptr);
}

common::Status ioUringRunBatchedCompletionLoopForTest(
    IoUringOperation operation, std::uint64_t offset, std::size_t length,
    std::uint64_t queueDepth, std::uint64_t batchSize, std::size_t maxBytesPerSqe,
    IoUringCompletionFn completion, void* userData, FileIoStats* stats) {
    if (length == 0) {
        return common::Status::ok();
    }
    if (completion == nullptr) {
        return common::Status::invalidArgument("io_uring completion callback is required");
    }
    const std::size_t depth =
        static_cast<std::size_t>(std::min<std::uint64_t>(queueDepth == 0 ? 1 : queueDepth, 256));
    const std::size_t batch =
        static_cast<std::size_t>(std::min<std::uint64_t>(batchSize == 0 ? depth : batchSize, depth));
    const std::size_t sqeBytes = std::max<std::size_t>(1, maxBytesPerSqe);
    struct Slot {
        std::uint64_t offset = 0;
        std::size_t remaining = 0;
        std::size_t submittedLength = 0;
        bool inFlight = false;
    };

    std::vector<Slot> slots(depth);
    std::size_t nextOffset = 0;
    std::size_t inFlight = 0;
    while (nextOffset < length || inFlight > 0) {
        std::size_t submitted = 0;
        for (Slot& slot : slots) {
            if (nextOffset >= length || inFlight >= depth || submitted >= batch) {
                break;
            }
            if (slot.inFlight || slot.remaining != 0) {
                continue;
            }
            const std::size_t request = std::min<std::size_t>(sqeBytes, length - nextOffset);
            slot = Slot{
                .offset = offset + nextOffset,
                .remaining = request,
                .submittedLength = request,
                .inFlight = true,
            };
            nextOffset += request;
            ++inFlight;
            ++submitted;
        }
        if (submitted > 0 && stats != nullptr) {
            stats->recordIoUringSubmit(submitted);
        }
        if (inFlight == 0) {
            continue;
        }
        if (stats != nullptr) {
            stats->recordIoUringWait();
        }

        Slot* slot = nullptr;
        for (Slot& candidate : slots) {
            if (candidate.inFlight) {
                slot = &candidate;
            }
        }
        if (slot == nullptr) {
            return common::Status::runtimeError("io_uring test loop lost an in-flight slot");
        }

        std::size_t bytes = 0;
        const common::Status status =
            completion(slot->offset, slot->submittedLength, &bytes, userData);
        if (!status.isOk()) {
            if (status.code() == common::StatusCode::SystemError &&
                (status.errorNumber() == EINTR || status.errorNumber() == EAGAIN)) {
                if (stats != nullptr) {
                    stats->recordIoUringRetry();
                    stats->recordIoUringSubmit(1);
                }
                continue;
            }
            return status;
        }
        if (bytes == 0) {
            return operation == IoUringOperation::Read
                       ? common::Status::runtimeError("unexpected EOF while reading file")
                       : common::Status::runtimeError("io_uring write returned zero bytes");
        }
        if (bytes > slot->submittedLength) {
            return common::Status::runtimeError("io_uring completion exceeded submitted length");
        }
        if (stats != nullptr) {
            stats->recordIoUringCompletion(bytes);
            if (bytes < slot->submittedLength) {
                stats->recordIoUringPartialCompletion();
            }
        }
        slot->offset += bytes;
        slot->remaining -= bytes;
        --inFlight;
        if (slot->remaining > 0) {
            slot->submittedLength = slot->remaining;
            slot->inFlight = true;
            ++inFlight;
            if (stats != nullptr) {
                stats->recordIoUringSubmit(1);
            }
        } else {
            *slot = Slot{};
        }
    }
    return common::Status::ok();
}

common::Status ioUringReadAtAll(const PosixFile&, std::uint64_t, std::uint8_t*, std::size_t,
                                const FileIoConfig&, FileIoStats*) {
    return common::Status::runtimeError(
        "file IO backend unavailable: io_uring (liburing not found at build time)");
}

common::Status ioUringWriteAtAll(const PosixFile&, std::uint64_t, const std::uint8_t*,
                                 std::size_t, const FileIoConfig&, FileIoStats*) {
    return common::Status::runtimeError(
        "file IO backend unavailable: io_uring (liburing not found at build time)");
}

}  // namespace gridflux::storage
