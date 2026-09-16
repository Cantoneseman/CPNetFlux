#include "cpnetflux/core/scheduler/work_item_planner.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

cpnetflux::core::tree::TreeFileRecord makeRecord(const std::string& relativePath,
                                                std::uint64_t size,
                                                cpnetflux::core::tree::TreeFileStatus status,
                                                const std::string& transferId) {
    return cpnetflux::core::tree::TreeFileRecord{relativePath, size, 0, transferId, status, ""};
}

}  // namespace

TEST(SchedulerWorkItemTest, PlansLargeFileIntoRangeWorkItems) {
    cpnetflux::core::scheduler::WorkItemPlannerConfig config;
    config.minBytes = 64ULL * 1024ULL * 1024ULL;
    config.maxBytes = 256ULL * 1024ULL * 1024ULL;
    config.resumeGeneration = 42;
    config.direction = cpnetflux::core::scheduler::SchedulerDirection::Upload;
    config.taskId = "task-1";
    cpnetflux::core::scheduler::WorkItemPlanner planner(config);

    cpnetflux::core::scheduler::CompressionDecision compression;
    auto items = planner.planFile("file-1", "nested/big.bin", 512ULL * 1024ULL * 1024ULL,
                                  cpnetflux::checksum::ChecksumAlgorithm::Crc32c, compression, 2);
    ASSERT_TRUE(items.isOk()) << items.status().message();
    ASSERT_EQ(items.value().size(), 2U);

    const auto& first = items.value()[0];
    EXPECT_EQ(first.taskId, "task-1");
    EXPECT_EQ(first.fileId, "file-1");
    EXPECT_EQ(first.relativePath, "nested/big.bin");
    EXPECT_EQ(first.offset, 0U);
    EXPECT_EQ(first.length, 256ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(first.direction, cpnetflux::core::scheduler::SchedulerDirection::Upload);
    EXPECT_EQ(first.resumeGeneration, 42U);
    EXPECT_EQ(first.retryCount, 2U);
    EXPECT_TRUE(first.dispatchable);

    const auto& second = items.value()[1];
    EXPECT_EQ(second.offset, 256ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(second.length, 256ULL * 1024ULL * 1024ULL);
    EXPECT_FALSE(second.dispatchable);
}

TEST(SchedulerWorkItemTest, PlansManifestAndSkipsCompletedFiles) {
    cpnetflux::core::scheduler::WorkItemPlannerConfig config;
    config.minBytes = 64ULL * 1024ULL * 1024ULL;
    config.maxBytes = 256ULL * 1024ULL * 1024ULL;
    config.resumeGeneration = 7;
    config.direction = cpnetflux::core::scheduler::SchedulerDirection::Download;
    config.taskId = "task-2";
    cpnetflux::core::scheduler::WorkItemPlanner planner(config);

    cpnetflux::core::tree::TreeManifest manifest;
    manifest.checksumAlgorithm = cpnetflux::checksum::ChecksumAlgorithm::Crc32c;
    manifest.files.push_back(
        makeRecord("alpha.txt", 1024, cpnetflux::core::tree::TreeFileStatus::Pending, "file-a"));
    manifest.files.push_back(makeRecord("beta.txt", 2048, cpnetflux::core::tree::TreeFileStatus::Completed,
                                        "file-b"));
    manifest.files.push_back(
        makeRecord("gamma.txt", 4096, cpnetflux::core::tree::TreeFileStatus::Changed, "file-c"));

    auto plans = planner.planManifest(manifest, "task-2",
                                      cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                      cpnetflux::core::scheduler::CompressionDecision{}, 7);
    ASSERT_TRUE(plans.isOk()) << plans.status().message();
    ASSERT_EQ(plans.value().size(), 1U);
    EXPECT_EQ(plans.value()[0].manifestIndex, 0U);
    EXPECT_EQ(plans.value()[0].relativePath, "alpha.txt");
    ASSERT_EQ(plans.value()[0].workItems.size(), 1U);
    EXPECT_EQ(plans.value()[0].workItems[0].offset, 0U);
    EXPECT_EQ(plans.value()[0].workItems[0].length, 1024ULL);
}
