#include "cpnetflux/core/io/dynamic_file_queue.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include "cpnetflux/core/scheduler/work_item.h"
#include <unordered_set>
#include <string>
#include "cpnetflux/core/tree/tree_scan.h"

namespace cpnetflux::core::io {
bool samePersistentIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept {
    return a.fileId == b.fileId && a.generation == b.generation && a.totalSize == b.totalSize &&
        a.relativePath == b.relativePath && a.transferId == b.transferId &&
        a.chunkSize == b.chunkSize && a.mtimeUnixSeconds == b.mtimeUnixSeconds &&
        a.checksumAlgorithm == b.checksumAlgorithm &&
        a.rangeId == b.rangeId &&
        a.rangeOffset == b.rangeOffset && a.rangeLength == b.rangeLength &&
        a.rangeCount == b.rangeCount && a.attempt == b.attempt;
}
bool samePersistentRangeIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept {
    return samePersistentIdentity(a, b);
}
static bool samePersistentLeaseIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept {
    return samePersistentIdentity(a,b) && a.checksum.algorithm == b.checksum.algorithm &&
        a.checksum.value == b.checksum.value;
}
static std::string activeKey(const PersistentFileIdentity& file) {
    return std::to_string(file.fileId) + ":" + std::to_string(file.generation) + ":" +
        std::to_string(file.rangeId) + ":" + std::to_string(file.attempt) + ":" +
        std::to_string(file.transferId.size()) + ":" + file.transferId + ":" +
        std::to_string(file.relativePath.size()) + ":" + file.relativePath;
}

namespace {
std::uint64_t acquireLeaseId() {
    static std::atomic<std::uint64_t> next{1};
    auto id = next.load(std::memory_order_relaxed);
    while (id != UINT64_MAX) {
        if (next.compare_exchange_weak(id, id + 1, std::memory_order_relaxed)) return id;
    }
    return 0;
}
bool validWorkItem(const WorkItem& item) {
    const auto& file = item.file;
    return !item.taskId.empty() && file.fileId && file.generation && file.chunkSize &&
        !file.transferId.empty() && tree::validateTreeRelativePath(file.relativePath).isOk() &&
        file.totalSize <= static_cast<std::uint64_t>(INT64_MAX) &&
        (file.checksumAlgorithm == checksum::ChecksumAlgorithm::None ||
         file.checksumAlgorithm == checksum::ChecksumAlgorithm::Crc32c) &&
        (file.rangeId ? file.rangeCount && file.rangeId <= file.rangeCount && file.rangeLength &&
            file.rangeOffset <= file.totalSize && file.rangeLength <= file.totalSize - file.rangeOffset :
            !file.rangeOffset && !file.rangeLength && !file.rangeCount);
}
std::vector<WorkItem> asWorkItems(std::vector<PersistentFileIdentity> files) {
    std::vector<WorkItem> work;
    work.reserve(files.size());
    for (auto& file : files) {
        auto task = file.transferId;
        work.push_back({std::move(file), std::move(task)});
    }
    return work;
}
bool deadlineFits(WorkClock::time_point now, std::chrono::milliseconds timeout) {
    return timeout.count() > 0 &&
        timeout <= std::chrono::duration_cast<std::chrono::milliseconds>(WorkClock::duration::max()) &&
        now <= WorkClock::time_point::max() - std::chrono::duration_cast<WorkClock::duration>(timeout);
}
common::Status mismatch() {
    return common::Status::invalidArgument("work lease identity/owner/epoch/state mismatch");
}
}
DynamicFileQueue::DynamicFileQueue(std::vector<PersistentFileIdentity> files,
    std::uint32_t channels, std::uint32_t window, std::size_t capacity)
    : DynamicFileQueue(asWorkItems(std::move(files)), channels, window, capacity) {}

DynamicFileQueue::DynamicFileQueue(std::vector<WorkItem> files,
    std::uint32_t channels, std::uint32_t window, std::size_t capacity)
    : files_(std::move(files)), window_(window) {
    if (channels == 0 || channels > 64 || window == 0 || window > 16 || capacity == 0 || capacity > 256) {
        status_ = common::Status::invalidArgument("invalid dynamic queue bounds");
        return;
    }
    ring_.resize(capacity);
    credits_.resize(channels);
    for (std::uint32_t c = 0; c < channels; ++c) workers_.push_back({WorkerContext{c}});
    std::unordered_map<std::uint32_t, std::vector<PersistentFileIdentity>> groups;
    std::unordered_map<std::string, std::uint32_t> paths, transfers;
    std::unordered_map<std::uint32_t, std::string> taskIds;
    for (const auto& item : files_) {
        const auto& file = item.file;
        if (!validWorkItem(item) ||
            (paths.contains(file.relativePath) && paths[file.relativePath] != file.fileId) ||
            (transfers.contains(file.transferId) && transfers[file.transferId] != file.fileId) ||
            (taskIds.contains(file.fileId) && taskIds[file.fileId] != item.taskId)) {
            status_ = common::Status::invalidArgument("invalid or aliased dynamic work identity");
            return;
        }
        paths[file.relativePath] = transfers[file.transferId] = file.fileId;
        taskIds[file.fileId] = item.taskId;
        groups[file.fileId].push_back(file);
    }
    for (auto& [fileId, ranges] : groups) {
        auto base = ranges.front();
        base.rangeId = base.rangeOffset = base.rangeLength = base.rangeCount = base.attempt = 0;
        if (ranges.front().rangeId == 0) {
            if (ranges.size() != 1) {
                status_ = common::Status::invalidArgument("duplicate whole-file identity"); return;
            }
            continue;
        }
        std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
            return a.rangeOffset < b.rangeOffset;
        });
        std::unordered_set<std::uint64_t> ids;
        std::uint64_t nextOffset = 0;
        for (const auto& file : ranges) {
            auto candidate = file;
            candidate.rangeId = candidate.rangeOffset = candidate.rangeLength =
                candidate.rangeCount = candidate.attempt = 0;
            if (!samePersistentLeaseIdentity(base, candidate) || !file.rangeId ||
                file.rangeCount != ranges.size() || file.rangeId > file.rangeCount ||
                !ids.insert(file.rangeId).second || !file.rangeLength ||
                file.rangeOffset != nextOffset || file.rangeOffset > file.totalSize ||
                file.rangeLength > file.totalSize - file.rangeOffset) {
                status_ = common::Status::invalidArgument("ranges do not partition one stable file"); return;
            }
            nextOffset += file.rangeLength;
        }
        if (nextOffset != base.totalSize) {
            status_ = common::Status::invalidArgument("incomplete range partition"); return;
        }
    }
    std::stable_sort(files_.begin(), files_.end(), [](const auto& a, const auto& b) {
        return a.file.totalSize != b.file.totalSize ? a.file.totalSize > b.file.totalSize : a.file.fileId < b.file.fileId;
    });
    fill();
}
void DynamicFileQueue::fill() {
    while (size_ < ring_.size() && next_ < files_.size()) {
        ring_[tail_] = Ready{std::move(files_[next_++]), WorkClock::now()};
        tail_ = (tail_ + 1) % ring_.size();
        ++size_;
        high_ = std::max(high_, size_);
    }
}
bool DynamicFileQueue::matchesWorker(const WorkerContext& worker) const {
    return worker.workerId < workers_.size() && workers_[worker.workerId].context == worker;
}
common::Result<const DynamicFileQueue::Active*> DynamicFileQueue::find(const WorkLease& lease) const {
    if (!status_.isOk()) return status_;
    const auto found = active_.find(activeKey(lease.file));
    if (!matchesWorker(lease.owner) || found == active_.end() || found->second.lease.leaseId != lease.leaseId ||
        found->second.lease.owner != lease.owner || found->second.lease.taskId != lease.taskId ||
        !samePersistentLeaseIdentity(found->second.lease.file, lease.file)) return mismatch();
    return &found->second;
}
common::Result<DynamicFileQueue::Active*> DynamicFileQueue::find(const WorkLease& lease) {
    auto found = static_cast<const DynamicFileQueue*>(this)->find(lease);
    if (!found.isOk()) return found.status();
    return const_cast<Active*>(found.value());
}
common::Result<WorkerContext> DynamicFileQueue::worker(std::uint32_t channel) const {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (channel >= workers_.size()) return mismatch();
    return workers_[channel].context;
}
common::Result<std::optional<WorkLease>> DynamicFileQueue::claim(const WorkerContext& owner,
    WorkClock::time_point now, std::chrono::milliseconds timeout) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!matchesWorker(owner) || workers_[owner.workerId].state != WorkerState::Accepting ||
        !deadlineFits(now, timeout)) return mismatch();
    if (credits_[owner.workerId] >= window_) return common::Status::runtimeError("dynamic channel credit exhausted");
    if (!size_ && retry_.empty()) return std::optional<WorkLease>{};
    const auto leaseId = acquireLeaseId();
    if (!leaseId) return common::Status::runtimeError("work lease ID exhausted");
    Ready ready;
    if (!retry_.empty()) {
        ready = std::move(retry_.front()); retry_.pop_front();
    } else {
        ready = std::move(*ring_[head_]); ring_[head_].reset();
        head_ = (head_ + 1) % ring_.size(); --size_;
    }
    WorkLease lease;
    static_cast<WorkItem&>(lease) = std::move(ready.item);
    lease.leaseId = leaseId;
    lease.owner = owner;
    lease.deadline = now + timeout;
    // Admission uses wall-monotonic time even when a test supplies a logical deadline clock.
    lease.queueWaitSeconds = std::max(0.0, std::chrono::duration<double>(WorkClock::now() - ready.admitted).count());
    const auto inserted = active_.emplace(activeKey(lease.file), Active{lease, timeout}).second;
    if (!inserted) return common::Status::runtimeError("duplicate active work item");
    ++credits_[owner.workerId]; fill();
    return std::optional<WorkLease>{std::move(lease)};
}
common::Result<std::optional<DynamicFileLease>> DynamicFileQueue::claim(std::uint32_t channel) {
    auto owner = worker(channel);
    if (!owner.isOk()) return owner.status();
    if (owner.value().epoch.value != 1) return mismatch();
    return claim(owner.value());
}
common::Result<WorkLease> DynamicFileQueue::legacyLease(std::uint32_t channel,
    const PersistentFileIdentity& file) const {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    const auto found = active_.find(activeKey(file));
    if (found == active_.end() || found->second.lease.owner.workerId != channel ||
        found->second.lease.owner.epoch.value != 1 || !samePersistentLeaseIdentity(found->second.lease.file, file)) return mismatch();
    return found->second.lease;
}
common::Status DynamicFileQueue::markInFlight(std::uint32_t channel, const PersistentFileIdentity& file) {
    auto lease = legacyLease(channel, file); return lease.isOk() ? begin(lease.value()) : lease.status();
}
common::Status DynamicFileQueue::markCommitPending(std::uint32_t channel, const PersistentFileIdentity& file) {
    auto lease = legacyLease(channel, file); return lease.isOk() ? awaitResult(lease.value()) : lease.status();
}
common::Status DynamicFileQueue::complete(std::uint32_t channel, const PersistentFileIdentity& file) {
    auto lease = legacyLease(channel, file); return lease.isOk() ? commit(lease.value()) : lease.status();
}

