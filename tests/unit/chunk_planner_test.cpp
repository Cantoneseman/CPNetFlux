#include "cpnetflux/core/chunk/chunk_planner.h"

#include <gtest/gtest.h>

TEST(ChunkPlannerTest, PlansExactChunks) {
    const auto result = cpnetflux::core::chunk::planChunks(4096, 1024, 2);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    ASSERT_EQ(result.value().size(), 4U);
    EXPECT_EQ(result.value()[0].offset, 0U);
    EXPECT_EQ(result.value()[0].length, 1024U);
    EXPECT_EQ(result.value()[0].streamId, 0U);
    EXPECT_EQ(result.value()[1].offset, 1024U);
    EXPECT_EQ(result.value()[1].streamId, 1U);
    EXPECT_EQ(result.value()[2].streamId, 0U);
    EXPECT_EQ(result.value()[3].streamId, 1U);
}

TEST(ChunkPlannerTest, PlansTailChunk) {
    const auto result = cpnetflux::core::chunk::planChunks(2500, 1024, 4);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    ASSERT_EQ(result.value().size(), 3U);
    EXPECT_EQ(result.value()[0].offset, 0U);
    EXPECT_EQ(result.value()[0].length, 1024U);
    EXPECT_EQ(result.value()[1].offset, 1024U);
    EXPECT_EQ(result.value()[1].length, 1024U);
    EXPECT_EQ(result.value()[2].offset, 2048U);
    EXPECT_EQ(result.value()[2].length, 452U);
    EXPECT_EQ(result.value()[2].streamId, 2U);
}

TEST(ChunkPlannerTest, HandlesFileSmallerThanChunk) {
    const auto result = cpnetflux::core::chunk::planChunks(32, 1024, 8);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    ASSERT_EQ(result.value().size(), 1U);
    EXPECT_EQ(result.value()[0].chunkId, 0U);
    EXPECT_EQ(result.value()[0].offset, 0U);
    EXPECT_EQ(result.value()[0].length, 32U);
    EXPECT_EQ(result.value()[0].streamId, 0U);
}

TEST(ChunkPlannerTest, HandlesMoreConnectionsThanChunks) {
    const auto result = cpnetflux::core::chunk::planChunks(2048, 1024, 8);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    ASSERT_EQ(result.value().size(), 2U);
    EXPECT_EQ(result.value()[0].streamId, 0U);
    EXPECT_EQ(result.value()[1].streamId, 1U);
}

TEST(ChunkPlannerTest, AssignsStreamsDeterministically) {
    const auto first = cpnetflux::core::chunk::planChunks(8192, 1024, 3);
    const auto second = cpnetflux::core::chunk::planChunks(8192, 1024, 3);

    ASSERT_TRUE(first.isOk()) << first.status().message();
    ASSERT_TRUE(second.isOk()) << second.status().message();
    ASSERT_EQ(first.value().size(), second.value().size());
    for (std::size_t index = 0; index < first.value().size(); ++index) {
        EXPECT_EQ(first.value()[index].streamId, second.value()[index].streamId);
        EXPECT_EQ(first.value()[index].streamId, index % 3);
    }
}

TEST(ChunkPlannerTest, HandlesEmptyFile) {
    const auto result = cpnetflux::core::chunk::planChunks(0, 1024, 4);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_TRUE(result.value().empty());
}

TEST(ChunkPlannerTest, RejectsInvalidInputs) {
    EXPECT_FALSE(cpnetflux::core::chunk::planChunks(1, 0, 1).isOk());
    EXPECT_FALSE(cpnetflux::core::chunk::planChunks(1, 1, 0).isOk());
    EXPECT_FALSE(cpnetflux::core::chunk::planChunks(1, 1, 65).isOk());
}
