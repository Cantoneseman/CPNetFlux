#include "cpnetflux/core/io/work_executor.h"

namespace cpnetflux::core::io {

common::Result<std::optional<WorkLease>> WorkExecutor::claim(
    WorkClock::time_point now, std::chrono::milliseconds timeout) {
    return queue_->claim(owner_, now, timeout);
}

common::Result<WorkLease> WorkExecutor::claimIncoming(
    WorkItem item, WorkClock::time_point now, std::chrono::milliseconds timeout) {
    return queue_->claimIncoming(owner_, std::move(item), now, timeout);
}

common::Status WorkExecutor::begin(const WorkLease& lease, WorkClock::time_point now) {
    if (lease.owner != owner_) {
        return common::Status::invalidArgument("work executor owner mismatch");
    }
    return queue_->begin(lease, now);
}

common::Status WorkExecutor::startIo(
    const WorkLease& lease, std::uint64_t bytes, WorkClock::time_point now) {
    if (lease.owner != owner_) {
        return common::Status::invalidArgument("work executor owner mismatch");
    }
    return queue_->startIo(lease, bytes, now);
}

common::Status WorkExecutor::finishIo(
    const WorkLease& lease, std::uint64_t bytes, WorkClock::time_point now) {
    if (lease.owner != owner_) {
        return common::Status::invalidArgument("work executor owner mismatch");
    }
    return queue_->finishIo(lease, bytes, now);
}

common::Status WorkExecutor::awaitResult(const WorkLease& lease, WorkClock::time_point now) {
    if (lease.owner != owner_) {
        return common::Status::invalidArgument("work executor owner mismatch");
    }
    return queue_->awaitResult(lease, now);
}

common::Status WorkExecutor::commit(const WorkLease& lease, WorkClock::time_point now) {
    if (lease.owner != owner_) {
        return common::Status::invalidArgument("work executor owner mismatch");
    }
    return queue_->commit(lease, now);
}

common::Status WorkExecutor::reject(const WorkLease& lease) {
    if (lease.owner != owner_) {
        return common::Status::invalidArgument("work executor owner mismatch");
    }
    return queue_->reject(lease);
}

common::Status WorkExecutor::failTerminal(
    const WorkLease& lease, const common::Status& reason) {
    if (lease.owner != owner_) {
        return common::Status::invalidArgument("work executor owner mismatch");
    }
    return queue_->failTerminal(lease, reason);
}

common::Status WorkExecutor::finalize(
    const WorkLease& lease, const common::Status& result, WorkClock::time_point now) {
    if (lease.owner != owner_) {
        return common::Status::invalidArgument("work executor owner mismatch");
    }
    if (!result.isOk()) return reject(lease);
    auto current = queue_->snapshot(lease);
    if (!current.isOk()) return current.status();
    if (current.value().state == WorkLeaseState::InFlight) {
        auto awaited = awaitResult(lease, now);
        if (!awaited.isOk()) return awaited;
    }
    return commit(lease, now);
}

common::Result<WorkerContext> WorkExecutor::recover(
    const std::vector<WorkRecoveryDecision>& reconciled, std::uint32_t maxRetries,
    WorkClock::time_point now) {
    auto recovered = queue_->recover(owner_, reconciled, maxRetries, now);
    if (recovered.isOk()) owner_ = recovered.value();
    return recovered;
}

}  // namespace cpnetflux::core::io
