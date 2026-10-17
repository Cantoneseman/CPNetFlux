#include "cpnetflux/core/io/dynamic_file_queue.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include "cpnetflux/core/scheduler/work_item.h"
#include "cpnetflux/core/scheduler/work_item_planner.h"
#include "cpnetflux/core/scheduler/scheduler.h"
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
    std::uint32_t channels, std::uint32_t window, std::size_t capacity, bool preserveReadyOrder)
    : DynamicFileQueue(asWorkItems(std::move(files)), channels, window, capacity, preserveReadyOrder) {}

DynamicFileQueue::DynamicFileQueue(std::vector<WorkItem> files,
    std::uint32_t channels, std::uint32_t window, std::size_t capacity, bool preserveReadyOrder)
    : files_(std::move(files)), window_(window), preserveReadyOrder_(preserveReadyOrder) {
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
    if (!preserveReadyOrder_) {
        std::stable_sort(files_.begin(), files_.end(), [](const auto& a, const auto& b) {
            return a.file.totalSize != b.file.totalSize ? a.file.totalSize > b.file.totalSize : a.file.fileId < b.file.fileId;
        });
    }
    fill();
}
common::Status DynamicFileQueue::attachScheduler(scheduler::GlobalScheduler* scheduler) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!scheduler || scheduler_ || leasesIssued_ || scheduler->config().maxConnections > workers_.size())
        return mismatch();
    scheduler_ = scheduler;
    workerLimit_ = scheduler->config().initialConnections;
    readyBytes_ = 0;
    for (std::size_t index = next_; index < files_.size(); ++index) {
        const auto& item = files_[index];
        const auto bytes = item.file.rangeId ? item.file.rangeLength : item.file.totalSize;
        if (readyBytes_ > UINT64_MAX - bytes) return mismatch();
        readyBytes_ += bytes;
    }
    // fill() moves admitted metadata, so count admitted entries as well.
    for (const auto& ready : ring_) if (ready) {
        const auto& file = ready->item.file;
        readyBytes_ += file.rangeId ? file.rangeLength : file.totalSize;
    }
    for (auto& worker : workers_) worker.context.linkId = scheduler->config().linkId;
    return scheduler_->linkManager().setReadyBytes(scheduler_->config().linkId, readyBytes_);
}
common::Status DynamicFileQueue::setConnectionEpoch(ConnectionEpoch epoch) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!epoch.value || leasesIssued_) return mismatch();
    for (auto& worker : workers_) worker.context.epoch = epoch;
    return common::Status::ok();
}
common::Status DynamicFileQueue::schedulerEvent(const WorkLease& lease, const char* event) {
    if (!scheduler_) return common::Status::ok();
    const auto& file = lease.file;
    scheduler::SchedulerEventRecord record;
    record.event = event; record.taskId = scheduler_->taskId(); record.fileId = file.transferId;
    record.linkId = lease.owner.linkId; record.offset = file.rangeId ? file.rangeOffset : 0;
    record.length = file.rangeId ? file.rangeLength : file.totalSize;
    record.readyBytes = readyBytes_; record.targetConnections = workerLimit_;
    record.reason = std::string(event) == "workitem_reconciled"
        ? "manifest_confirmed_commit" : "shared_v3_executor";
    record.result = std::string(event) == "executor_failed" ? "fail" : "ok";
    record.leaseId = lease.leaseId; record.workerId = lease.owner.workerId;
    record.connectionEpoch = lease.owner.epoch.value; record.generation = file.generation;
    record.attempt = file.attempt;
    if (std::string(event) == "workitem_retry") return scheduler_->recordRetry(record);
    if (std::string(event) == "executor_failed") return scheduler_->recordFailed(record);
    if (std::string(event) == "executor_complete") return scheduler_->recordExecutorComplete(record);
    if (std::string(event) == "workitem_reconciled") return scheduler_->recordLinkEvent(record);
    return scheduler_->recordDispatch(record);
}
common::Status DynamicFileQueue::schedulerFeedback() {
    if (!scheduler_) return common::Status::ok();
    auto& manager = scheduler_->linkManager();
    const auto& id = scheduler_->config().linkId;
    auto updated = manager.setReadyBytes(id, readyBytes_); if (!updated.isOk()) return updated;
    auto* link = manager.link(id); if (!link) return mismatch();
    const auto decision = scheduler_->feedbackController().evaluate(*link, 0, 0, 0);
    workerLimit_ = decision.targetConnections;
    updated = manager.setTargetConnections(id, workerLimit_); if (!updated.isOk()) return updated;
    updated = manager.recordWatermarkHit(id, decision.queueLow, decision.queueHigh);
    if (!updated.isOk()) return updated;
    updated = manager.recordRamp(id, decision.rampUp, decision.rampDown);
    if (!updated.isOk()) return updated;
    changed_.notify_all();
    return common::Status::ok();
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
    std::unique_lock lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!matchesWorker(owner) || workers_[owner.workerId].state != WorkerState::Accepting ||
        !deadlineFits(now, timeout)) return mismatch();
    if (scheduler_ && owner.workerId >= workerLimit_ && (size_ || !retry_.empty())) {
        const auto admissionDeadline = WorkClock::now() + timeout;
        if (changed_.wait_until(lock, admissionDeadline, [&] { return !status_.isOk() || !matchesWorker(owner) ||
            workers_[owner.workerId].state != WorkerState::Accepting ||
            owner.workerId < workerLimit_ || (!size_ && retry_.empty()); }) == false)
            return common::Status::runtimeError("scheduler worker admission timed out");
        if (!status_.isOk()) return status_;
        if (!matchesWorker(owner) || workers_[owner.workerId].state != WorkerState::Accepting) return mismatch();
        now = WorkClock::now();
    }
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
    const auto inserted = active_.emplace(activeKey(lease.file), Active{lease, timeout, scheduler_ != nullptr}).second;
    if (!inserted) return common::Status::runtimeError("duplicate active work item");
    ++credits_[owner.workerId]; leasesIssued_ = true; fill();
    if (scheduler_) {
        const auto bytes = lease.file.rangeId ? lease.file.rangeLength : lease.file.totalSize;
        readyBytes_ -= bytes;
        auto observed = scheduler_->linkManager().addInflightBytes(scheduler_->config().linkId, bytes);
        if (observed.isOk()) observed = schedulerEvent(lease, "workitem_dispatch");
        if (observed.isOk()) observed = schedulerFeedback();
        if (!observed.isOk()) { status_ = observed; changed_.notify_all(); return observed; }
    }
    changed_.notify_all();
    return std::optional<WorkLease>{std::move(lease)};
}
common::Result<WorkLease> DynamicFileQueue::claimIncoming(const WorkerContext& owner,
    WorkItem item, WorkClock::time_point now, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!validWorkItem(item) || !matchesWorker(owner) ||
        workers_[owner.workerId].state != WorkerState::Accepting ||
        !deadlineFits(now, timeout)) return mismatch();
    if (scheduler_ && owner.workerId >= workerLimit_) {
        const auto admissionDeadline = WorkClock::now() + timeout;
        if (changed_.wait_until(lock, admissionDeadline, [&] {
            return !status_.isOk() || !matchesWorker(owner) ||
                workers_[owner.workerId].state != WorkerState::Accepting ||
                owner.workerId < workerLimit_;
        }) == false)
            return common::Status::runtimeError("scheduler worker admission timed out");
        if (!status_.isOk()) return status_;
        if (!matchesWorker(owner) || workers_[owner.workerId].state != WorkerState::Accepting)
            return mismatch();
        now = WorkClock::now();
    }
    if (credits_[owner.workerId] >= window_) {
        return common::Status::runtimeError("dynamic channel credit exhausted");
    }
    const auto leaseId = acquireLeaseId();
    if (!leaseId) return common::Status::runtimeError("work lease ID exhausted");
    WorkLease lease;
    static_cast<WorkItem&>(lease) = std::move(item);
    lease.leaseId = leaseId;
    lease.owner = owner;
    lease.deadline = now + timeout;
    if (!active_.emplace(activeKey(lease.file), Active{lease, timeout, scheduler_ != nullptr}).second)
        return common::Status::runtimeError("duplicate active work item");
    ++credits_[owner.workerId];
    leasesIssued_ = true;
    if (scheduler_) {
        const auto bytes = lease.file.rangeId ? lease.file.rangeLength : lease.file.totalSize;
        auto observed = scheduler_->linkManager().addInflightBytes(scheduler_->config().linkId, bytes);
        if (observed.isOk()) observed = schedulerEvent(lease, "workitem_dispatch");
        if (observed.isOk()) observed = schedulerFeedback();
        if (!observed.isOk()) {
            status_ = observed;
            changed_.notify_all();
            return observed;
        }
    }
    changed_.notify_all();
    return lease;
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
    if (scheduler_ && found.value()->schedulerInflight) {
        const auto bytes = current.file.rangeId ? current.file.rangeLength : current.file.totalSize;
        auto observed = scheduler_->linkManager().releaseInflightBytes(scheduler_->config().linkId, bytes);
        if (observed.isOk()) observed = schedulerEvent(current, "executor_complete");
        if (observed.isOk()) observed = schedulerFeedback();
        if (!observed.isOk()) { status_ = observed; changed_.notify_all(); return observed; }
        found.value()->schedulerInflight = false;
    }
    current.state = WorkLeaseState::Completed;
    --credits_[current.owner.workerId]; active_.erase(activeKey(lease.file));
    changed_.notify_all();
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
    if (scheduler_ && found.value()->schedulerInflight) {
        const auto bytes = current.file.rangeId ? current.file.rangeLength : current.file.totalSize;
        auto observed = scheduler_->linkManager().releaseInflightBytes(scheduler_->config().linkId, bytes);
        if (observed.isOk()) observed = schedulerEvent(current, "executor_failed");
        if (observed.isOk()) observed = schedulerFeedback();
        if (!observed.isOk()) {
            status_ = observed;
            changed_.notify_all();
            return observed;
        }
        found.value()->schedulerInflight = false;
    }
    changed_.notify_all();
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
    const auto bytes = item.file.rangeId ? item.file.rangeLength : item.file.totalSize;
    if (scheduler_) {
        auto observed = common::Status::ok();
        if (found.value()->schedulerInflight) {
            observed = scheduler_->linkManager().releaseInflightBytes(scheduler_->config().linkId, bytes);
            if (observed.isOk()) found.value()->schedulerInflight = false;
        }
        if (observed.isOk()) observed = scheduler_->linkManager().addReadyBytes(
            scheduler_->config().linkId, bytes);
        if (!observed.isOk()) {
            status_ = observed;
            changed_.notify_all();
            return observed;
        }
        readyBytes_ += bytes;
        observed = scheduler_->linkManager().addRetryCount(scheduler_->config().linkId, 1);
        if (observed.isOk()) observed = schedulerEvent(current, "workitem_retry");
        if (observed.isOk()) observed = schedulerFeedback();
        if (!observed.isOk()) {
            status_ = observed;
            changed_.notify_all();
            return observed;
        }
    }
    retry_.push_back({std::move(item), now});
    --credits_[current.owner.workerId]; active_.erase(activeKey(lease.file));
    changed_.notify_all();
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
    changed_.notify_all();
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
    changed_.notify_all();
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
    changed_.notify_all();
    return worker.context;
}
common::Result<WorkerContext> DynamicFileQueue::recover(
    const WorkerContext& owner, const std::vector<WorkRecoveryDecision>& reconciled,
    std::uint32_t maxRetries, WorkClock::time_point now) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (!matchesWorker(owner) || workers_[owner.workerId].state != WorkerState::Disconnected ||
        owner.epoch.value == UINT64_MAX) return mismatch();

    // Validate the entire ledger before releasing credits or changing any epoch.
    std::unordered_set<std::uint64_t> seen;
    std::size_t owned = 0;
    for (const auto& [key, active] : active_) {
        if (active.lease.owner == owner) ++owned;
    }
    if (reconciled.size() != owned || owned != credits_[owner.workerId]) {
        return common::Status::invalidArgument("recovery requires a verdict for every active lease");
    }
    std::uint64_t retryBytes = 0;
    for (const auto& decision : reconciled) {
        const auto& lease = decision.lease;
        if (lease.owner != owner || !seen.insert(lease.leaseId).second) return mismatch();
        auto found = find(lease);
        if (!found.isOk()) return found.status();
        const auto& current = found.value()->lease;
        if (current.state != WorkLeaseState::Reclaimable ||
            current.inflightFrames || current.inflightBytes) return mismatch();
        if (decision.disposition != WorkRecoveryDisposition::Missing &&
            decision.disposition != WorkRecoveryDisposition::Committed) return mismatch();
        if (decision.disposition == WorkRecoveryDisposition::Missing) {
            if (current.file.attempt >= maxRetries) {
                return common::Status::runtimeError("work retry limit exceeded");
            }
            const auto bytes = current.file.rangeId ? current.file.rangeLength : current.file.totalSize;
            if (retryBytes > UINT64_MAX - bytes) return mismatch();
            retryBytes += bytes;
        }
    }
    if (readyBytes_ > UINT64_MAX - retryBytes) return mismatch();

    for (const auto& decision : reconciled) {
        auto found = active_.find(activeKey(decision.lease.file));
        auto& active = found->second;
        const bool retry = decision.disposition == WorkRecoveryDisposition::Missing;
        const auto bytes = active.lease.file.rangeId ? active.lease.file.rangeLength :
            active.lease.file.totalSize;
        auto observed = common::Status::ok();
        if (scheduler_ && active.schedulerInflight) {
            observed = scheduler_->linkManager().releaseInflightBytes(
                scheduler_->config().linkId, bytes);
            if (observed.isOk()) active.schedulerInflight = false;
        }
        if (observed.isOk() && scheduler_ && retry) {
            observed = scheduler_->linkManager().addReadyBytes(
                scheduler_->config().linkId, bytes);
            if (observed.isOk()) readyBytes_ += bytes;
            if (observed.isOk()) observed = scheduler_->linkManager().addRetryCount(
                scheduler_->config().linkId, 1);
        }
        if (observed.isOk()) observed = schedulerEvent(active.lease,
            retry ? "workitem_retry" : "workitem_reconciled");
        // A telemetry IO failure is terminal, as with ordinary claim/commit.
        if (!observed.isOk()) {
            status_ = observed;
            changed_.notify_all();
            return observed;
        }
        if (retry) {
            auto item = static_cast<const WorkItem&>(active.lease);
            ++item.file.attempt;
            retry_.push_back({std::move(item), now});
        }
        --credits_[owner.workerId];
        active_.erase(found);
    }

    auto observed = schedulerFeedback();
    if (!observed.isOk()) {
        status_ = observed;
        changed_.notify_all();
        return observed;
    }
    auto& worker = workers_[owner.workerId];
    ++worker.context.epoch.value;
    worker.state = WorkerState::Accepting;
    changed_.notify_all();
    return worker.context;
}

