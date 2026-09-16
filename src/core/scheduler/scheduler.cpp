#include "cpnetflux/core/scheduler/scheduler.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace cpnetflux::core::scheduler {
namespace {

std::string defaultTaskId(const SchedulerConfig& config) {
    if (!config.taskId.empty()) {
        return config.taskId;
    }
    std::ostringstream output;
    output << "cpnetflux-global-scheduler-" << config.linkId;
    return output.str();
}

}  // namespace

common::Result<GlobalScheduler> GlobalScheduler::create(SchedulerConfig config) {
    if (config.linkId.empty()) {
        return common::Status::invalidArgument("scheduler link id must not be empty");
    }
    if (config.capacityGbps <= 0.0) {
        return common::Status::invalidArgument("scheduler capacity must be greater than zero");
    }
    if (config.workItemMinBytes == 0 || config.workItemMaxBytes == 0) {
        return common::Status::invalidArgument("scheduler work item bytes must be greater than zero");
    }
    if (config.workItemMinBytes > config.workItemMaxBytes) {
        return common::Status::invalidArgument("scheduler work item min exceeds max");
    }
    if (config.initialConnections == 0 || config.maxConnections == 0 ||
        config.initialConnections > config.maxConnections) {
        return common::Status::invalidArgument("scheduler connection bounds are invalid");
    }
    config.taskId = defaultTaskId(config);
    config.initialConnections = std::max<std::uint32_t>(1, config.initialConnections);
    config.maxConnections = std::max<std::uint32_t>(config.initialConnections, config.maxConnections);

    LinkProfile profile;
    profile.linkId = config.linkId;
    profile.remoteHost = config.remoteHost;
    profile.dataPortRange = config.dataPortRange;
    profile.localBindAddr = config.localBindAddr;
    profile.capacityGbps = config.capacityGbps;
    profile.initialConnections = config.initialConnections;
    profile.maxConnections = config.maxConnections;
    LinkState profileState;
    profileState.profile = profile;
    LinkManager targetCalculator;
    profile.targetQueueBytes =
        targetCalculator.targetQueueBytes(profileState, config.workItemMinBytes,
                                          config.defaultRttMs);

    auto metricsWriterResult = MetricsWriter::open(config.metricsPaths);
    if (!metricsWriterResult.isOk()) {
        return metricsWriterResult.status();
    }
    MetricsWriter metricsWriter = std::move(metricsWriterResult.value());
    WorkItemPlanner planner({config.workItemMinBytes, config.workItemMaxBytes, 0,
                             SchedulerDirection::Upload, config.taskId});
    LinkManager linkManager;
    const common::Status addStatus = linkManager.addLink(profile);
    if (!addStatus.isOk()) {
        return addStatus;
    }
    FeedbackController feedbackController(FeedbackControllerConfig{config.policy,
                                                                   config.initialConnections,
                                                                   config.maxConnections});
    CompressionAdvisor compressionAdvisor(
        CompressionAdvisorConfig{0.85, 85.0, 0.5, config.minCompressGbps});
    return GlobalScheduler(std::move(config), profile, std::move(metricsWriter), std::move(planner),
                           std::move(linkManager), std::move(feedbackController),
                           std::move(compressionAdvisor));
}

GlobalScheduler::GlobalScheduler(SchedulerConfig config, LinkProfile linkProfile,
                                 MetricsWriter metricsWriter, WorkItemPlanner planner,
                                 LinkManager linkManager, FeedbackController feedbackController,
                                 CompressionAdvisor compressionAdvisor)
    : config_(std::move(config)),
      linkProfile_(std::move(linkProfile)),
      metricsWriter_(std::move(metricsWriter)),
      planner_(std::move(planner)),
      linkManager_(std::move(linkManager)),
      feedbackController_(std::move(feedbackController)),
      compressionAdvisor_(std::move(compressionAdvisor)) {}

const SchedulerConfig& GlobalScheduler::config() const noexcept { return config_; }

LinkManager& GlobalScheduler::linkManager() noexcept { return linkManager_; }

