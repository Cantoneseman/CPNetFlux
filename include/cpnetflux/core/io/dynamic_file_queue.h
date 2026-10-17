#pragma once

#include <deque>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "cpnetflux/core/io/work_execution.h"

namespace cpnetflux::core::scheduler { class GlobalScheduler; }
namespace cpnetflux::core::io {
[[nodiscard]] bool samePersistentIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept;
[[nodiscard]] bool samePersistentRangeIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept;
using DynamicFileLease = WorkLease;
using DynamicFileLeaseState = WorkLeaseState;
// One owner of ready work, lease state and per-worker credits. All operations are serialized.
class DynamicFileQueue {
 public:
    DynamicFileQueue(std::vector<PersistentFileIdentity> files, std::uint32_t channels,
                     std::uint32_t window, std::size_t capacity, bool preserveReadyOrder = false);
    DynamicFileQueue(std::vector<WorkItem> work, std::uint32_t channels,
                     std::uint32_t window, std::size_t capacity, bool preserveReadyOrder = false);
    [[nodiscard]] common::Status attachScheduler(scheduler::GlobalScheduler* scheduler);
    [[nodiscard]] common::Status setConnectionEpoch(ConnectionEpoch epoch);
    // Compatibility entry points are restricted to the original epoch. New executors retain handles.
    [[nodiscard]] common::Result<std::optional<DynamicFileLease>> claim(std::uint32_t channel);
    [[nodiscard]] common::Status markInFlight(std::uint32_t channel, const PersistentFileIdentity& file);
    [[nodiscard]] common::Status markCommitPending(std::uint32_t channel, const PersistentFileIdentity& file);
    [[nodiscard]] common::Status complete(std::uint32_t channel, const PersistentFileIdentity& file);
    [[nodiscard]] common::Result<WorkerContext> worker(std::uint32_t channel) const;
    [[nodiscard]] common::Result<std::optional<WorkLease>> claim(const WorkerContext& worker,
        WorkClock::time_point now = WorkClock::now(), std::chrono::milliseconds timeout = std::chrono::seconds(30));
    // Receiver-side dynamic streams learn the full identity from FILE_BEGIN.
    [[nodiscard]] common::Result<WorkLease> claimIncoming(const WorkerContext& worker, WorkItem item,
        WorkClock::time_point now = WorkClock::now(), std::chrono::milliseconds timeout = std::chrono::seconds(30));
    [[nodiscard]] common::Status begin(const WorkLease&, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status startIo(const WorkLease&, std::uint64_t bytes, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status finishIo(const WorkLease&, std::uint64_t bytes, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status awaitResult(const WorkLease&, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status commit(const WorkLease&, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status reject(const WorkLease&);
    // Requeue requires confirmed transport teardown, zero IO, and reconciled missing work.
    [[nodiscard]] common::Status requeue(const WorkLease&, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Result<WorkLease> snapshot(const WorkLease&) const;
    [[nodiscard]] std::size_t expire(WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status requestDrain(const WorkerContext&);
    // Caller must have shut down the transport and stopped its IO before disconnect.
    [[nodiscard]] common::Status disconnect(const WorkerContext&);
    [[nodiscard]] common::Result<bool> cutReady(const WorkerContext&) const;
    [[nodiscard]] common::Result<WorkerContext> reopen(const WorkerContext&);
    void cancel(const common::Status& reason);
    [[nodiscard]] std::size_t highWatermark() const;
    [[nodiscard]] std::size_t outstanding() const;
 private:
    enum class WorkerState { Accepting, Draining, Disconnected };
    struct Worker { WorkerContext context; WorkerState state = WorkerState::Accepting; };
    struct Ready { WorkItem item; WorkClock::time_point admitted; };
    struct Active { WorkLease lease; std::chrono::milliseconds timeout; bool schedulerInflight = false; };
    [[nodiscard]] bool matchesWorker(const WorkerContext&) const;
    [[nodiscard]] common::Result<Active*> find(const WorkLease&);
    [[nodiscard]] common::Result<const Active*> find(const WorkLease&) const;
    [[nodiscard]] common::Result<WorkLease> legacyLease(std::uint32_t, const PersistentFileIdentity&) const;
    void fill();
    [[nodiscard]] common::Status schedulerEvent(const WorkLease&, const char* event);
    [[nodiscard]] common::Status schedulerFeedback();
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    scheduler::GlobalScheduler* scheduler_ = nullptr;
    std::uint32_t workerLimit_ = 64;
    std::uint64_t readyBytes_ = 0;
    bool leasesIssued_ = false;
    std::vector<WorkItem> files_;
    std::vector<std::optional<Ready>> ring_;
    // Reclaimed work is bounded by released active credits and is claimed before fresh ready work.
    std::deque<Ready> retry_;
    std::vector<std::uint32_t> credits_;
    std::vector<Worker> workers_;
    std::unordered_map<std::string, Active> active_;
    std::uint32_t window_;
    bool preserveReadyOrder_ = false;
    std::size_t next_ = 0, head_ = 0, tail_ = 0, size_ = 0, high_ = 0;
    common::Status status_ = common::Status::ok();
};
}  // namespace cpnetflux::core::io
