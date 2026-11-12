#include "cpnetflux/core/io/sender_work_profile.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ostream>
#include <string_view>

namespace cpnetflux::core::io {
namespace {
constexpr const char* names[] = {"producer_create", "producer_loop", "join_producer_work", "producer_join_tail",
    "credit_ready", "credit_empty_at_resume", "credit_unproven", "credit_terminal", "result_read", "result_drain"};
}
common::Result<bool> senderWorkProfileFromEnvironment() {
    const auto* value = std::getenv("CPNETFLUX_SENDER_WORK_PROFILE");
    if (!value || std::string_view(value) == "0") return false;
    if (std::string_view(value) == "1") return true;
    return common::Status::invalidArgument("CPNETFLUX_SENDER_WORK_PROFILE must be 0 or 1");
}
void SenderWorkProfile::record(SenderWorkStage stage, double begin, double end) {
    if (!enabled) return;
    const auto i = static_cast<std::size_t>(stage);
    if (i >= kSenderWorkStages || !std::isfinite(begin) || !std::isfinite(end) || begin < 0 || end < begin) {
        truncated = true; return;
    }
    ++calls[i]; sums[i] += end - begin;
    if (intervals.size() == kMaxIntervals) { truncated = true; return; }
    intervals.push_back({begin, end, stage});
}
void SenderWorkProfile::join(double begin, double loopExit, double end) {
    if (end < begin) { truncated = true; return; }
    const auto split = std::clamp(loopExit, begin, end);
    record(SenderWorkStage::JoinProducerWork, begin, split);
    record(SenderWorkStage::ProducerJoinTail, split, end);
}
void SenderWorkProfile::merge(SenderWorkProfile&& other) {
    if (!enabled) return;
    truncated |= other.truncated;
    startedMembers += other.startedMembers; sentMembers += other.sentMembers;
    completedMembers += other.completedMembers; excludedRanges += other.excludedRanges;
    resultFailures += other.resultFailures; workerEntrySeconds += other.workerEntrySeconds;
    readSeconds += other.readSeconds; checksumSeconds += other.checksumSeconds;
    for (std::size_t i = 0; i < kSenderWorkStages; ++i) { calls[i] += other.calls[i]; sums[i] += other.sums[i]; }
    const auto available = kMaxIntervals - intervals.size();
    truncated |= other.intervals.size() > available;
    intervals.insert(intervals.end(), other.intervals.begin(),
                     other.intervals.begin() + std::min(available, other.intervals.size()));
}
void appendSenderWorkProfile(std::ostream& out, bool enabled, bool sender,
                             const std::vector<SenderWorkProfile>& lanes) {
    out << "{\"enabled\":" << (enabled ? "true" : "false");
    if (!enabled) { out << '}'; return; }
    SenderWorkProfile total;
    std::array<std::vector<MemberWorkInterval>, kSenderWorkStages> ranges;
    std::vector<MemberWorkInterval> threadRanges;
    for (const auto& lane : lanes) {
        total.truncated |= lane.truncated;
        total.startedMembers += lane.startedMembers; total.sentMembers += lane.sentMembers;
        total.completedMembers += lane.completedMembers; total.excludedRanges += lane.excludedRanges;
        total.resultFailures += lane.resultFailures; total.workerEntrySeconds += lane.workerEntrySeconds;
        total.readSeconds += lane.readSeconds; total.checksumSeconds += lane.checksumSeconds;
        for (std::size_t i = 0; i < kSenderWorkStages; ++i) { total.calls[i] += lane.calls[i]; total.sums[i] += lane.sums[i]; }
        for (const auto& interval : lane.intervals) {
            const MemberWorkInterval range{interval.begin, interval.end};
            ranges[static_cast<std::size_t>(interval.stage)].push_back(range);
            if (interval.stage == SenderWorkStage::ProducerCreate || interval.stage == SenderWorkStage::ProducerJoinTail)
                threadRanges.push_back(range);
        }
    }
    out << ",\"effective\":\"" << (sender ? "sender" : "not_used_receiver")
        << "\",\"scope\":\"local_whole_file_sender_attempts_excludes_ranges\""
        << ",\"truncated\":" << (total.truncated ? "true" : "false")
        << ",\"started_members\":" << total.startedMembers << ",\"sent_members\":" << total.sentMembers
        << ",\"completed_members\":" << total.completedMembers << ",\"excluded_ranges\":" << total.excludedRanges
        << ",\"result_failures\":" << total.resultFailures
        << ",\"worker_entry_sum_seconds\":" << total.workerEntrySeconds
        << ",\"read_sum_seconds\":" << total.readSeconds << ",\"checksum_sum_seconds\":" << total.checksumSeconds
        << ",\"thread_envelope_union_seconds\":" << memberWorkUnionSeconds(std::move(threadRanges))
        << ",\"stages\":{";
    for (std::size_t i = 0; i < kSenderWorkStages; ++i) {
        if (i) out << ',';
        out << '"' << names[i] << "\":{\"calls\":" << total.calls[i] << ",\"sum_seconds\":" << total.sums[i]
            << ",\"union_seconds\":" << memberWorkUnionSeconds(std::move(ranges[i])) << '}';
    }
    out << "},\"lanes\":[";
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        if (lane) out << ',';
        out << "{\"index\":" << lane << ",\"completed_members\":" << lanes[lane].completedMembers << ",\"stage_sums\":{";
        for (std::size_t i = 0; i < kSenderWorkStages; ++i) {
            if (i) out << ',';
            out << '"' << names[i] << "\":" << lanes[lane].sums[i];
        }
        out << "}}";
    }
    out << "]}";
}
}  // namespace cpnetflux::core::io
