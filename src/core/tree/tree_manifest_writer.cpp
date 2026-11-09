#include "cpnetflux/core/tree/tree_manifest_writer.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "cpnetflux/checkpoint/transfer_manifest.h"

namespace cpnetflux::core::tree {

std::uint64_t treeManifestMonotonicNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void TreeManifestWriterProfile::recordHold(std::uint64_t start, std::uint64_t end) {
    if (snapshotHolds.size() == 4096) { truncated = true; return; }
    snapshotHolds.emplace_back(start, end);
}

common::Result<TreeManifestBlocking> measureTreeManifestBlocking(
    std::vector<TreeManifestProfileInterval> waits, std::vector<TreeManifestProfileInterval> holds) {
    auto merge = [](auto& intervals) {
        for (const auto& [start, end] : intervals) if (end < start) return false;
        std::sort(intervals.begin(), intervals.end());
        std::size_t used = 0;
        for (const auto interval : intervals) {
            if (interval.first == interval.second) continue;
            if (used && intervals[used - 1].second >= interval.first)
                intervals[used - 1].second = std::max(intervals[used - 1].second, interval.second);
            else intervals[used++] = interval;
        }
        intervals.resize(used);
        return true;
    };
    if (!merge(waits) || !merge(holds))
        return common::Status::invalidArgument("tree manifest profile interval runs backwards");
    std::uint64_t waitNanos = 0, holdNanos = 0, overlapNanos = 0;
    for (const auto& [start, end] : waits) waitNanos += end - start;
    for (const auto& [start, end] : holds) holdNanos += end - start;
    for (std::size_t a = 0, b = 0; a < waits.size() && b < holds.size();) {
        const auto start = std::max(waits[a].first, holds[b].first);
        const auto end = std::min(waits[a].second, holds[b].second);
        if (end > start) overlapNanos += end - start;
        if (waits[a].second < holds[b].second) ++a; else ++b;
    }
    return TreeManifestBlocking{waitNanos / 1e9, holdNanos / 1e9, overlapNanos / 1e9};
}

TreeManifestWriter::TreeManifestWriter(TreeManifest& manifest, std::mutex& stateMutex,
                                       SaveSnapshot save, std::uint64_t eventThreshold,
                                       std::chrono::milliseconds maxDelay,
                                       TreeManifestWriterProfile* profile)
    : manifest_(manifest), mutex_(stateMutex), save_(std::move(save)),
      threshold_(std::max<std::uint64_t>(1, eventThreshold)),
      maxDelay_(std::max(std::chrono::milliseconds(1), maxDelay)),
      profile_(profile),
      worker_([this] { run(); }) {}

TreeManifestWriter::~TreeManifestWriter() {
    (void)finish();
}

common::Status TreeManifestWriter::markDirtyLocked() {
    if (stopping_) return !status_.isOk() ? status_ :
        common::Status::runtimeError("tree manifest writer already stopped");
    if (stats_.generation == capturedGeneration_) dirtySince_ = std::chrono::steady_clock::now();
    ++stats_.generation;
    manifest_.updatedAtUnixNanos = checkpoint::nowUnixNanos();
    changed_.notify_one();
    return status_;
}

common::Status TreeManifestWriter::finish() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        changed_.notify_one();
    }
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_);
    return status_;
}

TreeManifestWriteStats TreeManifestWriter::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

void TreeManifestWriter::run() {
    try {
        std::unique_lock lock(mutex_);
        for (;;) {
            changed_.wait(lock, [&] { return stopping_ || stats_.generation != capturedGeneration_; });
            if (stats_.generation == capturedGeneration_) {
                if (stopping_) return;
                continue;
            }
            const auto deadline = dirtySince_ + maxDelay_;
            changed_.wait_until(lock, deadline, [&] {
                return stopping_ || stats_.generation - capturedGeneration_ >= threshold_;
            });
            std::uint64_t destroyStarted = 0;
            {
                const auto copyStarted = profile_ ? treeManifestMonotonicNanoseconds() : 0;
                TreeManifest snapshot = manifest_;
                if (profile_) {
                    const auto copied = treeManifestMonotonicNanoseconds();
                    profile_->snapshotCopyRecords += snapshot.files.size();
                    profile_->snapshotCopySeconds += (copied - copyStarted) / 1e9;
                    profile_->recordHold(copyStarted, copied);
                }
                const auto generation = stats_.generation;
                capturedGeneration_ = generation;
                lock.unlock();
                const auto started = std::chrono::steady_clock::now();
                std::uint64_t bytes = 0;
                auto saved = save_(snapshot, &bytes);
                const auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count();
                const auto acquiring = std::chrono::steady_clock::now();
                lock.lock();
                stats_.stateLockWaitSeconds += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - acquiring).count();
                stats_.writeSeconds += elapsed;
                stats_.serializedBytes += bytes;
                ++stats_.snapshots;
                if (!saved.isOk()) {
                    status_ = saved;
                } else {
                    stats_.persistedGeneration = generation;
                }
                if (profile_) destroyStarted = treeManifestMonotonicNanoseconds();
            }  // Snapshot destruction retains the original state-lock ownership.
            if (profile_) {
                const auto destroyed = treeManifestMonotonicNanoseconds();
                profile_->snapshotDestroySeconds += (destroyed - destroyStarted) / 1e9;
                profile_->recordHold(destroyStarted, destroyed);
            }
            if (!status_.isOk()) return;
        }
    } catch (const std::exception& error) {
        if (profile_) profile_->truncated = true;
        std::lock_guard lock(mutex_);
        status_ = common::Status::runtimeError("tree manifest worker failed: " + std::string(error.what()));
    } catch (...) {
        if (profile_) profile_->truncated = true;
        std::lock_guard lock(mutex_);
        status_ = common::Status::runtimeError("tree manifest worker failed");
    }
}

}  // namespace cpnetflux::core::tree
