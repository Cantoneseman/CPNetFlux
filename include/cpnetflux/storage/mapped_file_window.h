#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cpnetflux/common/status.h"

namespace cpnetflux::storage {
struct MappedWriteStats {
    std::uint64_t mapCalls = 0, mapFailures = 0, unmapCalls = 0;
    std::uint64_t writeCalls = 0, writtenBytes = 0;
    std::uint64_t activeMappedBytes = 0, peakMappedBytes = 0;
};

// One range attempt owns this object; its writing pin protects the borrowed fd.
class MappedFileWindow {
 public:
    static constexpr std::uint64_t kWindowBytes = 64ULL * 1024 * 1024;
    MappedFileWindow(int fd, std::uint64_t fileSize) : fd_(fd), fileSize_(fileSize) {}
    ~MappedFileWindow();
    MappedFileWindow(const MappedFileWindow&) = delete;
    MappedFileWindow& operator=(const MappedFileWindow&) = delete;
    common::Status write(std::uint64_t offset, std::span<const std::uint8_t> bytes);
    common::Status close();
    const MappedWriteStats& stats() const { return stats_; }
 private:
    int fd_;
    std::uint64_t fileSize_, base_ = 0;
    void* mapping_ = nullptr;
    bool mapped_ = false;
    MappedWriteStats stats_;
};
}  // namespace cpnetflux::storage
