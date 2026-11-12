#pragma once

#include "cpnetflux/core/io/persistent_tree_transfer.h"

#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace cpnetflux::core::io::detail {

// One lane can have multiple outstanding FILE_RESULTs. Its socket outlives this guard.
class PersistentTransferLifetime {
 public:
    PersistentTransferLifetime(FramedDataSocket* socket, PersistentTransferLimits limits)
        : socket_(socket), limits_(limits), watcher_([this] { watch(); }) {}
    ~PersistentTransferLifetime() {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            changed_.notify_all();
        }
        watcher_.join();
    }
    PersistentTransferLifetime(const PersistentTransferLifetime&) = delete;
    PersistentTransferLifetime& operator=(const PersistentTransferLifetime&) = delete;

    [[nodiscard]] WorkClock::time_point beginResult(const PersistentFileIdentity& file) {
        std::lock_guard lock(mutex_);
        const auto now = WorkClock::now();
        const auto maximum = std::chrono::duration_cast<std::chrono::milliseconds>(
            WorkClock::time_point::max() - now);
        if (limits_.resultTimeout.count() <= 0 || limits_.resultTimeout >= maximum) {
            failure_ = common::Status::invalidArgument("invalid V3 result timeout");
            changed_.notify_all();
            return now;
        }
        auto deadline = now + limits_.resultTimeout;
        if (limits_.taskDeadline) deadline = std::min(deadline, *limits_.taskDeadline);
        const auto [entry, inserted] = results_.try_emplace(
            std::make_pair(file.fileId, file.rangeId), deadline);
        (void)inserted;
        changed_.notify_all();
        return entry->second;
    }
    void finishResult(const PersistentFileIdentity& file) {
        std::lock_guard lock(mutex_);
        checkExpired();
        results_.erase(std::make_pair(file.fileId, file.rangeId));
        changed_.notify_all();
    }
    [[nodiscard]] common::Status status() {
        std::lock_guard lock(mutex_);
        checkExpired();
        if (!failure_.isOk()) changed_.notify_all();
        return failure_;
    }

 private:
    void checkExpired() {
        if (!failure_.isOk()) return;
        const auto now = WorkClock::now();
        if (limits_.taskDeadline && now >= *limits_.taskDeadline) {
            failure_ = common::Status::runtimeError("transfer total deadline exceeded");
            return;
        }
        for (const auto& [key, deadline] : results_) {
            (void)key;
            if (now >= deadline) {
                failure_ = common::Status::runtimeError("V3 result/commit deadline exceeded");
                return;
            }
        }
    }
    void watch() {
        std::unique_lock lock(mutex_);
        while (!stopped_) {
            checkExpired();
            if (!failure_.isOk()) {
                lock.unlock();
                socket_->shutdown();
                return;
            }
            SocketDeadline next = limits_.taskDeadline;
            for (const auto& [key, deadline] : results_) {
                (void)key;
                if (!next || deadline < *next) next = deadline;
            }
            if (next) changed_.wait_until(lock, *next);
            else changed_.wait(lock);
        }
    }
    FramedDataSocket* socket_;
    PersistentTransferLimits limits_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::pair<std::uint32_t, std::uint64_t>, WorkClock::time_point> results_;
    common::Status failure_ = common::Status::ok();
    bool stopped_ = false;
    std::thread watcher_;
};

}  // namespace cpnetflux::core::io::detail
