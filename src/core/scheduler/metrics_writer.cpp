#include "cpnetflux/core/scheduler/metrics_writer.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace cpnetflux::core::scheduler {
namespace {

std::string csvEscapeValue(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped += "\"";
    return escaped;
}

std::string jsonEscapeValue(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (character < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(character);
                    output += hex.str();
                } else {
                    output.push_back(static_cast<char>(character));
                }
        }
    }
    return output;
}

std::string isoTimestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&nowTime, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

}  // namespace

MetricsWriter::MetricsWriter(SchedulerMetricsPaths paths) : paths_(std::move(paths)) {}

MetricsWriter::MetricsWriter(MetricsWriter&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mutex_);
    paths_ = std::move(other.paths_);
}

MetricsWriter& MetricsWriter::operator=(MetricsWriter&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    std::lock(mutex_, other.mutex_);
    std::lock_guard<std::mutex> left(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> right(other.mutex_, std::adopt_lock);
    paths_ = std::move(other.paths_);
    return *this;
}

common::Result<MetricsWriter> MetricsWriter::open(SchedulerMetricsPaths paths) {
    MetricsWriter writer(std::move(paths));
    const common::Status dirs = writer.createParentDirectories();
    if (!dirs.isOk()) {
        return dirs;
    }
    if (!writer.paths_.summaryCsv.empty()) {
        std::ofstream summary(writer.paths_.summaryCsv, std::ios::trunc);
        if (!summary) {
            return common::Status::runtimeError("open scheduler summary failed: " +
                                               writer.paths_.summaryCsv);
        }
        summary <<
            "task_id,total_bytes,logical_bytes,wire_bytes,elapsed_seconds,goodput_gbps,wire_gbps,"
            "compression_ratio_effective,compression_attempts,compressed_workitems,"
            "raw_fallback_workitems,compression_failures,decompression_failures,retried_workitems,"
            "result,dominant_bottleneck,policy,initial_connections,current_connections,"
            "target_connections,max_connections,ramp_up_count,ramp_down_count,queue_low_count,"
            "queue_high_count,send_pressure_count,write_pressure_count,cpu_pressure_count,"
            "retry_count,max_send_pressure,max_write_pressure,max_cpu_pressure\n";
    }
    if (!writer.paths_.samplesCsv.empty()) {
        std::ofstream samples(writer.paths_.samplesCsv, std::ios::trunc);
        if (!samples) {
            return common::Status::runtimeError("open scheduler samples failed: " +
                                               writer.paths_.samplesCsv);
        }
        samples <<
            "file_id,offset,length,sample_ratio,sample_comp_gbps,decision,reason,cpu_percent,"
            "link_ready_ratio\n";
    }
    if (!writer.paths_.eventsJsonl.empty()) {
        std::ofstream events(writer.paths_.eventsJsonl, std::ios::trunc);
        if (!events) {
            return common::Status::runtimeError("open scheduler events failed: " +
                                               writer.paths_.eventsJsonl);
        }
    }
    return writer;
}

bool MetricsWriter::enabled() const noexcept {
    return !paths_.summaryCsv.empty() || !paths_.eventsJsonl.empty() || !paths_.samplesCsv.empty();
}

const SchedulerMetricsPaths& MetricsWriter::paths() const noexcept { return paths_; }

common::Status MetricsWriter::writeSummary(const SchedulerSummaryRecord& record) {
    if (paths_.summaryCsv.empty()) {
        return common::Status::ok();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream output(paths_.summaryCsv, std::ios::app);
    if (!output) {
        return common::Status::runtimeError("scheduler summary cannot be opened: " +
                                           paths_.summaryCsv);
    }
    output << csvEscapeValue(record.taskId) << ',' << record.totalBytes << ','
           << record.logicalBytes << ',' << record.wireBytes << ',' << record.elapsedSeconds
           << ',' << record.goodputGbps << ',' << record.wireGbps << ','
           << record.compressionRatioEffective << ',' << record.compressionAttempts << ','
           << record.compressedWorkItems << ',' << record.rawFallbackWorkItems << ','
           << record.compressionFailures << ',' << record.decompressionFailures << ','
           << record.retriedWorkItems << ','
           << csvEscapeValue(record.result) << ',' << csvEscapeValue(record.dominantBottleneck)
           << ',' << csvEscapeValue(record.policy) << ',' << record.initialConnections << ','
           << record.currentConnections << ',' << record.targetConnections << ','
           << record.maxConnections << ',' << record.rampUpCount << ',' << record.rampDownCount
           << ',' << record.queueLowCount << ',' << record.queueHighCount << ','
           << record.sendPressureCount << ',' << record.writePressureCount << ','
           << record.cpuPressureCount << ',' << record.retryCount << ','
           << record.maxSendPressure << ',' << record.maxWritePressure << ','
           << record.maxCpuPressure
           << '\n';
    if (!output) {
        return common::Status::runtimeError("scheduler summary write failed: " + paths_.summaryCsv);
    }
    return common::Status::ok();
}

common::Status MetricsWriter::writeEvent(const SchedulerEventRecord& record) {
    if (paths_.eventsJsonl.empty()) {
        return common::Status::ok();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream output(paths_.eventsJsonl, std::ios::app);
    if (!output) {
        return common::Status::runtimeError("scheduler events cannot be opened: " +
                                           paths_.eventsJsonl);
    }
    output << "{";
    const std::string ts = record.ts.empty() ? isoTimestampUtc() : record.ts;
    output << "\"ts\":\"" << jsonEscapeValue(ts) << "\",";
    output << "\"event\":\"" << jsonEscapeValue(record.event) << "\",";
    output << "\"task_id\":\"" << jsonEscapeValue(record.taskId) << "\",";
    output << "\"file_id\":\"" << jsonEscapeValue(record.fileId) << "\",";
    output << "\"link_id\":\"" << jsonEscapeValue(record.linkId) << "\",";
    output << "\"offset\":" << record.offset << ",";
    output << "\"length\":" << record.length << ",";
    output << "\"reason\":\"" << jsonEscapeValue(record.reason) << "\",";
    output << "\"ready_bytes\":" << record.readyBytes << ",";
    output << "\"target_connections\":" << record.targetConnections << ",";
    output << "\"result\":\"" << jsonEscapeValue(record.result) << "\",";
    output << "\"message\":\"" << jsonEscapeValue(record.message) << "\"";
    output << "}\n";
    if (!output) {
        return common::Status::runtimeError("scheduler events write failed: " + paths_.eventsJsonl);
    }
    return common::Status::ok();
}

common::Status MetricsWriter::writeSample(const SchedulerSampleRecordOut& record) {
    if (paths_.samplesCsv.empty()) {
        return common::Status::ok();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream output(paths_.samplesCsv, std::ios::app);
    if (!output) {
        return common::Status::runtimeError("scheduler samples cannot be opened: " +
                                           paths_.samplesCsv);
    }
    output << csvEscapeValue(record.fileId) << ',' << record.offset << ',' << record.length << ','
           << record.sampleRatio << ',' << record.sampleCompGbps << ','
           << csvEscapeValue(record.decision) << ',' << csvEscapeValue(record.reason) << ','
           << record.cpuPercent << ',' << record.linkReadyRatio << '\n';
    if (!output) {
        return common::Status::runtimeError("scheduler samples write failed: " + paths_.samplesCsv);
    }
    return common::Status::ok();
}

common::Status MetricsWriter::createParentDirectories() const {
    for (const std::string* path : {&paths_.summaryCsv, &paths_.eventsJsonl, &paths_.samplesCsv}) {
        if (path->empty()) {
            continue;
        }
        std::filesystem::path outputPath(*path);
        if (!outputPath.parent_path().empty()) {
            std::error_code error;
            std::filesystem::create_directories(outputPath.parent_path(), error);
            if (error) {
                return common::Status::systemError("create scheduler metrics directory failed: " +
                                                       error.message(),
                                                   error.value());
            }
        }
    }
    return common::Status::ok();
}

std::string MetricsWriter::csvEscape(const std::string& value) { return csvEscapeValue(value); }

std::string MetricsWriter::jsonEscape(const std::string& value) { return jsonEscapeValue(value); }

std::string MetricsWriter::timestampUtc() { return isoTimestampUtc(); }

common::Status MetricsWriter::writeSummaryHeader() const {
    return common::Status::ok();
}

common::Status MetricsWriter::writeSampleHeader() const {
    return common::Status::ok();
}

}  // namespace cpnetflux::core::scheduler
