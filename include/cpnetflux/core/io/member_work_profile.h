#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>
#include "cpnetflux/checkpoint/manifest_save_stats.h"

namespace cpnetflux::core::io {
enum class MemberWorkStage : std::size_t {
    ThreadCreate, JoinPayloadWait, ThreadJoinTail, TransactionBegin, Commit, ResultWrite, Count
};
inline constexpr std::size_t kMemberWorkStages = static_cast<std::size_t>(MemberWorkStage::Count);
struct MemberWorkInterval {
    double begin = 0, end = 0;
    MemberWorkStage stage = MemberWorkStage::Count;
};
struct MemberWorkSample {
    std::array<MemberWorkInterval, kMemberWorkStages> intervals{};
    double workerEntrySeconds = 0;
    void set(MemberWorkStage stage, double begin, double end);
    void splitJoin(double begin, double workerLoopExit, double end);
};
// One lane owns this collector; only completed whole-file attempts are recorded.
struct MemberWorkProfile {
    static constexpr std::size_t kMaxIntervals = 65536;
    bool enabled = false, truncated = false;
    std::uint64_t startedMembers = 0, completedMembers = 0, excludedRanges = 0;
    double workerEntrySeconds = 0;
    std::array<std::uint64_t, kMemberWorkStages> calls{};
    std::array<double, kMemberWorkStages> sums{};
    std::vector<MemberWorkInterval> intervals;
    checkpoint::ManifestSaveStats manifestSave;
    void record(const MemberWorkSample& sample, const checkpoint::ManifestSaveStats& saves);
};
double memberWorkNow();
double memberWorkUnionSeconds(std::vector<MemberWorkInterval> intervals);
void appendMemberWorkProfile(std::ostream& out, bool enabled, bool receiver,
                             const std::vector<MemberWorkProfile>& lanes);
}  // namespace cpnetflux::core::io
