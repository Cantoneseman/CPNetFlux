#include "cpnetflux/core/chunk/range_planner.h"

#include <gtest/gtest.h>

namespace {

TEST(RangePlannerTest, KeepsSmallFilesWhole) {
    cpnetflux::core::chunk::AdaptiveRangeInputs inputs;
    inputs.fileSize = 64ULL * 1024ULL * 1024ULL - 1;
    inputs.chunkSize = 1024ULL * 1024ULL;
    inputs.channels = 4;

    const auto result = cpnetflux::core::chunk::planAdaptiveRanges(inputs);
    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_FALSE(result.value().striped);
    EXPECT_TRUE(result.value().ranges.empty());
}

TEST(RangePlannerTest, CoversLargeFileWithoutOverlap) {
    cpnetflux::core::chunk::AdaptiveRangeInputs inputs;
    inputs.fileSize = 128ULL * 1024ULL * 1024ULL;
    inputs.chunkSize = 1024ULL * 1024ULL;
    inputs.channels = 4;

    const auto result = cpnetflux::core::chunk::planAdaptiveRanges(inputs);
    ASSERT_TRUE(result.isOk()) << result.status().message();
    ASSERT_TRUE(result.value().striped);
    ASSERT_FALSE(result.value().ranges.empty());
    std::uint64_t offset = 0;
    for (std::size_t index = 0; index < result.value().ranges.size(); ++index) {
        const auto& range = result.value().ranges[index];
        EXPECT_EQ(range.chunkId, index + 1);
        EXPECT_EQ(range.offset, offset);
        EXPECT_GT(range.length, 0U);
        EXPECT_LT(range.streamId, inputs.channels);
        offset += range.length;
    }
    EXPECT_EQ(offset, inputs.fileSize);
}

TEST(RangePlannerTest, ThresholdRespondsToBdpAndOverhead) {
    cpnetflux::core::chunk::AdaptiveRangeInputs fast;
    fast.fileSize = 128ULL * 1024ULL * 1024ULL;
    fast.chunkSize = 1024ULL * 1024ULL;
    fast.channels = 4;
    fast.rttSeconds = 0.001;
    fast.bandwidthBytesPerSecond = 10ULL * 1024ULL * 1024ULL;

    auto slow = fast;
    slow.rttSeconds = 0.2;
    slow.bandwidthBytesPerSecond = 500ULL * 1024ULL * 1024ULL;
    slow.schedulingOverheadBytes = 32ULL * 1024ULL * 1024ULL;

    const auto fastPlan = cpnetflux::core::chunk::planAdaptiveRanges(fast);
    const auto slowPlan = cpnetflux::core::chunk::planAdaptiveRanges(slow);
    ASSERT_TRUE(fastPlan.isOk()) << fastPlan.status().message();
    ASSERT_TRUE(slowPlan.isOk()) << slowPlan.status().message();
    EXPECT_LT(fastPlan.value().thresholdBytes, slowPlan.value().thresholdBytes);
}

TEST(RangePlannerTest, RejectsInvalidInputs) {
    cpnetflux::core::chunk::AdaptiveRangeInputs inputs;
    inputs.fileSize = 1;
    inputs.chunkSize = 0;
    EXPECT_FALSE(cpnetflux::core::chunk::planAdaptiveRanges(inputs).isOk());
    inputs.chunkSize = 1;
    inputs.channels = 0;
    EXPECT_FALSE(cpnetflux::core::chunk::planAdaptiveRanges(inputs).isOk());
}

TEST(RangePlannerTest, UnifiedPlannerUsesAdaptiveRangesOnlyAboveThreshold) {
    const auto small = cpnetflux::core::chunk::planUnifiedRanges(8ULL * 1024ULL * 1024ULL,
        1024ULL * 1024ULL, 4);
    ASSERT_TRUE(small.isOk()) << small.status().message();
    ASSERT_EQ(small.value().size(), 8U);
    EXPECT_EQ(small.value().front().offset, 0U);
    EXPECT_EQ(small.value().front().streamId, 0U);

    const auto large = cpnetflux::core::chunk::planUnifiedRanges(128ULL * 1024ULL * 1024ULL,
        1024ULL * 1024ULL, 4);
    ASSERT_TRUE(large.isOk()) << large.status().message();
    ASSERT_FALSE(large.value().empty());
    EXPECT_GT(large.value().front().length, 1024ULL * 1024ULL);
    EXPECT_EQ(large.value().front().chunkId, 1U);
}

}  // namespace
