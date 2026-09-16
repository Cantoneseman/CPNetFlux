#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/scheduler/link_profile.h"

namespace cpnetflux::core::scheduler {

enum class SchedulerPolicy {
    Fixed,
    Adaptive,
};

[[nodiscard]] common::Result<SchedulerPolicy> parseSchedulerPolicy(std::string_view value);
[[nodiscard]] const char* schedulerPolicyName(SchedulerPolicy policy) noexcept;

struct FeedbackControllerConfig {
    SchedulerPolicy policy = SchedulerPolicy::Fixed;
    std::uint32_t initialConnections = 1;
    std::uint32_t maxConnections = 1;
    double lowWatermarkRatio = 0.5;
    double highWatermarkRatio = 2.0;
    double sendPressureHigh = 0.75;
    double writePressureHigh = 0.75;
    double cpuPressureHigh = 85.0;
    std::uint32_t rampUpLowWatermarkHits = 2;
    std::uint32_t rampDownPressureHits = 1;
    std::uint32_t rampDownHighWatermarkHits = 1;
};

struct FeedbackDecision {
    std::uint32_t targetConnections = 1;
    bool rampUp = false;
    bool rampDown = false;
    bool paused = false;
    bool queueLow = false;
    bool queueHigh = false;
    bool pressureHigh = false;
    bool retryPressure = false;
    std::string reason = "balanced";
};

class FeedbackController {
   public:
    FeedbackController(std::uint32_t initialConnections = 1, std::uint32_t maxConnections = 1);
    explicit FeedbackController(FeedbackControllerConfig config);

    [[nodiscard]] FeedbackDecision evaluate(const LinkState& state, double cpuPressure,
                                           double writePressure, double sendPressure,
                                           std::uint64_t retryCount = 0);

    [[nodiscard]] std::uint32_t currentConnections() const noexcept;
    [[nodiscard]] const FeedbackControllerConfig& config() const noexcept;
    void reset(std::uint32_t connections) noexcept;

   private:
    FeedbackControllerConfig config_;
    std::uint32_t currentConnections_ = 1;
    std::uint32_t maxConnections_ = 1;
    std::uint64_t lastRetryCount_ = 0;
    std::uint32_t lowWatermarkStreak_ = 0;
    std::uint32_t highWatermarkStreak_ = 0;
    std::uint32_t pressureStreak_ = 0;
};

}  // namespace cpnetflux::core::scheduler
