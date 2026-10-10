#include "cpnetflux/core/chunk/range_planner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "cpnetflux/core/chunk/chunk_planner.h"

namespace cpnetflux::core::chunk {
namespace {
constexpr std::uint32_t kMaxChannels = 64;

std::uint64_t saturatingMultiply(std::uint64_t left, std::uint64_t right) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}
}
common::Result<AdaptiveRangePlan> planAdaptiveRanges(const AdaptiveRangeInputs& inputs) {
    if (inputs.chunkSize == 0) return common::Status::invalidArgument("range planner chunk size must be greater than zero");
    if (inputs.channels == 0 || inputs.channels > kMaxChannels) return common::Status::invalidArgument("range planner channels must be in range 1..64");
    if (!std::isfinite(inputs.rttSeconds) || inputs.rttSeconds < 0.0 ||
        !std::isfinite(inputs.bandwidthBytesPerSecond) || inputs.bandwidthBytesPerSecond <= 0.0 ||
        !std::isfinite(inputs.diskBytesPerSecond) || inputs.diskBytesPerSecond <= 0.0)
        return common::Status::invalidArgument("range planner performance inputs are invalid");
    if (inputs.minRangeBytes == 0 || inputs.schedulingOverheadBytes == 0)
        return common::Status::invalidArgument("range planner bounds must be greater than zero");
    AdaptiveRangePlan plan;
    const auto channels = static_cast<std::uint64_t>(inputs.channels);
    const auto minRange = std::max(inputs.minRangeBytes, inputs.chunkSize);
    const auto channelWindow = saturatingMultiply(minRange, channels);
    const double bdp = inputs.bandwidthBytesPerSecond * inputs.rttSeconds;
    const double diskWindow = inputs.diskBytesPerSecond * inputs.rttSeconds;
    const double required = std::max({static_cast<double>(channelWindow),
                                      bdp + static_cast<double>(inputs.schedulingOverheadBytes),
                                      diskWindow + static_cast<double>(inputs.schedulingOverheadBytes)});
    plan.thresholdBytes = static_cast<std::uint64_t>(std::min<double>(
        required, static_cast<double>(std::numeric_limits<std::uint64_t>::max())));
    plan.thresholdBytes = std::max(plan.thresholdBytes, channelWindow);
    if (inputs.fileSize <= plan.thresholdBytes || inputs.channels == 1 || inputs.fileSize == 0)
        return plan;
    plan.striped = true;
    const auto targetRanges = channels * 2;
    const auto targetRangeSize = inputs.fileSize / targetRanges +
        (inputs.fileSize % targetRanges == 0 ? 0 : 1);
    plan.rangeSizeBytes = std::max(minRange, targetRangeSize);
    if (plan.rangeSizeBytes > inputs.chunkSize) {
        const auto remainder = plan.rangeSizeBytes % inputs.chunkSize;
        if (remainder != 0 && plan.rangeSizeBytes <= std::numeric_limits<std::uint64_t>::max() - (inputs.chunkSize - remainder))
            plan.rangeSizeBytes += inputs.chunkSize - remainder;
    }
    std::uint64_t offset = 0;
    std::uint64_t id = 1;
    while (offset < inputs.fileSize) {
        const auto length = std::min(plan.rangeSizeBytes, inputs.fileSize - offset);
        plan.ranges.push_back(ChunkRange{id++, offset, length, static_cast<std::uint32_t>((id - 2) % channels)});
        offset += length;
    }
    return plan;
}

common::Result<std::vector<ChunkRange>> planUnifiedRanges(std::uint64_t fileSize,
    std::uint64_t chunkSize, std::uint32_t channels) {
    AdaptiveRangeInputs inputs;
    inputs.fileSize = fileSize;
    inputs.chunkSize = chunkSize;
    inputs.channels = channels;
    auto adaptive = planAdaptiveRanges(inputs);
    if (!adaptive.isOk()) return adaptive.status();
    if (adaptive.value().striped) return adaptive.value().ranges;
    return planChunks(fileSize, chunkSize, channels);
}
}  // namespace cpnetflux::core::chunk
