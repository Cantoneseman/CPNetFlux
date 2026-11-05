#pragma once
#include <chrono>
#include <cstdint>

namespace cpnetflux::checkpoint {
// Caller-owned, not shared between concurrent lanes. Counts include failed attempts.
struct ManifestSaveStats {
    std::uint64_t attempts = 0, serializeCalls = 0, serializedBytes = 0;
    std::uint64_t tempWriteCalls = 0, renameCalls = 0, publishCalls = 0;
    std::uint64_t publishSuccesses = 0, failures = 0;
    double saveSeconds = 0, serializeSeconds = 0, tempWriteSeconds = 0, renameSeconds = 0;
    std::uint64_t recordPreparationCalls = 0, recordSortCalls = 0, recordCopyItems = 0;
    // Disjoint serialization substeps; scalar headers/body assembly remain in total serializeSeconds.
    double recordPreparationSeconds = 0, rangeValidationSeconds = 0;
    double recordFormatSeconds = 0, bodyCrcSeconds = 0;
};
class OptionalManifestTimer {
 public:
    explicit OptionalManifestTimer(double* target) : target_(target) {
        if (target_) started_ = std::chrono::steady_clock::now();
    }
    void stop() {
        if (target_) {
            *target_ += std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count();
            target_ = nullptr;
        }
    }
    ~OptionalManifestTimer() { stop(); }
 private:
    double* target_;
    std::chrono::steady_clock::time_point started_{};
};
class ManifestSaveAttempt {
 public:
    explicit ManifestSaveAttempt(ManifestSaveStats* stats) : stats_(stats) {
        if (stats_) ++stats_->attempts;
    }
    void succeed() { succeeded_ = true; }
    ~ManifestSaveAttempt() { if (stats_ && !succeeded_) ++stats_->failures; }
 private:
    ManifestSaveStats* stats_;
    bool succeeded_ = false;
};
}  // namespace cpnetflux::checkpoint
