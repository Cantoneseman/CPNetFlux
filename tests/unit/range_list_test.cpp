#include "cpnetflux/core/chunk/range_list.h"

#include <gtest/gtest.h>

TEST(RangeListTest, InsertsAdjacentRangesAndReportsMissingGaps) {
    cpnetflux::core::chunk::RangeList ranges;

    EXPECT_TRUE(ranges.insert(0, 1024, 4096).isOk());
    EXPECT_TRUE(ranges.insert(1024, 1024, 4096).isOk());
    EXPECT_TRUE(ranges.insert(3072, 1024, 4096).isOk());

    ASSERT_EQ(ranges.ranges().size(), 2U);
    EXPECT_EQ(ranges.ranges()[0].begin, 0U);
    EXPECT_EQ(ranges.ranges()[0].end, 2048U);
    EXPECT_EQ(ranges.bytesCompleted(), 3072U);

    const auto missing = ranges.missingRanges(4096);
    ASSERT_EQ(missing.size(), 1U);
    EXPECT_EQ(missing[0].begin, 2048U);
    EXPECT_EQ(missing[0].end, 3072U);
}

TEST(RangeListTest, RejectsDuplicateAndOverlapOnStrictInsert) {
    cpnetflux::core::chunk::RangeList ranges;

    EXPECT_TRUE(ranges.insert(0, 1024, 4096).isOk());
    EXPECT_FALSE(ranges.insert(0, 1024, 4096).isOk());
    EXPECT_FALSE(ranges.insert(512, 1024, 4096).isOk());
}

TEST(RangeListTest, MergesOutOfOrderOverlapAndAdjacentRanges) {
    cpnetflux::core::chunk::RangeList ranges;

    EXPECT_TRUE(ranges.merge(2048, 1024, 4096).isOk());
    EXPECT_TRUE(ranges.merge(0, 1024, 4096).isOk());
    EXPECT_TRUE(ranges.merge(1024, 1024, 4096).isOk());
    EXPECT_TRUE(ranges.merge(512, 512, 4096).isOk());

    ASSERT_EQ(ranges.ranges().size(), 1U);
    EXPECT_EQ(ranges.ranges()[0].begin, 0U);
    EXPECT_EQ(ranges.ranges()[0].end, 3072U);
    EXPECT_EQ(ranges.bytesCompleted(), 3072U);
}

TEST(RangeListTest, RejectsInvalidRanges) {
    cpnetflux::core::chunk::RangeList ranges;

    EXPECT_FALSE(ranges.insert(0, 0, 4096).isOk());
    EXPECT_FALSE(ranges.insert(4090, 16, 4096).isOk());
    EXPECT_FALSE(ranges.merge(4096, 1, 4096).isOk());
}

TEST(RangeListTest, HandlesEmptyTotalSize) {
    cpnetflux::core::chunk::RangeList ranges;

    EXPECT_TRUE(ranges.missingRanges(0).empty());
    EXPECT_FALSE(ranges.insert(0, 1, 0).isOk());
}
