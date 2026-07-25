#pragma once

#include <cstdint>
#include <vector>

#include "gridflux/common/status.h"

namespace gridflux::core::chunk {

enum class TransferProgressError {
    None,
    DuplicateRange,
    RangeOutOfBounds,
    MissingRange,
};

class TransferProgress {
   public:
    common::Status begin(std::uint64_t totalSize);
    common::Status recordFrame(std::uint64_t chunkId, std::uint64_t offset, std::uint64_t length);
    common::Status finish();

    [[nodiscard]] std::uint64_t totalSize() const noexcept;
    [[nodiscard]] std::uint64_t bytesCompleted() const noexcept;
    [[nodiscard]] TransferProgressError lastError() const noexcept;

   private:
    struct Range {
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
        std::uint64_t chunkId = 0;
    };

    std::uint64_t totalSize_ = 0;
    std::uint64_t bytesCompleted_ = 0;
    TransferProgressError lastError_ = TransferProgressError::None;
    bool started_ = false;
    std::vector<Range> ranges_;
};

}  // namespace gridflux::core::chunk
