#include "cpnetflux/core/tree/tree_manifest_writer.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "cpnetflux/checkpoint/transfer_manifest.h"

namespace cpnetflux::core::tree {

TreeManifestWriter::TreeManifestWriter(TreeManifest& manifest, std::mutex& stateMutex,
                                       SaveSnapshot save, std::uint64_t eventThreshold,
                                       std::chrono::milliseconds maxDelay)
    : manifest_(manifest), mutex_(stateMutex), save_(std::move(save)),
      threshold_(std::max<std::uint64_t>(1, eventThreshold)),
      maxDelay_(std::max(std::chrono::milliseconds(1), maxDelay)),
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
            TreeManifest snapshot = manifest_;
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
                return;
            }
            stats_.persistedGeneration = generation;
        }
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        status_ = common::Status::runtimeError("tree manifest worker failed: " + std::string(error.what()));
    } catch (...) {
        std::lock_guard lock(mutex_);
        status_ = common::Status::runtimeError("tree manifest worker failed");
    }
}

}  // namespace cpnetflux::core::tree
