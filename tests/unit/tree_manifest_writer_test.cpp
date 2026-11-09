#include "cpnetflux/core/tree/tree_manifest_writer.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "cpnetflux/checkpoint/manifest_save_stats.h"

namespace {
using namespace cpnetflux::core::tree;
using cpnetflux::common::Status;
using namespace std::chrono_literals;

class TreeManifestWriterTest : public ::testing::Test {
   protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
            ("cpnetflux-coalescer-" + std::to_string(::getpid()) + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        path = (root / "tree.manifest").string();
        manifest.rootLogicalPath = root.string();
        manifest.createdAtUnixNanos = 1;
        manifest.files = {{"a.bin", 1, 2, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", TreeFileStatus::Pending, ""}};
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    TreeManifestWriter::SaveSnapshot sink() {
        return [this](const TreeManifest& snapshot, std::uint64_t* bytes) {
            return saveTreeManifestAtomic(path, snapshot, bytes);
        };
    }
    std::filesystem::path root;
    std::string path;
    TreeManifest manifest;
    std::mutex mutex;
};

TEST_F(TreeManifestWriterTest, EventThresholdCoalescesChangesAndFinalBarrierPersistsLatest) {
    TreeManifestWriter writer(manifest, mutex, sink(), 32, 10s);
    {
        std::lock_guard lock(mutex);
        for (int i = 0; i < 31; ++i) ASSERT_TRUE(writer.markDirtyLocked().isOk());
        manifest.files[0].status = TreeFileStatus::Completed;
        ASSERT_TRUE(writer.markDirtyLocked().isOk());
    }
    ASSERT_TRUE(writer.finish().isOk());
    auto stats = writer.stats();
    EXPECT_EQ(stats.generation, 32U);
    EXPECT_EQ(stats.persistedGeneration, 32U);
    EXPECT_EQ(stats.snapshots, 1U);
    auto loaded = loadTreeManifest(path);
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().files[0].status, TreeFileStatus::Completed);
    EXPECT_EQ(stats.serializedBytes, std::filesystem::file_size(path));
}

TEST_F(TreeManifestWriterTest, TimerPersistsBelowThresholdWithoutFinish) {
    std::promise<void> saved;
    auto observed = saved.get_future();
    auto disk = sink();
    TreeManifestWriter writer(manifest, mutex,
        [&](const TreeManifest& snapshot, std::uint64_t* bytes) {
            auto status = disk(snapshot, bytes);
            saved.set_value();
            return status;
        }, 32, 20ms);
    {
        std::lock_guard lock(mutex);
        ASSERT_TRUE(writer.markDirtyLocked().isOk());
    }
    EXPECT_EQ(observed.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(writer.finish().isOk());
    EXPECT_EQ(writer.stats().persistedGeneration, 1U);
}

TEST_F(TreeManifestWriterTest, EventThresholdPersistsWithoutWaitingForTimerOrFinish) {
    std::promise<void> saved;
    auto observed = saved.get_future();
    auto disk = sink();
    TreeManifestWriter writer(manifest, mutex,
        [&](const TreeManifest& snapshot, std::uint64_t* bytes) {
            auto status = disk(snapshot, bytes);
            saved.set_value();
            return status;
        }, 32, 60s);
    {
        std::lock_guard lock(mutex);
        for (int i = 0; i < 32; ++i) ASSERT_TRUE(writer.markDirtyLocked().isOk());
    }
    EXPECT_EQ(observed.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(writer.finish().isOk());
}

TEST_F(TreeManifestWriterTest, SlowSaveDoesNotHoldStateLockAndCannotOverwriteNewerGeneration) {
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto released = release.get_future().share();
    auto disk = sink();
    std::atomic<unsigned> saves{0};
    TreeManifestWriter writer(manifest, mutex,
        [&](const TreeManifest& snapshot, std::uint64_t* bytes) {
            if (saves.fetch_add(1) == 0) {
                entered.set_value();
                released.wait();
            }
            return disk(snapshot, bytes);
        }, 1, 100ms);
    {
        std::lock_guard lock(mutex);
        ASSERT_TRUE(writer.markDirtyLocked().isOk());
    }
    EXPECT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);
    // Nonblocking lock attempt avoids hanging the suite on a regression.
    const bool acquired = mutex.try_lock();
    EXPECT_TRUE(acquired) << "disk I/O held the state lock";
    if (acquired) {
        manifest.files[0].status = TreeFileStatus::Completed;
        EXPECT_TRUE(writer.markDirtyLocked().isOk());
        mutex.unlock();
    }
    auto finished = std::async(std::launch::async, [&] { return writer.finish(); });
    EXPECT_EQ(finished.wait_for(20ms), std::future_status::timeout);
    release.set_value();
    ASSERT_TRUE(finished.get().isOk());
    auto loaded = loadTreeManifest(path);
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().files[0].status, TreeFileStatus::Completed);
    EXPECT_EQ(writer.stats().generation, writer.stats().persistedGeneration);
    EXPECT_EQ(saves.load(), 2U);
}

TEST_F(TreeManifestWriterTest, ConcurrentProducersKeepLatestStateAndSingleSaveOrder) {
    TreeManifestWriter writer(manifest, mutex, sink(), 32, 10ms);
    std::vector<std::thread> producers;
    for (int lane = 0; lane < 4; ++lane) {
        producers.emplace_back([&] {
            for (int event = 0; event < 128; ++event) {
                std::lock_guard lock(mutex);
                manifest.files[0].error = std::to_string(event);
                EXPECT_TRUE(writer.markDirtyLocked().isOk());
            }
        });
    }
    for (auto& producer : producers) producer.join();
    {
        std::lock_guard lock(mutex);
        manifest.files[0].error = "latest";
        ASSERT_TRUE(writer.markDirtyLocked().isOk());
    }
    ASSERT_TRUE(writer.finish().isOk());
    EXPECT_EQ(writer.stats().generation, 513U);
    EXPECT_EQ(writer.stats().persistedGeneration, 513U);
    auto loaded = loadTreeManifest(path);
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().files[0].error, "latest");
}

TEST_F(TreeManifestWriterTest, SaveFailureRemainsVisibleAtFinalBarrier) {
    // A nonempty directory cannot be replaced by the atomic manifest rename.
    std::filesystem::create_directory(path);
    TreeManifestWriter writer(manifest, mutex, sink());
    {
        std::lock_guard lock(mutex);
        ASSERT_TRUE(writer.markDirtyLocked().isOk());
    }
    auto status = writer.finish();
    EXPECT_FALSE(status.isOk());
    EXPECT_NE(status.message().find("rename tree manifest failed"), std::string::npos);
    EXPECT_EQ(writer.stats().generation, 1U);
    EXPECT_EQ(writer.stats().persistedGeneration, 0U);
    std::lock_guard lock(mutex);
    EXPECT_EQ(writer.markDirtyLocked().message(), status.message());
}

TEST_F(TreeManifestWriterTest, DestructorFlushesOutstandingState) {
    {
        TreeManifestWriter writer(manifest, mutex, sink(), 32, 60s);
        std::lock_guard lock(mutex);
        manifest.files[0].status = TreeFileStatus::Failed;
        ASSERT_TRUE(writer.markDirtyLocked().isOk());
    }
    auto loaded = loadTreeManifest(path);
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().files[0].status, TreeFileStatus::Failed);
}

TEST(TreeManifestBlockingTest, UnionsOverlappingWaitsAndIntersectsSnapshotHolds) {
    using Interval = TreeManifestProfileInterval;
    const std::vector<Interval> waits{{20, 40}, {0, 10}, {5, 20}, {100, 100}};
    const std::vector<Interval> holds{{8, 12}, {30, 35}, {32, 50}};
    auto result = measureTreeManifestBlocking(waits, holds);
    ASSERT_TRUE(result.isOk());
    EXPECT_DOUBLE_EQ(result.value().callbackWaitUnionSeconds, 40e-9);
    EXPECT_DOUBLE_EQ(result.value().snapshotHoldUnionSeconds, 24e-9);
    EXPECT_DOUBLE_EQ(result.value().callbackWaitOnSnapshotUnionSeconds, 14e-9);
    EXPECT_EQ(waits.front(), (Interval{20, 40}));
    EXPECT_TRUE(measureTreeManifestBlocking({}, {}).isOk());
    EXPECT_FALSE(measureTreeManifestBlocking({{2, 1}}, {}).isOk());
}

TEST_F(TreeManifestWriterTest, OptionalProfileAccountsCopiesDestructionAndSaveFailures) {
    for (const auto serializer : {TreeManifestSerializer::Original, TreeManifestSerializer::Buffered}) {
        cpnetflux::checkpoint::ManifestSaveStats stats;
        const auto plain = serializeTreeManifest(manifest, serializer);
        const auto profiled = serializeTreeManifest(manifest, serializer, &stats);
        ASSERT_TRUE(plain.isOk());
        ASSERT_TRUE(profiled.isOk());
        EXPECT_EQ(plain.value(), profiled.value());
        EXPECT_EQ(stats.serializeCalls, 1U);
    }
    for (const bool fail : {false, true}) {
        TreeManifestWriterProfile profile;
        cpnetflux::checkpoint::ManifestSaveStats saveStats;
        TreeManifestWriter writer(manifest, mutex,
            [&](const TreeManifest& snapshot, std::uint64_t* bytes) {
                return saveTreeManifestAtomic(fail ? (root / "missing/tree").string() : path,
                    snapshot, bytes, TreeManifestSerializer::Original, &saveStats);
            }, 32, 60s, &profile);
        {
            std::lock_guard lock(mutex);
            ASSERT_TRUE(writer.markDirtyLocked().isOk());
        }
        EXPECT_EQ(writer.finish().isOk(), !fail);
        EXPECT_EQ(profile.snapshotCopyRecords, 1U);
        EXPECT_EQ(profile.snapshotHolds.size(), 2U);
        EXPECT_FALSE(profile.truncated);
        EXPECT_GT(profile.snapshotCopySeconds, 0);
        EXPECT_GE(profile.snapshotDestroySeconds, 0);
        EXPECT_EQ(saveStats.attempts, 1U);
        EXPECT_EQ(saveStats.failures, fail ? 1U : 0U);
        EXPECT_EQ(saveStats.publishSuccesses, fail ? 0U : 1U);
    }
}

TEST(TreeManifestBlockingTest, ProfileStorageIsBoundedAndMarksTruncation) {
    TreeManifestWriterProfile profile;
    for (std::uint64_t i = 0; i < 4100; ++i) profile.recordHold(i * 2, i * 2 + 1);
    EXPECT_EQ(profile.snapshotHolds.size(), 4096U);
    EXPECT_TRUE(profile.truncated);
}
}  // namespace
