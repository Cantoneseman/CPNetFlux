#pragma once

#include <cstdint>
#include <vector>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::chunk {

struct CompletedRange {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

class RangeList {
   public:
    common::Status insert(std::uint64_t offset, std::uint64_t length, std::uint64_t totalSize);
    common::Status merge(std::uint64_t offset, std::uint64_t length, std::uint64_t totalSize);

    [[nodiscard]] std::vector<CompletedRange> missingRanges(std::uint64_t totalSize) const;
    [[nodiscard]] const std::vector<CompletedRange>& ranges() const noexcept;
    [[nodiscard]] std::uint64_t bytesCompleted() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;

   private:
    common::Status validate(std::uint64_t offset, std::uint64_t length,
                            std::uint64_t totalSize) const;

    std::vector<CompletedRange> ranges_;
    std::uint64_t bytesCompleted_ = 0;
};

}  // namespace cpnetflux::core::chunk
