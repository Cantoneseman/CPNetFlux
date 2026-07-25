#include "gridflux/storage/file_io.h"

#if GRIDFLUX_HAS_IO_URING

#include <liburing.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace gridflux::storage {
namespace {

constexpr std::uint64_t kMaxQueueDepth = 256;

struct IoUringSlot {
    std::uint64_t offset = 0;
    std::size_t baseIndex = 0;
    std::size_t remaining = 0;
    std::size_t submittedLength = 0;
    bool inFlight = false;
};

struct IoUringFileState {
    io_uring* ring = nullptr;
    const PosixFile* file = nullptr;
    std::uint8_t* readData = nullptr;
    const std::uint8_t* writeData = nullptr;
    std::uint64_t baseOffset = 0;
    IoUringOperation operation = IoUringOperation::Read;
};

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

bool isRetryError(int error) noexcept { return error == EINTR || error == EAGAIN; }

std::size_t clampRange(std::uint64_t value, std::uint64_t fallback, std::uint64_t maximum) noexcept {
    const std::uint64_t normalized = value == 0 ? fallback : value;
    return static_cast<std::size_t>(std::min(normalized, maximum));
}

common::Status validateOffset(std::uint64_t offset, std::size_t length, const char* operation) {
    const std::uint64_t maxOffset = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
    if (offset > maxOffset) {
        return common::Status::invalidArgument(std::string(operation) + " offset exceeds off_t range");
    }
    if (length > 0 && offset > maxOffset - static_cast<std::uint64_t>(length - 1)) {
        return common::Status::invalidArgument(std::string(operation) + " range exceeds off_t range");
    }
    return common::Status::ok();
}

common::Status initRing(io_uring* ring, std::uint64_t queueDepth) {
    const std::size_t depth = clampRange(queueDepth, 1, kMaxQueueDepth);
    const int result = io_uring_queue_init(static_cast<unsigned>(depth), ring, 0);
    if (result < 0) {
        return systemStatus("io_uring_queue_init", -result);
    }
    return common::Status::ok();
}

common::Status prepFileSqe(IoUringFileState* state, IoUringSlot* slot, std::size_t slotIndex) {
    io_uring_sqe* sqe = io_uring_get_sqe(state->ring);
    if (sqe == nullptr) {
        return common::Status::runtimeError("io_uring_get_sqe returned null");
    }
    if (state->operation == IoUringOperation::Read) {
        io_uring_prep_read(sqe, state->file->fd(),
                           state->readData + (slot->offset - state->baseOffset),
                           slot->submittedLength, static_cast<off_t>(slot->offset));
    } else {
        io_uring_prep_write(sqe, state->file->fd(),
                            state->writeData + (slot->offset - state->baseOffset),
                            slot->submittedLength, static_cast<off_t>(slot->offset));
    }
    sqe->user_data = static_cast<__u64>(slotIndex + 1);
    slot->inFlight = true;
    return common::Status::ok();
}

common::Status handleCompletionResult(IoUringOperation operation, int result, IoUringSlot* slot,
                                      FileIoStats* stats) {
    slot->inFlight = false;
    if (result < 0) {
        const int error = -result;
        if (isRetryError(error)) {
            if (stats != nullptr) {
                stats->recordIoUringRetry();
            }
            return common::Status::ok();
        }
        return systemStatus(operation == IoUringOperation::Read ? "io_uring read" : "io_uring write",
                            error);
    }
    if (result == 0) {
        return operation == IoUringOperation::Read
                   ? common::Status::runtimeError("unexpected EOF while reading file")
                   : common::Status::runtimeError("io_uring write returned zero bytes");
    }
    const std::size_t completed = static_cast<std::size_t>(result);
    if (completed > slot->submittedLength) {
        return common::Status::runtimeError("io_uring completion exceeded submitted length");
    }
    if (stats != nullptr) {
        stats->recordIoUringCompletion(completed);
        if (completed < slot->submittedLength) {
            stats->recordIoUringPartialCompletion();
        }
    }
    slot->offset += completed;
    slot->baseIndex += completed;
    slot->remaining -= completed;
    return common::Status::ok();
}

common::Status submitPending(io_uring* ring, std::uint64_t submittedSqes, FileIoStats* stats) {
    if (submittedSqes == 0) {
        return common::Status::ok();
    }
    const int result = io_uring_submit(ring);
    if (result < 0) {
        return systemStatus("io_uring_submit", -result);
    }
    if (stats != nullptr) {
        stats->recordIoUringSubmit(submittedSqes);
    }
    return common::Status::ok();
}

}  // namespace

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
    const common::Status offsetStatus = validateOffset(offset, length, "file");
    if (!offsetStatus.isOk()) {
        return offsetStatus;
    }

    const std::size_t depth = clampRange(queueDepth, 1, kMaxQueueDepth);
    const std::size_t batch = std::min<std::size_t>(clampRange(batchSize, depth, kMaxQueueDepth), depth);
    const std::size_t sqeBytes = std::max<std::size_t>(1, maxBytesPerSqe);
    std::vector<IoUringSlot> slots(depth);

    std::size_t nextIndex = 0;
    std::size_t inFlight = 0;
    while (nextIndex < length || inFlight > 0) {
        std::uint64_t submitted = 0;
        for (std::size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
            IoUringSlot& slot = slots[slotIndex];
            if (nextIndex >= length || inFlight >= depth || submitted >= batch) {
                break;
            }
            if (slot.inFlight || slot.remaining != 0) {
                continue;
            }
            const std::size_t size = std::min(sqeBytes, length - nextIndex);
            slot = IoUringSlot{
                .offset = offset + nextIndex,
                .baseIndex = nextIndex,
                .remaining = size,
                .submittedLength = size,
                .inFlight = true,
            };
            nextIndex += size;
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
        IoUringSlot* slot = nullptr;
        for (IoUringSlot& candidate : slots) {
            if (candidate.inFlight) {
                slot = &candidate;
            }
        }
        if (slot == nullptr) {
            return common::Status::runtimeError("io_uring test loop lost an in-flight slot");
        }
        std::size_t completed = 0;
        const common::Status completionStatus =
            completion(slot->offset, slot->submittedLength, &completed, userData);
        if (!completionStatus.isOk()) {
            if (completionStatus.code() == common::StatusCode::SystemError &&
                isRetryError(completionStatus.errorNumber())) {
                if (stats != nullptr) {
                    stats->recordIoUringRetry();
                    stats->recordIoUringSubmit(1);
                }
                continue;
            }
            return completionStatus;
        }
        const common::Status handleStatus =
            handleCompletionResult(operation, static_cast<int>(completed), slot, stats);
        if (!handleStatus.isOk()) {
            return handleStatus;
        }
        --inFlight;
        if (slot->remaining > 0) {
            slot->submittedLength = slot->remaining;
            slot->inFlight = true;
            ++inFlight;
            if (stats != nullptr) {
                stats->recordIoUringSubmit(1);
            }
        } else {
            *slot = IoUringSlot{};
        }
    }
    return common::Status::ok();
}