const LinkManager& GlobalScheduler::linkManager() const noexcept { return linkManager_; }

FeedbackController& GlobalScheduler::feedbackController() noexcept { return feedbackController_; }

const FeedbackController& GlobalScheduler::feedbackController() const noexcept {
    return feedbackController_;
}

WorkItemPlanner& GlobalScheduler::workItemPlanner() noexcept { return planner_; }

const WorkItemPlanner& GlobalScheduler::workItemPlanner() const noexcept { return planner_; }

CompressionAdvisor& GlobalScheduler::compressionAdvisor() noexcept { return compressionAdvisor_; }

const CompressionAdvisor& GlobalScheduler::compressionAdvisor() const noexcept {
    return compressionAdvisor_;
}

MetricsWriter& GlobalScheduler::metricsWriter() noexcept { return metricsWriter_; }

const MetricsWriter& GlobalScheduler::metricsWriter() const noexcept { return metricsWriter_; }

const std::string& GlobalScheduler::taskId() const noexcept { return config_.taskId; }

common::Result<std::vector<FilePlan>> GlobalScheduler::planManifest(
    const core::tree::TreeManifest& manifest, const std::filesystem::path& rootPath,
    SchedulerDirection direction, bool allowSampling) {
    std::vector<FilePlan> plans;
    plans.reserve(manifest.files.size());
    WeightedRoundRobinTaskQueue queue;
    std::unordered_map<std::string, std::size_t> planIndexByFileId;
    std::uint64_t resumeGeneration = manifest.updatedAtUnixNanos;
    const LinkState* link = linkManager_.link(config_.linkId);
    const double observedReadyRatio =
        link == nullptr ? 1.0 : linkManager_.queueFillRatio(*link);
    const double linkReadyRatio =
        link == nullptr || link->readyBytes == 0 ? 1.0 : observedReadyRatio;
    for (std::size_t manifestIndex = 0; manifestIndex < manifest.files.size(); ++manifestIndex) {
        const core::tree::TreeFileRecord& record = manifest.files[manifestIndex];
        if (record.status == core::tree::TreeFileStatus::Completed ||
            record.status == core::tree::TreeFileStatus::Changed) {
            continue;
        }

        CompressionSampleRecord sample;
        sample.fileId = record.transferId;
        sample.reason = "sample_unavailable";
        sample.decision = compressionDispositionName(CompressionDisposition::Raw);
        if (config_.enableCompression && allowSampling && direction == SchedulerDirection::Upload) {
            auto analyzed = compressionAdvisor_.analyzeFile(rootPath / record.relativePath,
                                                            record.transferId, record.size,
                                                            resumeGeneration, 0.0,
                                                            linkReadyRatio, true);
            if (!analyzed.isOk()) {
                return analyzed.status();
            }
            sample = analyzed.value();
        } else {
            sample.offset = 0;
            sample.length = record.size;
            sample.cpuPercent = 0.0;
            sample.linkReadyRatio = linkReadyRatio;
        }
        CompressionDecision decision;
        decision.sampleRatio = sample.sampleRatio;
        decision.sampleCompGbps = sample.sampleCompGbps;
        decision.cpuPercent = sample.cpuPercent;
        decision.linkReadyRatio = sample.linkReadyRatio;
        decision.sampleRawBytes = sample.sampleRawBytes;
        decision.sampleCompressedBytes = sample.sampleCompressedBytes;
        decision.reason = sample.reason;
        decision.disposition =
            sample.decision == compressionDispositionName(CompressionDisposition::CompressionCandidate)
                ? CompressionDisposition::CompressionCandidate
                : CompressionDisposition::Raw;
        auto sampleStatus = metricsWriter_.writeSample(SchedulerSampleRecordOut{
            sample.fileId, sample.offset, sample.length, sample.sampleRatio, sample.sampleCompGbps,
            sample.decision, sample.reason, sample.cpuPercent, sample.linkReadyRatio});
        if (!sampleStatus.isOk()) {
            return sampleStatus;
        }
        const common::Status compressionEventStatus = metricsWriter_.writeEvent(
            SchedulerEventRecord{
                "",
                decision.disposition == CompressionDisposition::CompressionCandidate
                    ? "compression_candidate"
                    : "compression_reject",
                config_.taskId,
                record.transferId,
                config_.linkId,
                sample.offset,
                sample.length,
                decision.reason,
                0,
                feedbackController_.currentConnections(),
                decision.disposition == CompressionDisposition::CompressionCandidate ? "candidate"
                                                                                     : "raw",
                decision.disposition == CompressionDisposition::CompressionCandidate
                    ? "candidate"
                    : "raw"});
        if (!compressionEventStatus.isOk()) {
            return compressionEventStatus;
        }

        WorkItemPlanner directedPlanner({config_.workItemMinBytes, config_.workItemMaxBytes,
                                         resumeGeneration, direction, config_.taskId});
        auto filePlan = directedPlanner.planTreeFile(record, config_.taskId,
                                                     manifest.checksumAlgorithm, decision,
                                                     resumeGeneration);
        if (!filePlan.isOk()) {
            return filePlan.status();
        }
        filePlan.value().manifestIndex = manifestIndex;
        const std::size_t index = plans.size();
        planIndexByFileId.emplace(filePlan.value().fileId, index);
        queue.push(filePlan.value().workItems.front());
        plans.push_back(std::move(filePlan.value()));
    }

    std::vector<FilePlan> orderedPlans;
    orderedPlans.reserve(plans.size());
    while (auto workItem = queue.pop()) {
        auto found = planIndexByFileId.find(workItem->fileId);
        if (found == planIndexByFileId.end()) {
            continue;
        }
        orderedPlans.push_back(plans[found->second]);
    }
    if (orderedPlans.empty()) {
        orderedPlans = std::move(plans);
    }
    return orderedPlans;
}

