#include "cpnetflux/core/chunk/range_list.h"

#include <algorithm>
#include <limits>

namespace cpnetflux::core::chunk {
namespace {

std::uint64_t rangeLength(const CompletedRange& range) noexcept { return range.end - range.begin; }

}  // namespace

common::Status RangeList::validate(std::uint64_t offset, std::uint64_t length,
                                   std::uint64_t totalSize) const {
    if (length == 0) {
        return common::Status::invalidArgument("range length must be greater than zero");
    }
    if (offset > totalSize || length > totalSize - offset) {
        return common::Status::invalidArgument("range exceeds transfer size");
    }
    return common::Status::ok();
}

common::Status RangeList::insert(std::uint64_t offset, std::uint64_t length,
                                 std::uint64_t totalSize) {
    const common::Status valid = validate(offset, length, totalSize);
    if (!valid.isOk()) {
        return valid;
    }

    const CompletedRange incoming{offset, offset + length};
    auto insertAt = std::lower_bound(
        ranges_.begin(), ranges_.end(), incoming.begin,
        [](const CompletedRange& range, std::uint64_t value) { return range.begin < value; });

    if (insertAt != ranges_.begin()) {
        auto previous = insertAt - 1;
        if (previous->end > incoming.begin) {
            return common::Status::invalidArgument("range overlaps previous completed range");
        }
    }
    if (insertAt != ranges_.end() && insertAt->begin < incoming.end) {
        return common::Status::invalidArgument("range overlaps next completed range");
    }

    CompletedRange merged = incoming;
    if (insertAt != ranges_.begin()) {
        auto previous = insertAt - 1;
        if (previous->end == merged.begin) {
            merged.begin = previous->begin;
            bytesCompleted_ -= rangeLength(*previous);
            insertAt = ranges_.erase(previous);
        }
    }
    if (insertAt != ranges_.end() && merged.end == insertAt->begin) {
        merged.end = insertAt->end;
        bytesCompleted_ -= rangeLength(*insertAt);
        insertAt = ranges_.erase(insertAt);
    }

    ranges_.insert(insertAt, merged);
    bytesCompleted_ += rangeLength(merged);
    return common::Status::ok();
}

common::Status RangeList::merge(std::uint64_t offset, std::uint64_t length,
                                std::uint64_t totalSize) {
    const common::Status valid = validate(offset, length, totalSize);
    if (!valid.isOk()) {
        return valid;
    }

    CompletedRange merged{offset, offset + length};
    auto current = std::lower_bound(
        ranges_.begin(), ranges_.end(), merged.begin,
        [](const CompletedRange& range, std::uint64_t value) { return range.end < value; });

    if (current != ranges_.begin()) {
        auto previous = current - 1;
        if (previous->end >= merged.begin) {
            current = previous;
        }
    }

    while (current != ranges_.end() && current->begin <= merged.end) {
        merged.begin = std::min(merged.begin, current->begin);
        merged.end = std::max(merged.end, current->end);
        bytesCompleted_ -= rangeLength(*current);
        current = ranges_.erase(current);
    }

    auto insertAt = std::lower_bound(
        ranges_.begin(), ranges_.end(), merged.begin,
        [](const CompletedRange& range, std::uint64_t value) { return range.begin < value; });
    ranges_.insert(insertAt, merged);
    bytesCompleted_ += rangeLength(merged);
    return common::Status::ok();
}

std::vector<CompletedRange> RangeList::missingRanges(std::uint64_t totalSize) const {
    std::vector<CompletedRange> missing;
    std::uint64_t expected = 0;
    for (const CompletedRange& range : ranges_) {
        if (range.begin > expected) {
            missing.push_back(CompletedRange{expected, range.begin});
        }
        expected = std::max(expected, range.end);
    }
    if (expected < totalSize) {
        missing.push_back(CompletedRange{expected, totalSize});
    }
    return missing;
}

const std::vector<CompletedRange>& RangeList::ranges() const noexcept { return ranges_; }

std::uint64_t RangeList::bytesCompleted() const noexcept { return bytesCompleted_; }

bool RangeList::empty() const noexcept { return ranges_.empty(); }

void RangeList::clear() noexcept {
    ranges_.clear();
    bytesCompleted_ = 0;
}

}  // namespace cpnetflux::core::chunk
