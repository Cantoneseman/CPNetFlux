#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/scheduler/work_item.h"

namespace cpnetflux::core::scheduler {

struct CompressionAdvisorConfig {
    double poorRatioThreshold = 0.85;
    double cpuHighThreshold = 85.0;
    double lowReadyRatio = 0.5;
    double minCompressGbps = 1.0;
    std::uint64_t sampleBlockBytes = 1024ULL * 1024ULL;
};

struct CompressionSampleRecord {
    std::string fileId;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::uint64_t sampleRawBytes = 0;
    std::uint64_t sampleCompressedBytes = 0;
    double sampleRatio = 1.0;
    double sampleCompGbps = 0.0;
    std::string decision = "raw";
    std::string reason = "sample_unavailable";
    double cpuPercent = 0.0;
    double linkReadyRatio = 0.0;
};

class CompressionAdvisor {
   public:
    CompressionAdvisor() = default;
    explicit CompressionAdvisor(CompressionAdvisorConfig config);

    [[nodiscard]] common::Result<CompressionSampleRecord> analyzeFile(
        const std::filesystem::path& path, const std::string& fileId, std::uint64_t fileSize,
        std::uint64_t resumeGeneration, double cpuPercent, double linkReadyRatio,
        bool allowSampling) const;

    [[nodiscard]] CompressionDecision decide(double sampleRatio, double sampleCompGbps,
                                             double cpuPercent, double linkReadyRatio) const;

    [[nodiscard]] const CompressionAdvisorConfig& config() const noexcept;

   private:
    CompressionAdvisorConfig config_;
};

}  // namespace cpnetflux::core::scheduler