common::Status GlobalScheduler::recordDispatch(const SchedulerEventRecord& record) {
    return metricsWriter_.writeEvent(record);
}

common::Status GlobalScheduler::recordRetry(const SchedulerEventRecord& record) {
    return metricsWriter_.writeEvent(record);
}

common::Status GlobalScheduler::recordFailed(const SchedulerEventRecord& record) {
    return metricsWriter_.writeEvent(record);
}

common::Status GlobalScheduler::recordExecutorComplete(const SchedulerEventRecord& record) {
    return metricsWriter_.writeEvent(record);
}

common::Status GlobalScheduler::recordLinkEvent(const SchedulerEventRecord& record) {
    return metricsWriter_.writeEvent(record);
}

common::Status GlobalScheduler::recordSample(const SchedulerSampleRecordOut& record) {
    return metricsWriter_.writeSample(record);
}

common::Status GlobalScheduler::writeSummary(const SchedulerSummaryRecord& record) {
    return metricsWriter_.writeSummary(record);
}

std::string GlobalScheduler::dominantBottleneck(const SchedulerSummaryRecord& summary,
                                                const LinkState& state) const {
    const double wireUtilization = config_.capacityGbps > 0.0
                                       ? summary.wireGbps / config_.capacityGbps
                                       : 0.0;
    if (state.sendPressure > 0.5 || state.maxSendPressure > 0.5) {
        return "network_or_receiver_backpressure";
    }
    if (state.writePressure > 0.5 || state.maxWritePressure > 0.5) {
        return "receiver_write";
    }
    if (state.cpuPressure > 85.0 || state.maxCpuPressure > 85.0) {
        return "cpu";
    }
    if (summary.compressionRatioEffective < 0.95 && summary.compressedWorkItems > 0) {
        return "compression";
    }
    if (wireUtilization < 0.5 && state.queueFillRatio < 0.5) {
        return "scheduler_supply";
    }
    if (wireUtilization < 0.5 && state.queueFillRatio >= 0.5) {
        return "network_or_receiver_backpressure";
    }
    return "unknown_or_balanced";
}

}  // namespace cpnetflux::core::scheduler
