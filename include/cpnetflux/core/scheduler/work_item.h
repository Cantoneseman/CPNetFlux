#pragma once

#include <cstdint>
#include <string>

#include "cpnetflux/checksum/checksum.h"

namespace cpnetflux::core::scheduler {

enum class SchedulerDirection {
    Upload,
    Download,
};

enum class CompressionDisposition {
    Raw,
    CompressionCandidate,
};

struct CompressionDecision {
    CompressionDisposition disposition = CompressionDisposition::Raw;
    std::string reason = "raw";
    double sampleRatio = 1.0;
    double sampleCompGbps = 0.0;
    double cpuPercent = 0.0;
    double linkReadyRatio = 0.0;
    std::uint64_t sampleRawBytes = 0;
    std::uint64_t sampleCompressedBytes = 0;
};

struct WorkItem {
    std::string taskId;
    std::string fileId;
    std::string relativePath;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    SchedulerDirection direction = SchedulerDirection::Upload;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    CompressionDecision compression;
    std::uint32_t retryCount = 0;
    std::uint64_t resumeGeneration = 0;
    bool dispatchable = true;
};

[[nodiscard]] const char* schedulerDirectionName(SchedulerDirection direction) noexcept;
[[nodiscard]] const char* compressionDispositionName(CompressionDisposition disposition) noexcept;

}  // namespace cpnetflux::core::scheduler
