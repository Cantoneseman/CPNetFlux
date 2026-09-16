#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iosfwd>

namespace cpnetflux::core::metrics {

enum class TransferPhase {
    Recv,
    Send,
    Read,
    Write,
    Checksum,
    ManifestFlush,
    ResumePrecheck,
    FinalVerify,
    RenameCommit,
    Overall,
};

class TransferPhaseStats {
   public:
    void add(TransferPhase phase, std::chrono::steady_clock::duration duration,
             std::uint64_t bytes = 0) noexcept;

    [[nodiscard]] double seconds(TransferPhase phase) const noexcept;
    [[nodiscard]] std::uint64_t bytes(TransferPhase phase) const noexcept;

   private:
    struct Counters {
        std::atomic<std::uint64_t> nanos{0};
        std::atomic<std::uint64_t> bytes{0};
    };

    [[nodiscard]] Counters& counters(TransferPhase phase) noexcept;
    [[nodiscard]] const Counters& counters(TransferPhase phase) const noexcept;

    Counters recv_;
    Counters send_;
    Counters read_;
    Counters write_;
    Counters checksum_;
    Counters manifestFlush_;
    Counters resumePrecheck_;
    Counters finalVerify_;
    Counters renameCommit_;
    Counters overall_;
};

class ScopedPhaseTimer {
   public:
    ScopedPhaseTimer(TransferPhaseStats* stats, TransferPhase phase,
                     std::uint64_t bytes = 0) noexcept;
    ScopedPhaseTimer(const ScopedPhaseTimer&) = delete;
    ScopedPhaseTimer& operator=(const ScopedPhaseTimer&) = delete;
    ~ScopedPhaseTimer();

    void addBytes(std::uint64_t bytes) noexcept;
    void stop() noexcept;

   private:
    TransferPhaseStats* stats_;
    TransferPhase phase_;
    std::uint64_t bytes_;
    std::chrono::steady_clock::time_point start_;
    bool stopped_ = false;
};

void appendPhaseStats(std::ostream& stream, const TransferPhaseStats& stats);
void appendStorReceiverAliases(std::ostream& stream, const TransferPhaseStats& stats);
void appendRetrSenderAliases(std::ostream& stream, const TransferPhaseStats& stats);
void appendRetrReceiverAliases(std::ostream& stream, const TransferPhaseStats& stats);

}  // namespace cpnetflux::core::metrics
