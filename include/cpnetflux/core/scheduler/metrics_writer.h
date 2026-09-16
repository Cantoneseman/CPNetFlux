#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/scheduler/compression_advisor.h"
#include "cpnetflux/core/scheduler/link_profile.h"

namespace cpnetflux::core::scheduler {

struct SchedulerSummaryRecord {
    std::string taskId;
    std::uint64_t totalBytes = 0;
    std::uint64_t logicalBytes = 0;
    std::uint64_t wireBytes = 0;
    double elapsedSeconds = 0.0;
    double goodputGbps = 0.0;
    double wireGbps = 0.0;
    double compressionRatioEffective = 1.0;
    std::uint64_t compressionAttempts = 0;
    std::uint64_t rawWorkItems = 0;
    std::uint64_t compressedWorkItems = 0;
    std::uint64_t rawFallbackWorkItems = 0;
    std::uint64_t compressionFailures = 0;
    std::uint64_t decompressionFailures = 0;
    std::uint64_t retriedWorkItems = 0;
    std::string result = "pass";
    std::string dominantBottleneck = "unknown_or_balanced";
    std::string policy = "fixed";
    std::uint32_t initialConnections = 1;
    std::uint32_t currentConnections = 1;
    std::uint32_t targetConnections = 1;
    std::uint32_t maxConnections = 1;
    std::uint64_t rampUpCount = 0;
    std::uint64_t rampDownCount = 0;
    std::uint64_t queueLowCount = 0;
    std::uint64_t queueHighCount = 0;
    std::uint64_t sendPressureCount = 0;
    std::uint64_t writePressureCount = 0;
    std::uint64_t cpuPressureCount = 0;
    std::uint64_t retryCount = 0;
    double maxSendPressure = 0.0;
    double maxWritePressure = 0.0;
    double maxCpuPressure = 0.0;
};

struct SchedulerEventRecord {
    std::string ts;
    std::string event;
    std::string taskId;
    std::string fileId;
    std::string linkId;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::string reason;
    std::uint64_t readyBytes = 0;
    std::uint32_t targetConnections = 0;
    std::string result;
    std::string message;
};

struct SchedulerSampleRecordOut {
    std::string fileId;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    double sampleRatio = 1.0;
    double sampleCompGbps = 0.0;
    std::string decision = "raw";
    std::string reason = "sample_unavailable";
    double cpuPercent = 0.0;
    double linkReadyRatio = 0.0;
};

struct SchedulerMetricsPaths {
    std::string summaryCsv;
    std::string eventsJsonl;
    std::string samplesCsv;
};

class MetricsWriter {
   public:
    MetricsWriter() = default;
    MetricsWriter(const MetricsWriter&) = delete;
    MetricsWriter& operator=(const MetricsWriter&) = delete;
    MetricsWriter(MetricsWriter&& other) noexcept;
    MetricsWriter& operator=(MetricsWriter&& other) noexcept;

    static common::Result<MetricsWriter> open(SchedulerMetricsPaths paths);

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] const SchedulerMetricsPaths& paths() const noexcept;

    [[nodiscard]] common::Status writeSummary(const SchedulerSummaryRecord& record);
    [[nodiscard]] common::Status writeEvent(const SchedulerEventRecord& record);
    [[nodiscard]] common::Status writeSample(const SchedulerSampleRecordOut& record);

   private:
    explicit MetricsWriter(SchedulerMetricsPaths paths);

    static std::string csvEscape(const std::string& value);
    static std::string jsonEscape(const std::string& value);
    static std::string timestampUtc();

    common::Status writeSummaryHeader() const;
    common::Status writeSampleHeader() const;
    common::Status createParentDirectories() const;

    SchedulerMetricsPaths paths_;
    mutable std::mutex mutex_;
};

}  // namespace cpnetflux::core::scheduler
