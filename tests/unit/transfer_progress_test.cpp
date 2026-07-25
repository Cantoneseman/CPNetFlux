#include "gridflux/core/chunk/transfer_progress.h"

#include <gtest/gtest.h>

TEST(TransferProgressTest, TracksCompleteRanges) {
    gridflux::core::chunk::TransferProgress progress;

    EXPECT_TRUE(progress.begin(4096).isOk());
    EXPECT_TRUE(progress.recordFrame(0, 0, 1024).isOk());
    EXPECT_TRUE(progress.recordFrame(1, 1024, 1024).isOk());
    EXPECT_TRUE(progress.recordFrame(2, 2048, 2048).isOk());

    EXPECT_EQ(progress.bytesCompleted(), 4096U);
    EXPECT_TRUE(progress.finish().isOk());
}

TEST(TransferProgressTest, HandlesTailRange) {
    gridflux::core::chunk::TransferProgress progress;

    EXPECT_TRUE(progress.begin(2500).isOk());
    EXPECT_TRUE(progress.recordFrame(0, 0, 1024).isOk());
    EXPECT_TRUE(progress.recordFrame(1, 1024, 1024).isOk());
    EXPECT_TRUE(progress.recordFrame(2, 2048, 452).isOk());

    EXPECT_TRUE(progress.finish().isOk());
}

TEST(TransferProgressTest, RejectsDuplicateRange) {
    gridflux::core::chunk::TransferProgress progress;

    EXPECT_TRUE(progress.begin(2048).isOk());
    EXPECT_TRUE(progress.recordFrame(0, 0, 1024).isOk());
    EXPECT_FALSE(progress.recordFrame(0, 0, 1024).isOk());
    EXPECT_EQ(progress.lastError(), gridflux::core::chunk::TransferProgressError::DuplicateRange);
}

TEST(TransferProgressTest, RejectsOverlappingRange) {
    gridflux::core::chunk::TransferProgress progress;

    EXPECT_TRUE(progress.begin(2048).isOk());
    EXPECT_TRUE(progress.recordFrame(0, 0, 1024).isOk());
    EXPECT_FALSE(progress.recordFrame(1, 512, 1024).isOk());
    EXPECT_EQ(progress.lastError(), gridflux::core::chunk::TransferProgressError::DuplicateRange);
}

TEST(TransferProgressTest, RejectsOutOfBoundsRange) {
    gridflux::core::chunk::TransferProgress progress;

    EXPECT_TRUE(progress.begin(1024).isOk());
    EXPECT_FALSE(progress.recordFrame(0, 1020, 8).isOk());
    EXPECT_EQ(progress.lastError(), gridflux::core::chunk::TransferProgressError::RangeOutOfBounds);
}

TEST(TransferProgressTest, RejectsZeroLengthDataRange) {
    gridflux::core::chunk::TransferProgress progress;

    EXPECT_TRUE(progress.begin(1024).isOk());
    EXPECT_FALSE(progress.recordFrame(0, 0, 0).isOk());
    EXPECT_EQ(progress.lastError(), gridflux::core::chunk::TransferProgressError::RangeOutOfBounds);
}

TEST(TransferProgressTest, DetectsMissingRangeAtFinish) {
    gridflux::core::chunk::TransferProgress progress;

    EXPECT_TRUE(progress.begin(4096).isOk());
    EXPECT_TRUE(progress.recordFrame(0, 0, 1024).isOk());
    EXPECT_TRUE(progress.recordFrame(2, 2048, 2048).isOk());

    EXPECT_FALSE(progress.finish().isOk());
    EXPECT_EQ(progress.lastError(), gridflux::core::chunk::TransferProgressError::MissingRange);
}

TEST(TransferProgressTest, HandlesEmptyTransfer) {
    gridflux::core::chunk::TransferProgress progress;

    EXPECT_TRUE(progress.begin(0).isOk());
    EXPECT_EQ(progress.bytesCompleted(), 0U);
    EXPECT_TRUE(progress.finish().isOk());
}
