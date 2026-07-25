#include "gridflux/core/metrics/transfer_phase_stats.h"

#include <ostream>

namespace gridflux::core::metrics {
namespace {

std::uint64_t nanosFromDuration(std::chrono::steady_clock::duration duration) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

void appendPhase(std::ostream& stream, const char* name, const TransferPhaseStats& stats,
                 TransferPhase phase) {
    stream << " stage_" << name << "_seconds=" << stats.seconds(phase) << " stage_" << name
           << "_bytes=" << stats.bytes(phase);
}

void appendAlias(std::ostream& stream, const char* name, const TransferPhaseStats& stats,
                 TransferPhase phase) {
    stream << ' ' << name << "_seconds=" << stats.seconds(phase) << ' ' << name
           << "_bytes=" << stats.bytes(phase);
}

}  // namespace

void TransferPhaseStats::add(TransferPhase phase, std::chrono::steady_clock::duration duration,
                             std::uint64_t bytes) noexcept {
    Counters& phaseCounters = counters(phase);
    phaseCounters.nanos.fetch_add(nanosFromDuration(duration), std::memory_order_relaxed);
    phaseCounters.bytes.fetch_add(bytes, std::memory_order_relaxed);
}

double TransferPhaseStats::seconds(TransferPhase phase) const noexcept {
    return static_cast<double>(counters(phase).nanos.load(std::memory_order_relaxed)) /
           1000000000.0;
}

std::uint64_t TransferPhaseStats::bytes(TransferPhase phase) const noexcept {
    return counters(phase).bytes.load(std::memory_order_relaxed);
}

TransferPhaseStats::Counters& TransferPhaseStats::counters(TransferPhase phase) noexcept {
    switch (phase) {
        case TransferPhase::Recv:
            return recv_;
        case TransferPhase::Send:
            return send_;
        case TransferPhase::Read:
            return read_;
        case TransferPhase::Write:
            return write_;
        case TransferPhase::Checksum:
            return checksum_;
        case TransferPhase::ManifestFlush:
            return manifestFlush_;
        case TransferPhase::ResumePrecheck:
            return resumePrecheck_;
        case TransferPhase::FinalVerify:
            return finalVerify_;
        case TransferPhase::RenameCommit:
            return renameCommit_;
        case TransferPhase::Overall:
            return overall_;
    }
    return overall_;
}

const TransferPhaseStats::Counters& TransferPhaseStats::counters(TransferPhase phase) const
    noexcept {
    switch (phase) {
        case TransferPhase::Recv:
            return recv_;
        case TransferPhase::Send:
            return send_;
        case TransferPhase::Read:
            return read_;
        case TransferPhase::Write:
            return write_;
        case TransferPhase::Checksum:
            return checksum_;
        case TransferPhase::ManifestFlush:
            return manifestFlush_;
        case TransferPhase::ResumePrecheck:
            return resumePrecheck_;
        case TransferPhase::FinalVerify:
            return finalVerify_;
        case TransferPhase::RenameCommit:
            return renameCommit_;
        case TransferPhase::Overall:
            return overall_;
    }
    return overall_;
}

ScopedPhaseTimer::ScopedPhaseTimer(TransferPhaseStats* stats, TransferPhase phase,
                                   std::uint64_t bytes) noexcept
    : stats_(stats), phase_(phase), bytes_(bytes), start_(std::chrono::steady_clock::now()) {}

ScopedPhaseTimer::~ScopedPhaseTimer() { stop(); }

void ScopedPhaseTimer::addBytes(std::uint64_t bytes) noexcept { bytes_ += bytes; }

void ScopedPhaseTimer::stop() noexcept {
    if (stopped_) {
        return;
    }
    stopped_ = true;
    if (stats_ == nullptr) {
        return;
    }
    stats_->add(phase_, std::chrono::steady_clock::now() - start_, bytes_);
}

void appendPhaseStats(std::ostream& stream, const TransferPhaseStats& stats) {
    appendPhase(stream, "recv", stats, TransferPhase::Recv);
    appendPhase(stream, "send", stats, TransferPhase::Send);
    appendPhase(stream, "read", stats, TransferPhase::Read);
    appendPhase(stream, "write", stats, TransferPhase::Write);
    appendPhase(stream, "checksum", stats, TransferPhase::Checksum);
    appendPhase(stream, "manifest_flush", stats, TransferPhase::ManifestFlush);
    appendPhase(stream, "resume_precheck", stats, TransferPhase::ResumePrecheck);
    appendPhase(stream, "final_verify", stats, TransferPhase::FinalVerify);
    appendPhase(stream, "rename_commit", stats, TransferPhase::RenameCommit);
    appendPhase(stream, "overall", stats, TransferPhase::Overall);
}

void appendStorReceiverAliases(std::ostream& stream, const TransferPhaseStats& stats) {
    appendAlias(stream, "data_receive", stats, TransferPhase::Recv);
    appendAlias(stream, "temp_write", stats, TransferPhase::Write);
    appendAlias(stream, "checksum", stats, TransferPhase::Checksum);
    appendAlias(stream, "manifest_flush", stats, TransferPhase::ManifestFlush);
    appendAlias(stream, "final_verify", stats, TransferPhase::FinalVerify);
    appendAlias(stream, "finalize_rename", stats, TransferPhase::RenameCommit);
}

void appendRetrSenderAliases(std::ostream& stream, const TransferPhaseStats& stats) {
    appendAlias(stream, "source_read", stats, TransferPhase::Read);
    appendAlias(stream, "network_send", stats, TransferPhase::Send);
    appendAlias(stream, "checksum", stats, TransferPhase::Checksum);
}

void appendRetrReceiverAliases(std::ostream& stream, const TransferPhaseStats& stats) {
    appendAlias(stream, "download_temp_write", stats, TransferPhase::Write);
    appendAlias(stream, "manifest_flush", stats, TransferPhase::ManifestFlush);
    appendAlias(stream, "final_verify", stats, TransferPhase::FinalVerify);
    appendAlias(stream, "finalize_rename", stats, TransferPhase::RenameCommit);
}

}  // namespace gridflux::core::metrics
