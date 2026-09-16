#include "cpnetflux/core/chunk/transfer_progress.h"

#include <algorithm>

namespace cpnetflux::core::chunk {

common::Status TransferProgress::begin(std::uint64_t totalSize) {
    totalSize_ = totalSize;
    bytesCompleted_ = 0;
    lastError_ = TransferProgressError::None;
    started_ = true;
    ranges_.clear();
    return common::Status::ok();
}

common::Status TransferProgress::recordFrame(std::uint64_t chunkId, std::uint64_t offset,
                                             std::uint64_t length) {
    if (!started_) {
        lastError_ = TransferProgressError::RangeOutOfBounds;
        return common::Status::runtimeError("transfer progress was not started");
    }
    if (length == 0) {
        lastError_ = TransferProgressError::RangeOutOfBounds;
        return common::Status::invalidArgument("range length must be greater than zero");
    }
    if (offset > totalSize_ || length > totalSize_ - offset) {
        lastError_ = TransferProgressError::RangeOutOfBounds;
        return common::Status::invalidArgument("range exceeds transfer size");
    }

    const std::uint64_t end = offset + length;
    const auto insertAt = std::lower_bound(
        ranges_.begin(), ranges_.end(), offset,
        [](const Range& range, std::uint64_t value) { return range.begin < value; });

    if (insertAt != ranges_.begin()) {
        const auto previous = insertAt - 1;
        if (previous->end > offset) {
            lastError_ = TransferProgressError::DuplicateRange;
            return common::Status::invalidArgument("range overlaps previous completed range");
        }
    }
    if (insertAt != ranges_.end() && insertAt->begin < end) {
        lastError_ = TransferProgressError::DuplicateRange;
        return common::Status::invalidArgument("range overlaps next completed range");
    }

    ranges_.insert(insertAt, Range{offset, end, chunkId});
    bytesCompleted_ += length;
    lastError_ = TransferProgressError::None;
    return common::Status::ok();
}

common::Status TransferProgress::finish() {
    if (!started_) {
        lastError_ = TransferProgressError::MissingRange;
        return common::Status::runtimeError("transfer progress was not started");
    }
    if (totalSize_ == 0) {
        if (!ranges_.empty() || bytesCompleted_ != 0) {
            lastError_ = TransferProgressError::RangeOutOfBounds;
            return common::Status::runtimeError("empty transfer has completed ranges");
        }
        lastError_ = TransferProgressError::None;
        return common::Status::ok();
    }

    std::uint64_t expected = 0;
    for (const Range& range : ranges_) {
        if (range.begin != expected) {
            lastError_ = TransferProgressError::MissingRange;
            return common::Status::runtimeError("transfer has missing range");
        }
        expected = range.end;
    }

    if (expected != totalSize_ || bytesCompleted_ != totalSize_) {
        lastError_ = TransferProgressError::MissingRange;
        return common::Status::runtimeError("transfer did not complete expected size");
    }

    lastError_ = TransferProgressError::None;
    return common::Status::ok();
}

std::uint64_t TransferProgress::totalSize() const noexcept { return totalSize_; }

std::uint64_t TransferProgress::bytesCompleted() const noexcept { return bytesCompleted_; }

TransferProgressError TransferProgress::lastError() const noexcept { return lastError_; }

}  // namespace cpnetflux::core::chunk
