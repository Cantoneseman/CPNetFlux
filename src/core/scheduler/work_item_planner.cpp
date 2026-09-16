#include "cpnetflux/core/scheduler/work_item_planner.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace cpnetflux::core::scheduler {
namespace {

constexpr std::uint64_t kZeroWorkItemLength = 0;

}  // namespace

WorkItemPlanner::WorkItemPlanner(WorkItemPlannerConfig config) : config_(std::move(config)) {}

common::Result<std::vector<WorkItem>> WorkItemPlanner::planFile(
    const std::string& fileId, const std::string& relativePath, std::uint64_t fileSize,
    checksum::ChecksumAlgorithm checksumAlgorithm, const CompressionDecision& compression,
    std::uint32_t retryCount) const {
    if (config_.minBytes == 0 || config_.maxBytes == 0) {
        return common::Status::invalidArgument("work item bounds must be greater than zero");
    }
    if (config_.minBytes > config_.maxBytes) {
        return common::Status::invalidArgument("work item min bytes exceeds max bytes");
    }

    std::vector<WorkItem> items;
    if (fileSize == 0) {
        items.push_back(WorkItem{config_.taskId, fileId, relativePath, 0, kZeroWorkItemLength,
                                 config_.direction, checksumAlgorithm, compression, retryCount,
                                 config_.resumeGeneration, true});
        return items;
    }

    const std::uint64_t dispatchSize = std::min(config_.maxBytes, fileSize);
    if (fileSize <= config_.minBytes || fileSize <= config_.maxBytes) {
        items.push_back(WorkItem{config_.taskId, fileId, relativePath, 0, fileSize,
                                 config_.direction, checksumAlgorithm, compression, retryCount,
                                 config_.resumeGeneration, true});
        return items;
    }

    std::uint64_t offset = 0;
    std::uint64_t index = 0;
    while (offset < fileSize) {
        const std::uint64_t remaining = fileSize - offset;
        const std::uint64_t length = std::min(dispatchSize, remaining);
        items.push_back(WorkItem{config_.taskId, fileId, relativePath, offset, length,
                                 config_.direction, checksumAlgorithm, compression, retryCount,
                                 config_.resumeGeneration, index == 0});
        offset += length;
        ++index;
    }

    if (!items.empty() && items.back().length < config_.minBytes && items.size() > 1) {
        const std::uint64_t tail = items.back().length;
        items[items.size() - 2].length += tail;
        items.pop_back();
    }

    return items;
}

common::Result<FilePlan> WorkItemPlanner::planTreeFile(
    const core::tree::TreeFileRecord& record, const std::string& taskId,
    checksum::ChecksumAlgorithm checksumAlgorithm, const CompressionDecision& compression,
    std::uint64_t resumeGeneration) const {
    const std::uint32_t retryCount = record.status == core::tree::TreeFileStatus::Pending ? 0 : 1;
    WorkItemPlanner planner({config_.minBytes, config_.maxBytes, resumeGeneration,
                             config_.direction, taskId});
    auto items = planner.planFile(record.transferId, record.relativePath, record.size,
                                  checksumAlgorithm, compression, retryCount);
    if (!items.isOk()) {
        return items.status();
    }

    FilePlan plan;
    plan.taskId = taskId;
    plan.fileId = record.transferId;
    plan.relativePath = record.relativePath;
    plan.totalBytes = record.size;
    plan.resumeGeneration = resumeGeneration;
    plan.retryCount = retryCount;
    plan.checksumAlgorithm = checksumAlgorithm;
    plan.compression = compression;
    plan.workItems = std::move(items.value());
    return plan;
}

common::Result<std::vector<FilePlan>> WorkItemPlanner::planManifest(
    const core::tree::TreeManifest& manifest, const std::string& taskId,
    checksum::ChecksumAlgorithm checksumAlgorithm, const CompressionDecision& compression,
    std::uint64_t resumeGeneration) const {
    std::vector<FilePlan> plans;
    plans.reserve(manifest.files.size());
    for (const core::tree::TreeFileRecord& record : manifest.files) {
        if (record.status == core::tree::TreeFileStatus::Completed ||
            record.status == core::tree::TreeFileStatus::Changed) {
            continue;
        }
        auto plan = planTreeFile(record, taskId, checksumAlgorithm, compression,
                                 resumeGeneration);
        if (!plan.isOk()) {
            return plan.status();
        }
        plans.push_back(std::move(plan.value()));
    }
    return plans;
}

const WorkItemPlannerConfig& WorkItemPlanner::config() const noexcept { return config_; }

const char* schedulerDirectionName(SchedulerDirection direction) noexcept {
    switch (direction) {
        case SchedulerDirection::Upload:
            return "upload";
        case SchedulerDirection::Download:
            return "download";
    }
    return "upload";
}

const char* compressionDispositionName(CompressionDisposition disposition) noexcept {
    switch (disposition) {
        case CompressionDisposition::Raw:
            return "raw";
        case CompressionDisposition::CompressionCandidate:
            return "compression_candidate";
    }
    return "raw";
}

}  // namespace cpnetflux::core::scheduler
