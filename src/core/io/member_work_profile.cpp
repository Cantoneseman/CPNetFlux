#include "cpnetflux/core/io/member_work_profile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ostream>

namespace cpnetflux::core::io {
namespace {
constexpr const char* names[] = {"thread_create", "join_payload_wait", "thread_join_tail",
                                "transaction_begin", "commit", "result_write"};
bool valid(const MemberWorkInterval& interval) {
    return std::isfinite(interval.begin) && std::isfinite(interval.end) &&
        interval.begin >= 0 && interval.end >= interval.begin;
}
void addSaves(checkpoint::ManifestSaveStats& a, const checkpoint::ManifestSaveStats& b) {
    a.attempts += b.attempts; a.failures += b.failures;
    a.publishSuccesses += b.publishSuccesses; a.serializedBytes += b.serializedBytes;
    a.saveSeconds += b.saveSeconds; a.serializeSeconds += b.serializeSeconds;
    a.recordPreparationSeconds += b.recordPreparationSeconds;
    a.recordFormatSeconds += b.recordFormatSeconds; a.bodyCrcSeconds += b.bodyCrcSeconds;
    a.tempWriteSeconds += b.tempWriteSeconds; a.renameSeconds += b.renameSeconds;
}
}
double memberWorkNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void MemberWorkSample::set(MemberWorkStage stage, double begin, double end) {
    intervals[static_cast<std::size_t>(stage)] = {begin, end, stage};
}
void MemberWorkSample::splitJoin(double begin, double workerLoopExit, double end) {
    const auto split = std::clamp(workerLoopExit, begin, end);
    set(MemberWorkStage::JoinPayloadWait, begin, split);
    set(MemberWorkStage::ThreadJoinTail, split, end);
}
void MemberWorkProfile::record(const MemberWorkSample& sample, const checkpoint::ManifestSaveStats& saves) {
    if (!enabled) return;
    ++completedMembers;
    workerEntrySeconds += sample.workerEntrySeconds;
    addSaves(manifestSave, saves);
    for (const auto& interval : sample.intervals) {
        const auto index = static_cast<std::size_t>(interval.stage);
        if (index >= kMemberWorkStages) continue;
        if (!valid(interval)) { truncated = true; continue; }
        ++calls[index]; sums[index] += interval.end - interval.begin;
        if (intervals.size() == kMaxIntervals) { truncated = true; continue; }
        intervals.push_back(interval);
    }
}
double memberWorkUnionSeconds(std::vector<MemberWorkInterval> intervals) {
    intervals.erase(std::remove_if(intervals.begin(), intervals.end(),
        [](const auto& interval) { return !valid(interval); }), intervals.end());
    std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) { return a.begin < b.begin; });
    double total = 0, begin = 0, end = 0;
    for (const auto& interval : intervals) {
        if (interval.begin > end) { total += end - begin; begin = interval.begin; }
        end = std::max(end, interval.end);
    }
    return total + end - begin;
}
void appendMemberWorkProfile(std::ostream& out, bool enabled, bool receiver,
                             const std::vector<MemberWorkProfile>& lanes) {
    out << "{\"enabled\":" << (enabled ? "true" : "false");
    if (!enabled) { out << '}'; return; }
    MemberWorkProfile total;
    std::array<std::vector<MemberWorkInterval>, kMemberWorkStages> ranges;
    std::vector<MemberWorkInterval> threadRanges;
    for (const auto& lane : lanes) {
        total.truncated |= lane.truncated;
        total.startedMembers += lane.startedMembers; total.completedMembers += lane.completedMembers;
        total.excludedRanges += lane.excludedRanges; total.workerEntrySeconds += lane.workerEntrySeconds;
        addSaves(total.manifestSave, lane.manifestSave);
        for (std::size_t i = 0; i < kMemberWorkStages; ++i) {
            total.calls[i] += lane.calls[i]; total.sums[i] += lane.sums[i];
        }
        for (const auto& interval : lane.intervals) {
            ranges[static_cast<std::size_t>(interval.stage)].push_back(interval);
            if (interval.stage == MemberWorkStage::ThreadCreate || interval.stage == MemberWorkStage::ThreadJoinTail)
                threadRanges.push_back(interval);
        }
    }
    out << ",\"effective\":\"" << (receiver ? "receiver" : "not_used_sender")
        << "\",\"scope\":\"completed_local_whole_file_attempts_excludes_final_audit\""
        << ",\"truncated\":" << (total.truncated ? "true" : "false")
        << ",\"started_members\":" << total.startedMembers << ",\"completed_members\":" << total.completedMembers
        << ",\"excluded_ranges\":" << total.excludedRanges
        << ",\"worker_entry_sum_seconds\":" << total.workerEntrySeconds
        << ",\"thread_envelope_union_seconds\":" << memberWorkUnionSeconds(std::move(threadRanges))
        << ",\"stages\":{";
    for (std::size_t i = 0; i < kMemberWorkStages; ++i) {
        if (i) out << ',';
        out << '"' << names[i] << "\":{\"calls\":" << total.calls[i] << ",\"sum_seconds\":" << total.sums[i]
            << ",\"union_seconds\":" << memberWorkUnionSeconds(std::move(ranges[i])) << '}';
    }
    const auto& p = total.manifestSave;
    out << "},\"manifest_save\":{\"attempts\":" << p.attempts << ",\"failures\":" << p.failures
        << ",\"publish_successes\":" << p.publishSuccesses << ",\"serialized_bytes\":" << p.serializedBytes
        << ",\"save_sum_seconds\":" << p.saveSeconds << ",\"serialize_sum_seconds\":" << p.serializeSeconds
        << ",\"record_prepare_sum_seconds\":" << p.recordPreparationSeconds
        << ",\"record_format_sum_seconds\":" << p.recordFormatSeconds << ",\"body_crc_sum_seconds\":" << p.bodyCrcSeconds
        << ",\"temp_write_sum_seconds\":" << p.tempWriteSeconds << ",\"rename_sum_seconds\":" << p.renameSeconds
        << "},\"lanes\":[";
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        if (lane) out << ',';
        out << "{\"index\":" << lane << ",\"completed_members\":" << lanes[lane].completedMembers << ",\"stage_sums\":{";
        for (std::size_t i = 0; i < kMemberWorkStages; ++i) {
            if (i) out << ',';
            out << '"' << names[i] << "\":" << lanes[lane].sums[i];
        }
        out << "}}";
    }
    out << "]}";
}
}  // namespace cpnetflux::core::io
