#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cpnetflux/common/status.h"

namespace cpnetflux::storage {
struct MappedWriteProfile {
    bool enabled = false;
    std::uint64_t copyCalls = 0, copyBytes = 0, errors = 0;
    std::uint64_t copyMinorFaults = 0, copyMajorFaults = 0;
    double mapCpuSeconds = 0, mapWallSeconds = 0;
    double unmapCpuSeconds = 0, unmapWallSeconds = 0;
    double copyCpuSeconds = 0, copyWallSeconds = 0;
};
struct MappedWriteStats {
    std::uint64_t mapCalls = 0, mapFailures = 0, unmapCalls = 0;
    std::uint64_t writeCalls = 0, writtenBytes = 0;
    std::uint64_t activeMappedBytes = 0, peakMappedBytes = 0;
    MappedWriteProfile profile;
};

// One range attempt owns this object; its writing pin protects the borrowed fd.
class MappedFileWindow {
 public:
    static constexpr std::uint64_t kWindowBytes = 64ULL * 1024 * 1024;
    MappedFileWindow(int fd, std::uint64_t fileSize, bool profile = false)
        : fd_(fd), fileSize_(fileSize) { stats_.profile.enabled = profile; }
    ~MappedFileWindow();
    MappedFileWindow(const MappedFileWindow&) = delete;
    MappedFileWindow& operator=(const MappedFileWindow&) = delete;
    common::Status write(std::uint64_t offset, std::span<const std::uint8_t> bytes);
    // Tentative writable storage, not certified bytes. At most one64MiB mapping.
    // View ends at a window boundary and expires on next borrow/write/close.
    // Caller holds the attempt writing pin/IO lease until network IO and CRC end.
    common::Result<std::span<std::uint8_t>> writableSpan(std::uint64_t offset, std::size_t requested);
    common::Status close();
    const MappedWriteStats& stats() const { return stats_; }
 private:
    common::Status ensureMapped(std::uint64_t offset);
    int fd_;
    std::uint64_t fileSize_, base_ = 0;
    void* mapping_ = nullptr;
    bool mapped_ = false;
    MappedWriteStats stats_;
};
}  // namespace cpnetflux::storage
