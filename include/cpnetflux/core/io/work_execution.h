#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "cpnetflux/core/io/persistent_data_session.h"

namespace cpnetflux::core::scheduler { struct WorkItem; struct FilePlan; }
namespace cpnetflux::core::io {
using WorkClock = std::chrono::steady_clock;
struct ConnectionEpoch {
    std::uint64_t value = 1;
    bool operator==(const ConnectionEpoch&) const = default;
};
struct WorkerContext {
    std::uint32_t workerId = 0;
    std::string linkId = "link0";
    ConnectionEpoch epoch;
    bool operator==(const WorkerContext&) const = default;
};
// Full V3 identity is canonical; scheduler records are planning projections only.
struct WorkItem {
    PersistentFileIdentity file;
    std::string taskId;
    bool compressionCandidate = false;
};
enum class WorkLeaseState { Claimed, InFlight, CommitPending, Completed, Failed, Reclaimable };
// A value handle. State/counters/deadline are snapshots; the queue owns current state.
struct WorkLease : WorkItem {
    std::uint64_t leaseId = 0;
    WorkerContext owner;
    WorkLeaseState state = WorkLeaseState::Claimed;
    WorkClock::time_point deadline;
    std::uint64_t inflightFrames = 0, inflightBytes = 0;
    double queueWaitSeconds = 0;
};
// Every active lease requires an explicit verdict from manifest/peer reconciliation.
// Absence from a missing list is never sufficient evidence of a successful commit.
enum class WorkRecoveryDisposition { Missing, Committed };
struct WorkRecoveryDecision {
    WorkLease lease;
    WorkRecoveryDisposition disposition = WorkRecoveryDisposition::Missing;
};
[[nodiscard]] common::Result<WorkItem> bindScheduledWork(
    const scheduler::WorkItem& plan, const PersistentFileIdentity& identity);
// Scheduler plans remain projections; this adapter binds their ordered file set to full V3 identities.
[[nodiscard]] common::Result<std::vector<WorkItem>> bindScheduledPlans(
    const std::vector<scheduler::FilePlan>& plans,
    const std::vector<PersistentFileIdentity>& identities);
}  // namespace cpnetflux::core::io
