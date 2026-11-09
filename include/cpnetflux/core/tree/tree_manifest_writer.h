#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "cpnetflux/core/tree/tree_manifest.h"

namespace cpnetflux::core::tree {

using TreeManifestProfileInterval = std::pair<std::uint64_t, std::uint64_t>;
[[nodiscard]] std::uint64_t treeManifestMonotonicNanoseconds();

struct TreeManifestBlocking {
    double callbackWaitUnionSeconds = 0, snapshotHoldUnionSeconds = 0;
    double callbackWaitOnSnapshotUnionSeconds = 0;
};
[[nodiscard]] common::Result<TreeManifestBlocking> measureTreeManifestBlocking(
    std::vector<TreeManifestProfileInterval> waits, std::vector<TreeManifestProfileInterval> holds);

// Caller reads after finish(); only the worker mutates this bounded profile.
struct TreeManifestWriterProfile {
    std::uint64_t snapshotCopyRecords = 0;
    double snapshotCopySeconds = 0, snapshotDestroySeconds = 0;
    bool truncated = false;
    std::vector<TreeManifestProfileInterval> snapshotHolds;
    void recordHold(std::uint64_t start, std::uint64_t end);
};

struct TreeManifestWriteStats {
    std::uint64_t generation = 0;
    std::uint64_t persistedGeneration = 0;
    std::uint64_t snapshots = 0;
    std::uint64_t serializedBytes = 0;
    double writeSeconds = 0;
    double stateLockWaitSeconds = 0;
};

// The caller owns the manifest and state mutex, which must outlive this writer.
// All mutations (including dirty marking) must hold that mutex. Only the worker
// saves snapshots; finish() is a terminal barrier, called after producers stop.
class TreeManifestWriter {
   public:
    using SaveSnapshot = std::function<common::Status(const TreeManifest&, std::uint64_t*)>;

    TreeManifestWriter(TreeManifest& manifest, std::mutex& stateMutex, SaveSnapshot save,
                       std::uint64_t eventThreshold = 32,
                       std::chrono::milliseconds maxDelay = std::chrono::milliseconds(100),
                       TreeManifestWriterProfile* profile = nullptr);
    ~TreeManifestWriter();
    TreeManifestWriter(const TreeManifestWriter&) = delete;
    TreeManifestWriter& operator=(const TreeManifestWriter&) = delete;

    [[nodiscard]] common::Status markDirtyLocked();
    [[nodiscard]] common::Status finish();
    [[nodiscard]] TreeManifestWriteStats stats() const;

   private:
    void run();

    TreeManifest& manifest_;
    std::mutex& mutex_;
    SaveSnapshot save_;
    const std::uint64_t threshold_;
    const std::chrono::milliseconds maxDelay_;
    std::condition_variable changed_;
    TreeManifestWriteStats stats_;
    std::uint64_t capturedGeneration_ = 0;
    std::chrono::steady_clock::time_point dirtySince_;
    common::Status status_;
    bool stopping_ = false;
    TreeManifestWriterProfile* profile_;
    std::thread worker_;
};

}  // namespace cpnetflux::core::tree
