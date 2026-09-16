#include "cpnetflux/core/metrics/event_log.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <utility>

namespace cpnetflux::core::metrics {
namespace {

std::string timestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&nowTime, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string jsonEscape(const std::string& value) {
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

std::string sanitizeMessage(const std::string& message) {
    std::string output = message;
    constexpr const char* kPassword = "PASS ";
    std::size_t position = 0;
    while ((position = output.find(kPassword, position)) != std::string::npos) {
        const std::size_t begin = position + 5;
        std::size_t end = output.find_first_of("\r\n\t ", begin);
        if (end == std::string::npos) {
            end = output.size();
        }
        output.replace(begin, end - begin, "<redacted>");
        position = begin + 10;
    }
    return output;
}

}  // namespace

EventLogger::EventLogger(std::string path) : path_(std::move(path)) {}

EventLogger::EventLogger(EventLogger&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mutex_);
    path_ = std::move(other.path_);
}

EventLogger& EventLogger::operator=(EventLogger&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    std::lock(mutex_, other.mutex_);
    std::lock_guard<std::mutex> left(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> right(other.mutex_, std::adopt_lock);
    path_ = std::move(other.path_);
    return *this;
}

common::Result<EventLogger> EventLogger::open(std::string path) {
    const common::Status status = validateEventLogPath(path);
    if (!status.isOk()) {
        return status;
    }
    return EventLogger(std::move(path));
}

bool EventLogger::enabled() const noexcept { return !path_.empty(); }

const std::string& EventLogger::path() const noexcept { return path_; }

common::Status EventLogger::write(const EventRecord& record) {
    if (!enabled()) {
        return common::Status::ok();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return writeEventLog(path_, record);
}

common::Status validateEventLogPath(const std::string& path) {
    if (path.empty()) {
        return common::Status::ok();
    }
    std::filesystem::path outputPath(path);
    std::error_code error;
    if (!outputPath.parent_path().empty()) {
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            return common::Status::systemError("create event log directory failed: " +
                                                   error.message(),
                                               error.value());
        }
    }
    std::ofstream output(outputPath, std::ios::app);
    if (!output) {
        return common::Status::runtimeError("event log cannot be opened: " + path);
    }
    return common::Status::ok();
}

common::Status writeEventLog(const std::string& path, const EventRecord& record) {
    if (path.empty()) {
        return common::Status::ok();
    }
    std::ofstream output(path, std::ios::app);
    if (!output) {
        return common::Status::runtimeError("event log cannot be opened: " + path);
    }
    output << "{";
    output << "\"timestamp\":\"" << timestampUtc() << "\",";
    output << "\"component\":\"" << jsonEscape(record.component) << "\",";
    output << "\"event\":\"" << jsonEscape(record.event) << "\",";
    output << "\"transfer_id\":\"" << jsonEscape(record.transferId) << "\",";
    output << "\"direction\":\"" << jsonEscape(record.direction) << "\",";
    output << "\"path\":\"" << jsonEscape(record.path) << "\",";
    output << "\"result\":\"" << jsonEscape(record.result) << "\",";
    output << "\"error_code\":\"" << errorCodeName(record.errorCode) << "\",";
    output << "\"message\":\"" << jsonEscape(sanitizeMessage(record.message)) << "\",";
    output << "\"elapsed_seconds\":" << record.elapsedSeconds << ",";
    output << "\"bytes\":" << record.bytes;
    output << "}\n";
    if (!output) {
        return common::Status::runtimeError("event log write failed: " + path);
    }
    return common::Status::ok();
}

}  // namespace cpnetflux::core::metrics