void DynamicFileQueue::cancel(const common::Status& reason) {
    std::lock_guard lock(mutex_);
    if (status_.isOk()) status_ = reason.isOk() ? common::Status::runtimeError("dynamic queue cancelled") : reason;
    for (auto& item : ring_) item.reset();
    size_ = 0; retry_.clear(); active_.clear();
    std::fill(credits_.begin(), credits_.end(), 0);
    changed_.notify_all();
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
common::Result<std::vector<WorkItem>> bindScheduledPlans(
    const std::vector<scheduler::FilePlan>& plans,
    const std::vector<PersistentFileIdentity>& identities) {
    std::vector<WorkItem> work;
    std::unordered_set<std::string> seen;
    for (const auto& plan : plans) {
        if (plan.taskId.empty() || !seen.insert(plan.fileId).second || plan.workItems.empty()) return mismatch();
        std::uint64_t covered = 0;
        for (const auto& projection : plan.workItems) {
            if (projection.taskId != plan.taskId || projection.fileId != plan.fileId ||
                projection.relativePath != plan.relativePath || projection.offset != covered ||
                projection.length > plan.totalBytes - covered || projection.checksumAlgorithm != plan.checksumAlgorithm)
                return mismatch();
            covered += projection.length;
        }
        if (covered != plan.totalBytes) return mismatch();
        std::uint64_t identityBytes = 0; std::size_t matched = 0;
        for (const auto& identity : identities) {
            if (identity.transferId != plan.fileId) continue;
            if (identity.relativePath != plan.relativePath || identity.totalSize != plan.totalBytes ||
                identity.checksumAlgorithm != plan.checksumAlgorithm) return mismatch();
            const auto offset = identity.rangeId ? identity.rangeOffset : 0;
            const auto length = identity.rangeId ? identity.rangeLength : identity.totalSize;
            auto projection = plan.workItems.front();
            projection.resumeGeneration = identity.generation;
            projection.retryCount = identity.attempt;
            projection.offset = offset; projection.length = length; projection.dispatchable = true;
            auto item = bindScheduledWork(projection, identity);
            if (!item.isOk()) return item.status();
            item.value().compressionCandidate = plan.compression.disposition == scheduler::CompressionDisposition::CompressionCandidate;
            work.push_back(std::move(item.value()));
            identityBytes += length; ++matched;
        }
        if (!matched || identityBytes != plan.totalBytes) return mismatch();
    }
    if (work.size() != identities.size()) return mismatch();
    return work;
}
}  // namespace cpnetflux::core::io
