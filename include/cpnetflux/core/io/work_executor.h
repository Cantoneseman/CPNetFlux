#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "cpnetflux/core/io/dynamic_file_queue.h"

namespace cpnetflux::core::io {

// Shared typed entry point for sender and receiver lease lifecycles.
// Queue must outlive this object. Quiesce all users before recover mutates owner_.
class WorkExecutor {
 public:
    WorkExecutor(DynamicFileQueue& queue, WorkerContext owner) : queue_(&queue), owner_(std::move(owner)) {}

    [[nodiscard]] const WorkerContext& owner() const noexcept { return owner_; }

    [[nodiscard]] common::Result<std::optional<WorkLease>> claim(
        WorkClock::time_point now = WorkClock::now(),
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    [[nodiscard]] common::Result<WorkLease> claimIncoming(
        WorkItem item, WorkClock::time_point now = WorkClock::now(),
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    [[nodiscard]] common::Status begin(
        const WorkLease&, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status startIo(
        const WorkLease&, std::uint64_t bytes, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status finishIo(
        const WorkLease&, std::uint64_t bytes, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status awaitResult(
        const WorkLease&, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status commit(
        const WorkLease&, WorkClock::time_point now = WorkClock::now());
    [[nodiscard]] common::Status reject(const WorkLease&);
    [[nodiscard]] common::Status failTerminal(const WorkLease&, const common::Status& reason);

    // Returned Status describes the lifecycle transition, not the transfer result.
    // Success passes through awaitResult before commit; failure retains a reclaimable lease.
    [[nodiscard]] common::Status finalize(
        const WorkLease&, const common::Status& result,
        WorkClock::time_point now = WorkClock::now());

    // Caller must stop/join old transport IO and call queue.disconnect(owner()).
    // Supply one authoritative verdict per active lease; maxRetries excludes the initial attempt.
    // Only success replaces owner_ with the new epoch. This does not open network connections.
    [[nodiscard]] common::Result<WorkerContext> recover(
        const std::vector<WorkRecoveryDecision>& reconciled,
        std::uint32_t maxRetries = 3,
        WorkClock::time_point now = WorkClock::now());

 private:
    DynamicFileQueue* queue_ = nullptr;
    WorkerContext owner_;
};

}  // namespace cpnetflux::core::io