common::Status DynamicFileQueue::begin(const WorkLease& lease, WorkClock::time_point now) {
    std::lock_guard lock(mutex_);
    auto found = find(lease); if (!found.isOk()) return found.status();
    auto& current = found.value()->lease;
    if (current.state != WorkLeaseState::Claimed || now >= current.deadline) return mismatch();
    current.state = WorkLeaseState::InFlight;
    return common::Status::ok();
}
common::Status DynamicFileQueue::startIo(const WorkLease& lease, std::uint64_t bytes,
    WorkClock::time_point now) {
    std::lock_guard lock(mutex_);
    auto found = find(lease); if (!found.isOk()) return found.status();
    auto& current = found.value()->lease;
    if (current.state != WorkLeaseState::InFlight || now >= current.deadline || !bytes ||
        current.inflightBytes > UINT64_MAX - bytes || current.inflightFrames == UINT64_MAX) return mismatch();
    ++current.inflightFrames; current.inflightBytes += bytes;
    return common::Status::ok();
}
common::Status DynamicFileQueue::finishIo(const WorkLease& lease, std::uint64_t bytes,
    WorkClock::time_point now) {
    std::lock_guard lock(mutex_);
    auto found = find(lease); if (!found.isOk()) return found.status();
    auto& current = found.value()->lease;
    if ((current.state != WorkLeaseState::InFlight && current.state != WorkLeaseState::Reclaimable) ||
        !current.inflightFrames || !bytes || bytes > current.inflightBytes ||
        ((current.inflightFrames == 1) != (bytes == current.inflightBytes))) return mismatch();
    // Completing already-issued IO after expiry can drain counters but cannot revive its deadline.
    if (current.state == WorkLeaseState::InFlight && now < current.deadline) {
        if (!deadlineFits(now, found.value()->timeout)) return mismatch();
        current.deadline = now + found.value()->timeout;
    }
    --current.inflightFrames; current.inflightBytes -= bytes;
    return common::Status::ok();
}
common::Status DynamicFileQueue::awaitResult(const WorkLease& lease, WorkClock::time_point now) {
    std::lock_guard lock(mutex_);
    auto found = find(lease); if (!found.isOk()) return found.status();
    auto& current = found.value()->lease;
    if (current.state != WorkLeaseState::InFlight || now >= current.deadline ||
        current.inflightFrames || current.inflightBytes) return mismatch();
    current.state = WorkLeaseState::CommitPending;
    return common::Status::ok();
}
common::Status DynamicFileQueue::commit(const WorkLease& lease, WorkClock::time_point now) {
    std::lock_guard lock(mutex_);
    auto found = find(lease); if (!found.isOk()) return found.status();
    auto& current = found.value()->lease;
    if (current.state != WorkLeaseState::CommitPending || now >= current.deadline ||
        current.inflightFrames || current.inflightBytes) return mismatch();
    current.state = WorkLeaseState::Completed;
    --credits_[current.owner.workerId]; active_.erase(activeKey(lease.file));
    return common::Status::ok();
}
common::Status DynamicFileQueue::reject(const WorkLease& lease) {
    std::lock_guard lock(mutex_);
    auto found = find(lease); if (!found.isOk()) return found.status();
    auto& current = found.value()->lease;
    if (current.state == WorkLeaseState::Reclaimable) return mismatch();
    current.state = WorkLeaseState::Reclaimable;
    auto& state = workers_[current.owner.workerId].state;
    if (state == WorkerState::Accepting) state = WorkerState::Draining;
    return common::Status::ok();
}
common::Status DynamicFileQueue::requeue(const WorkLease& lease, WorkClock::time_point now) {
    std::lock_guard lock(mutex_);
    auto found = find(lease); if (!found.isOk()) return found.status();
    const auto& current = found.value()->lease;
    if (current.state != WorkLeaseState::Reclaimable || current.inflightFrames || current.inflightBytes ||
        workers_[current.owner.workerId].state != WorkerState::Disconnected ||
        current.file.attempt == UINT64_MAX) return mismatch();
    auto item = static_cast<const WorkItem&>(current);
    ++item.file.attempt;
    retry_.push_back({std::move(item), now});
    --credits_[current.owner.workerId]; active_.erase(activeKey(lease.file));
    return common::Status::ok();
}
common::Result<WorkLease> DynamicFileQueue::snapshot(const WorkLease& lease) const {
    std::lock_guard lock(mutex_);
    auto found = find(lease); if (!found.isOk()) return found.status();
    return found.value()->lease;
}
std::size_t DynamicFileQueue::expire(WorkClock::time_point now) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return 0;
    std::size_t expired = 0;
    for (auto& [key, active] : active_) {
        auto& lease = active.lease;
        if (lease.state != WorkLeaseState::Reclaimable && now >= lease.deadline) {
            lease.state = WorkLeaseState::Reclaimable;
            auto& state = workers_[lease.owner.workerId].state;
            if (state == WorkerState::Accepting) state = WorkerState::Draining;
            ++expired;
        }
    }
    return expired;
}
common::Status DynamicFileQueue::requestDrain(const WorkerContext& owner) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!matchesWorker(owner) || workers_[owner.workerId].state == WorkerState::Disconnected) return mismatch();
    workers_[owner.workerId].state = WorkerState::Draining;
    return common::Status::ok();
}
common::Status DynamicFileQueue::disconnect(const WorkerContext& owner) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!matchesWorker(owner)) return mismatch();
    workers_[owner.workerId].state = WorkerState::Disconnected;
    for (auto& [key, active] : active_) {
        if (active.lease.owner != owner) continue;
        active.lease.state = WorkLeaseState::Reclaimable;
        // Only the executor can confirm transport teardown; old completions then become invalid.
        active.lease.inflightFrames = active.lease.inflightBytes = 0;
    }
    return common::Status::ok();
}
common::Result<bool> DynamicFileQueue::cutReady(const WorkerContext& owner) const {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!matchesWorker(owner)) return mismatch();
    return workers_[owner.workerId].state != WorkerState::Accepting && credits_[owner.workerId] == 0;
}
common::Result<WorkerContext> DynamicFileQueue::reopen(const WorkerContext& owner) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!matchesWorker(owner) || workers_[owner.workerId].state == WorkerState::Accepting ||
        credits_[owner.workerId] != 0 || owner.epoch.value == UINT64_MAX) return mismatch();
    auto& worker = workers_[owner.workerId];
    ++worker.context.epoch.value;
    worker.state = WorkerState::Accepting;
    return worker.context;
}
void DynamicFileQueue::cancel(const common::Status& reason) {
    std::lock_guard lock(mutex_);
    if (status_.isOk()) status_ = reason.isOk() ? common::Status::runtimeError("dynamic queue cancelled") : reason;
    for (auto& item : ring_) item.reset();
    size_ = 0; retry_.clear(); active_.clear();
    std::fill(credits_.begin(), credits_.end(), 0);
}
std::size_t DynamicFileQueue::highWatermark() const { std::lock_guard lock(mutex_); return high_; }
std::size_t DynamicFileQueue::outstanding() const { std::lock_guard lock(mutex_); return active_.size(); }
common::Result<WorkItem> bindScheduledWork(const scheduler::WorkItem& plan,
    const PersistentFileIdentity& identity) {
    const auto offset = identity.rangeId ? identity.rangeOffset : 0;
    const auto length = identity.rangeId ? identity.rangeLength : identity.totalSize;
    if (!validWorkItem(WorkItem{identity, plan.taskId}) || !plan.dispatchable ||
        plan.fileId != identity.transferId || plan.relativePath != identity.relativePath ||
        plan.offset != offset || plan.length != length || plan.checksumAlgorithm != identity.checksumAlgorithm ||
        (plan.resumeGeneration != 0 && plan.resumeGeneration != identity.generation) ||
        plan.retryCount != identity.attempt) return mismatch();
    return WorkItem{identity, plan.taskId};
}
}  // namespace cpnetflux::core::io
