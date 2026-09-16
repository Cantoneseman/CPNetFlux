#include "cpnetflux/core/scheduler/metrics_writer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST(SchedulerMetricsWriterTest, WritesHeadersAndRecords) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-scheduler-metrics-writer";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    cpnetflux::core::scheduler::SchedulerMetricsPaths paths;
    paths.summaryCsv = (root / "scheduler_summary.csv").string();
    paths.eventsJsonl = (root / "scheduler_events.jsonl").string();
    paths.samplesCsv = (root / "scheduler_samples.csv").string();

    auto writerResult = cpnetflux::core::scheduler::MetricsWriter::open(paths);
    ASSERT_TRUE(writerResult.isOk()) << writerResult.status().message();
    cpnetflux::core::scheduler::MetricsWriter writer = std::move(writerResult.value());

    cpnetflux::core::scheduler::SchedulerSummaryRecord summary;
    summary.taskId = "task-1";
    summary.totalBytes = 1024;
    summary.logicalBytes = 1024;
    summary.wireBytes = 800;
    summary.elapsedSeconds = 2.5;
    summary.goodputGbps = 0.003;
    summary.wireGbps = 0.0025;
    summary.compressionRatioEffective = 0.78125;
    summary.rawWorkItems = 5;
    summary.compressedWorkItems = 2;
    summary.retriedWorkItems = 1;
    summary.result = "pass";
    summary.dominantBottleneck = "scheduler_supply";
    summary.policy = "adaptive";
    summary.initialConnections = 1;
    summary.currentConnections = 2;
    summary.targetConnections = 2;
    summary.maxConnections = 4;
    summary.rampUpCount = 1;
    summary.rampDownCount = 0;
    summary.queueLowCount = 2;
    summary.queueHighCount = 0;
    summary.sendPressureCount = 0;
    summary.writePressureCount = 1;
    summary.cpuPressureCount = 0;
    summary.retryCount = 1;
    summary.maxSendPressure = 0.9;
    summary.maxWritePressure = 0.8;
    summary.maxCpuPressure = 5.0;
    ASSERT_TRUE(writer.writeSummary(summary).isOk());

    cpnetflux::core::scheduler::SchedulerEventRecord event;
    event.event = "ramp_up";
    event.taskId = "task-1";
    event.fileId = "file-1";
    event.linkId = "link0";
    event.offset = 128;
    event.length = 4096;
    event.reason = "low_watermark";
    event.readyBytes = 8192;
    event.targetConnections = 2;
    event.result = "pass";
    event.message = "ok";
    ASSERT_TRUE(writer.writeEvent(event).isOk());

    cpnetflux::core::scheduler::SchedulerSampleRecordOut sample;
    sample.fileId = "file-1";
    sample.offset = 128;
    sample.length = 4096;
    sample.sampleRatio = 0.25;
    sample.sampleCompGbps = 1.5;
    sample.decision = "compression_candidate";
    sample.reason = "worthwhile";
    sample.cpuPercent = 5.0;
    sample.linkReadyRatio = 0.75;
    ASSERT_TRUE(writer.writeSample(sample).isOk());

    const auto summaryLines = readLines(paths.summaryCsv);
    const auto eventLines = readLines(paths.eventsJsonl);
    const auto sampleLines = readLines(paths.samplesCsv);

    ASSERT_EQ(summaryLines.size(), 2U);
    EXPECT_EQ(summaryLines[0],
              "task_id,total_bytes,logical_bytes,wire_bytes,elapsed_seconds,goodput_gbps,wire_gbps,"
              "compression_ratio_effective,compression_attempts,compressed_workitems,"
              "raw_fallback_workitems,compression_failures,decompression_failures,retried_workitems,"
              "result,dominant_bottleneck,policy,initial_connections,current_connections,"
              "target_connections,max_connections,ramp_up_count,ramp_down_count,queue_low_count,"
              "queue_high_count,send_pressure_count,write_pressure_count,cpu_pressure_count,"
              "retry_count,max_send_pressure,max_write_pressure,max_cpu_pressure");
    EXPECT_NE(summaryLines[1].find("task-1"), std::string::npos);
    EXPECT_NE(summaryLines[1].find("scheduler_supply"), std::string::npos);
    EXPECT_NE(summaryLines[1].find("adaptive"), std::string::npos);

    ASSERT_EQ(eventLines.size(), 1U);
    const std::string& eventLine = eventLines[0];
    const std::vector<std::string> orderedFields = {
        "\"ts\":\"", "\"event\":\"", "\"task_id\":\"", "\"file_id\":\"",
        "\"link_id\":\"", "\"offset\":", "\"length\":", "\"reason\":\"",
        "\"ready_bytes\":", "\"target_connections\":", "\"result\":\"",
        "\"message\":\""};
    std::size_t previous = 0;
    for (const std::string& field : orderedFields) {
        const std::size_t position = eventLine.find(field);
        ASSERT_NE(position, std::string::npos);
        EXPECT_GE(position, previous);
        previous = position;
    }
    EXPECT_NE(eventLine.find("\"event\":\"ramp_up\""), std::string::npos);
    EXPECT_NE(eventLine.find("\"file_id\":\"file-1\""), std::string::npos);

    ASSERT_EQ(sampleLines.size(), 2U);
    EXPECT_EQ(sampleLines[0],
              "file_id,offset,length,sample_ratio,sample_comp_gbps,decision,reason,cpu_percent,"
              "link_ready_ratio");
    EXPECT_NE(sampleLines[1].find("compression_candidate"), std::string::npos);
    EXPECT_NE(sampleLines[1].find("worthwhile"), std::string::npos);

    std::filesystem::remove_all(root);
}
