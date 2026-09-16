#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/scheduler/work_item.h"
#include "cpnetflux/core/tree/tree_manifest.h"

namespace cpnetflux::core::scheduler {

struct WorkItemPlannerConfig {
    std::uint64_t minBytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t maxBytes = 256ULL * 1024ULL * 1024ULL;
    std::uint64_t resumeGeneration = 0;
    SchedulerDirection direction = SchedulerDirection::Upload;
    std::string taskId;
};

struct FilePlan {
    std::size_t manifestIndex = 0;
    std::string taskId;
    std::string fileId;
    std::string relativePath;
    std::uint64_t totalBytes = 0;
    std::uint64_t resumeGeneration = 0;
    std::uint32_t retryCount = 0;
    std::uint32_t targetConnections = 1;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    CompressionDecision compression;
    std::vector<WorkItem> workItems;
};

class WorkItemPlanner {
   public:
    WorkItemPlanner() = default;
    explicit WorkItemPlanner(WorkItemPlannerConfig config);

    [[nodiscard]] common::Result<std::vector<WorkItem>> planFile(
        const std::string& fileId, const std::string& relativePath, std::uint64_t fileSize,
        checksum::ChecksumAlgorithm checksumAlgorithm, const CompressionDecision& compression,
        std::uint32_t retryCount) const;

    [[nodiscard]] common::Result<FilePlan> planTreeFile(
        const core::tree::TreeFileRecord& record, const std::string& taskId,
        checksum::ChecksumAlgorithm checksumAlgorithm, const CompressionDecision& compression,
        std::uint64_t resumeGeneration) const;

    [[nodiscard]] common::Result<std::vector<FilePlan>> planManifest(
        const core::tree::TreeManifest& manifest, const std::string& taskId,
        checksum::ChecksumAlgorithm checksumAlgorithm, const CompressionDecision& compression,
        std::uint64_t resumeGeneration) const;

    [[nodiscard]] const WorkItemPlannerConfig& config() const noexcept;

   private:
    WorkItemPlannerConfig config_;
};

}  // namespace cpnetflux::core::scheduler