common::Status ioUringRunCompletionLoopForTest(IoUringOperation operation, std::uint64_t offset,
                                               std::size_t length,
                                               IoUringCompletionFn completion,
                                               void* userData) {
    return ioUringRunBatchedCompletionLoopForTest(operation, offset, length, 1, 1, length,
                                                  completion, userData, nullptr);
}

common::Status ioUringReadAtAll(const PosixFile& file, std::uint64_t offset, std::uint8_t* data,
                                std::size_t length, const FileIoConfig& config,
                                FileIoStats* stats) {
    if (length == 0) {
        return common::Status::ok();
    }
    const common::Status offsetStatus = validateOffset(offset, length, "read");
    if (!offsetStatus.isOk()) {
        return offsetStatus;
    }

    io_uring ring {};
    const common::Status initStatus = initRing(&ring, config.queueDepth);
    if (!initStatus.isOk()) {
        return initStatus;
    }

    const std::size_t depth = clampRange(config.queueDepth, 1, kMaxQueueDepth);
    const std::size_t batch =
        std::min<std::size_t>(clampRange(config.batchSize, depth, kMaxQueueDepth), depth);
    const std::size_t maxBytesPerSqe = std::max<std::size_t>(1, (length + depth - 1) / depth);
    std::vector<IoUringSlot> slots(depth);
    IoUringFileState state{&ring, &file, data, nullptr, offset, IoUringOperation::Read};

    std::size_t nextIndex = 0;
    std::size_t inFlight = 0;
    common::Status finalStatus = common::Status::ok();
    while (nextIndex < length || inFlight > 0) {
        std::uint64_t submitted = 0;
        for (std::size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
            IoUringSlot& slot = slots[slotIndex];
            if (nextIndex >= length || inFlight >= depth || submitted >= batch) {
                break;
            }
            if (slot.inFlight || slot.remaining != 0) {
                continue;
            }
            const std::size_t size = std::min(maxBytesPerSqe, length - nextIndex);
            slot = IoUringSlot{offset + nextIndex, nextIndex, size, size, false};
            const common::Status prepStatus = prepFileSqe(&state, &slot, slotIndex);
            if (!prepStatus.isOk()) {
                finalStatus = prepStatus;
                goto done;
            }
            nextIndex += size;
            ++inFlight;
            ++submitted;
        }
        finalStatus = submitPending(&ring, submitted, stats);
        if (!finalStatus.isOk()) {
            goto done;
        }
        if (inFlight == 0) {
            continue;
        }
        if (stats != nullptr) {
            stats->recordIoUringWait();
        }
        io_uring_cqe* cqe = nullptr;
        const int waitResult = io_uring_wait_cqe(&ring, &cqe);
        if (waitResult < 0) {
            if (-waitResult == EINTR || -waitResult == EAGAIN) {
                if (stats != nullptr) {
                    stats->recordIoUringRetry();
                }
                continue;
            }
            finalStatus = systemStatus("io_uring_wait_cqe", -waitResult);
            goto done;
        }
        const std::uint64_t encodedSlot = cqe->user_data;
        const int result = cqe->res;
        io_uring_cqe_seen(&ring, cqe);
        if (encodedSlot == 0 || encodedSlot > slots.size()) {
            finalStatus = common::Status::runtimeError("io_uring completion missing slot");
            goto done;
        }
        IoUringSlot* slot = &slots[static_cast<std::size_t>(encodedSlot - 1)];
        if (!slot->inFlight) {
            finalStatus = common::Status::runtimeError("io_uring completion missing slot");
            goto done;
        }
        finalStatus = handleCompletionResult(IoUringOperation::Read, result, slot, stats);
        if (!finalStatus.isOk()) {
            goto done;
        }
        --inFlight;
        if (slot->remaining > 0) {
            slot->submittedLength = slot->remaining;
            finalStatus = prepFileSqe(&state, slot, static_cast<std::size_t>(encodedSlot - 1));
            if (!finalStatus.isOk()) {
                goto done;
            }
            finalStatus = submitPending(&ring, 1, stats);
            if (!finalStatus.isOk()) {
                goto done;
            }
            ++inFlight;
        } else {
            *slot = IoUringSlot{};
        }
    }

done:
    io_uring_queue_exit(&ring);
    return finalStatus;
}

