#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/scheduler/compression_advisor.h"
#include "cpnetflux/core/scheduler/feedback_controller.h"
#include "cpnetflux/core/scheduler/link_manager.h"
#include "cpnetflux/core/scheduler/metrics_writer.h"
#include "cpnetflux/core/scheduler/task_queue.h"
#include "cpnetflux/core/scheduler/work_item_planner.h"

namespace cpnetflux::core::scheduler {

struct SchedulerConfig {
    std::string taskId;
    std::string linkId = "link0";
    std::string remoteHost;
    std::string dataPortRange;
    std::string localBindAddr;
    double capacityGbps = 0.01;
    SchedulerPolicy policy = SchedulerPolicy::Fixed;
    std::uint32_t initialConnections = 1;
    std::uint32_t maxConnections = 1;
    std::uint64_t workItemMinBytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t workItemMaxBytes = 256ULL * 1024ULL * 1024ULL;
    std::uint64_t defaultRttMs = 10;
    double minCompressGbps = 1.0;
    SchedulerMetricsPaths metricsPaths;
};

class GlobalScheduler {
   public:
    GlobalScheduler() = default;
    static common::Result<GlobalScheduler> create(SchedulerConfig config);

    [[nodiscard]] const SchedulerConfig& config() const noexcept;
    [[nodiscard]] LinkManager& linkManager() noexcept;
    [[nodiscard]] const LinkManager& linkManager() const noexcept;
    [[nodiscard]] FeedbackController& feedbackController() noexcept;
    [[nodiscard]] const FeedbackController& feedbackController() const noexcept;
    [[nodiscard]] WorkItemPlanner& workItemPlanner() noexcept;
    [[nodiscard]] const WorkItemPlanner& workItemPlanner() const noexcept;
    [[nodiscard]] CompressionAdvisor& compressionAdvisor() noexcept;
    [[nodiscard]] const CompressionAdvisor& compressionAdvisor() const noexcept;
    [[nodiscard]] MetricsWriter& metricsWriter() noexcept;
    [[nodiscard]] const MetricsWriter& metricsWriter() const noexcept;
    [[nodiscard]] const std::string& taskId() const noexcept;

    [[nodiscard]] common::Result<std::vector<FilePlan>> planManifest(
        const core::tree::TreeManifest& manifest, const std::filesystem::path& rootPath,
        SchedulerDirection direction, bool allowSampling);

    [[nodiscard]] common::Status recordDispatch(const SchedulerEventRecord& record);
    [[nodiscard]] common::Status recordRetry(const SchedulerEventRecord& record);
    [[nodiscard]] common::Status recordFailed(const SchedulerEventRecord& record);
    [[nodiscard]] common::Status recordExecutorComplete(const SchedulerEventRecord& record);
    [[nodiscard]] common::Status recordLinkEvent(const SchedulerEventRecord& record);
    [[nodiscard]] common::Status recordSample(const SchedulerSampleRecordOut& record);
    [[nodiscard]] common::Status writeSummary(const SchedulerSummaryRecord& record);
    [[nodiscard]] std::string dominantBottleneck(const SchedulerSummaryRecord& summary,
                                                 const LinkState& state) const;

   private:
    explicit GlobalScheduler(SchedulerConfig config, LinkProfile linkProfile,
                             MetricsWriter metricsWriter, WorkItemPlanner planner,
                             LinkManager linkManager, FeedbackController feedbackController,
                             CompressionAdvisor compressionAdvisor);

    SchedulerConfig config_;
    LinkProfile linkProfile_;
    MetricsWriter metricsWriter_;
    WorkItemPlanner planner_;
    LinkManager linkManager_;
    FeedbackController feedbackController_;
    CompressionAdvisor compressionAdvisor_;
};

}  // namespace cpnetflux::core::scheduler
