#pragma once

#include <cstdint>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/chunk/chunk_planner.h"

namespace cpnetflux::core::chunk {

struct AdaptiveRangeInputs {
    std::uint64_t fileSize = 0;
    std::uint64_t chunkSize = 0;
    std::uint32_t channels = 1;
    double rttSeconds = 0.01;
    double bandwidthBytesPerSecond = 125'000'000.0;
    double diskBytesPerSecond = 500'000'000.0;
    std::uint64_t minRangeBytes = 16ULL * 1024ULL * 1024ULL;
    std::uint64_t schedulingOverheadBytes = 4ULL * 1024ULL * 1024ULL;
};

struct AdaptiveRangePlan {
    bool striped = false;
    std::uint64_t thresholdBytes = 0;
    std::uint64_t rangeSizeBytes = 0;
    std::vector<ChunkRange> ranges;
};

common::Result<AdaptiveRangePlan> planAdaptiveRanges(const AdaptiveRangeInputs& inputs);
common::Result<std::vector<ChunkRange>> planUnifiedRanges(std::uint64_t fileSize,
    std::uint64_t chunkSize, std::uint32_t channels);

}  // namespace cpnetflux::core::chunk