common::Status ioUringWriteAtAll(const PosixFile& file, std::uint64_t offset,
                                 const std::uint8_t* data, std::size_t length,
                                 const FileIoConfig& config, FileIoStats* stats) {
    if (length == 0) {
        return common::Status::ok();
    }
    const common::Status offsetStatus = validateOffset(offset, length, "write");
    if (!offsetStatus.isOk()) {
        return offsetStatus;
    }

    io_uring ring {};
    const common::Status initStatus = initRing(&ring, config.queueDepth);
    if (!initStatus.isOk()) {
        return initStatus;
    }

    const std::size_t depth = clampRange(config.queueDepth, 1, kMaxQueueDepth);
    const std::size_t batch =
        std::min<std::size_t>(clampRange(config.batchSize, depth, kMaxQueueDepth), depth);
    const std::size_t maxBytesPerSqe = std::max<std::size_t>(1, (length + depth - 1) / depth);
    std::vector<IoUringSlot> slots(depth);
    IoUringFileState state{&ring, &file, nullptr, data, offset, IoUringOperation::Write};

    std::size_t nextIndex = 0;
    std::size_t inFlight = 0;
    common::Status finalStatus = common::Status::ok();
    while (nextIndex < length || inFlight > 0) {
        std::uint64_t submitted = 0;
        for (std::size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
            IoUringSlot& slot = slots[slotIndex];
            if (nextIndex >= length || inFlight >= depth || submitted >= batch) {
                break;
            }
            if (slot.inFlight || slot.remaining != 0) {
                continue;
            }
            const std::size_t size = std::min(maxBytesPerSqe, length - nextIndex);
            slot = IoUringSlot{offset + nextIndex, nextIndex, size, size, false};
            const common::Status prepStatus = prepFileSqe(&state, &slot, slotIndex);
            if (!prepStatus.isOk()) {
                finalStatus = prepStatus;
                goto done;
            }
            nextIndex += size;
            ++inFlight;
            ++submitted;
        }
        finalStatus = submitPending(&ring, submitted, stats);
        if (!finalStatus.isOk()) {
            goto done;
        }
        if (inFlight == 0) {
            continue;
        }
        if (stats != nullptr) {
            stats->recordIoUringWait();
        }
        io_uring_cqe* cqe = nullptr;
        const int waitResult = io_uring_wait_cqe(&ring, &cqe);
        if (waitResult < 0) {
            if (-waitResult == EINTR || -waitResult == EAGAIN) {
                if (stats != nullptr) {
                    stats->recordIoUringRetry();
                }
                continue;
            }
            finalStatus = systemStatus("io_uring_wait_cqe", -waitResult);
            goto done;
        }
        const std::uint64_t encodedSlot = cqe->user_data;
        const int result = cqe->res;
        io_uring_cqe_seen(&ring, cqe);
        if (encodedSlot == 0 || encodedSlot > slots.size()) {
            finalStatus = common::Status::runtimeError("io_uring completion missing slot");
            goto done;
        }
        IoUringSlot* slot = &slots[static_cast<std::size_t>(encodedSlot - 1)];
        if (!slot->inFlight) {
            finalStatus = common::Status::runtimeError("io_uring completion missing slot");
            goto done;
        }
        finalStatus = handleCompletionResult(IoUringOperation::Write, result, slot, stats);
        if (!finalStatus.isOk()) {
            goto done;
        }
        --inFlight;
        if (slot->remaining > 0) {
            slot->submittedLength = slot->remaining;
            finalStatus = prepFileSqe(&state, slot, static_cast<std::size_t>(encodedSlot - 1));
            if (!finalStatus.isOk()) {
                goto done;
            }
            finalStatus = submitPending(&ring, 1, stats);
            if (!finalStatus.isOk()) {
                goto done;
            }
            ++inFlight;
        } else {
            *slot = IoUringSlot{};
        }
    }

done:
    io_uring_queue_exit(&ring);
    return finalStatus;
}

}  // namespace gridflux::storage

#endif
