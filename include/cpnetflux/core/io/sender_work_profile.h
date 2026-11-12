#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/member_work_profile.h"

namespace cpnetflux::core::io {
enum class SenderWorkStage : std::size_t {
    ProducerCreate, ProducerLoop, JoinProducerWork, ProducerJoinTail,
    CreditReady, CreditEmptyAtResume, CreditUnproven, CreditTerminal,
    ResultRead, ResultDrain, Count
};
inline constexpr auto kSenderWorkStages = static_cast<std::size_t>(SenderWorkStage::Count);
struct SenderWorkInterval {
    double begin = 0, end = 0;
    SenderWorkStage stage;
};
// Lane writer and result-reader own separate collectors, merged after join.
struct SenderWorkProfile {
    static constexpr std::size_t kMaxIntervals = 65536;
    bool enabled = false, truncated = false;
    std::uint64_t startedMembers = 0, sentMembers = 0, completedMembers = 0;
    std::uint64_t excludedRanges = 0, resultFailures = 0;
    double workerEntrySeconds = 0, readSeconds = 0, checksumSeconds = 0;
    std::array<std::uint64_t, kSenderWorkStages> calls{};
    std::array<double, kSenderWorkStages> sums{};
    std::vector<SenderWorkInterval> intervals;
    void record(SenderWorkStage stage, double begin, double end);
    void join(double begin, double loopExit, double end);
    void merge(SenderWorkProfile&& other);
};
common::Result<bool> senderWorkProfileFromEnvironment();
void appendSenderWorkProfile(std::ostream& out, bool enabled, bool sender,
                             const std::vector<SenderWorkProfile>& lanes);
}  // namespace cpnetflux::core::io
