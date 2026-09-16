#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/metrics/error_code.h"

namespace cpnetflux::core::metrics {

struct EventRecord {
    std::string component;
    std::string event;
    std::string transferId;
    std::string direction;
    std::string path;
    std::string result;
    ErrorCode errorCode = ErrorCode::Ok;
    std::string message;
    double elapsedSeconds = 0.0;
    std::uint64_t bytes = 0;
};

class EventLogger {
   public:
    EventLogger() = default;
    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;
    EventLogger(EventLogger&& other) noexcept;
    EventLogger& operator=(EventLogger&& other) noexcept;

    [[nodiscard]] static common::Result<EventLogger> open(std::string path);
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] const std::string& path() const noexcept;
    [[nodiscard]] common::Status write(const EventRecord& record);

   private:
    explicit EventLogger(std::string path);

    std::string path_;
    mutable std::mutex mutex_;
};

[[nodiscard]] common::Status validateEventLogPath(const std::string& path);
[[nodiscard]] common::Status writeEventLog(const std::string& path, const EventRecord& record);

}  // namespace cpnetflux::core::metrics
