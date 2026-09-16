#include "cpnetflux/core/scheduler/feedback_controller.h"

#include <algorithm>
#include <string_view>

namespace cpnetflux::core::scheduler {

common::Result<SchedulerPolicy> parseSchedulerPolicy(std::string_view value) {
    if (value == "fixed") {
        return SchedulerPolicy::Fixed;
    }
    if (value == "adaptive") {
        return SchedulerPolicy::Adaptive;
    }
    return common::Status::invalidArgument("--scheduler-policy must be fixed or adaptive");
}

const char* schedulerPolicyName(SchedulerPolicy policy) noexcept {
    switch (policy) {
        case SchedulerPolicy::Fixed:
            return "fixed";
        case SchedulerPolicy::Adaptive:
            return "adaptive";
    }
    return "fixed";
}

FeedbackController::FeedbackController(std::uint32_t initialConnections,
                                       std::uint32_t maxConnections)
    : FeedbackController(FeedbackControllerConfig{SchedulerPolicy::Fixed,
                                                  initialConnections,
                                                  maxConnections}) {}

FeedbackDecision FeedbackController::evaluate(const LinkState& state, double cpuPressure,
                                              double writePressure, double sendPressure,
                                              std::uint64_t retryCount) {
    FeedbackDecision decision;
    decision.targetConnections = currentConnections_;

    const double effectiveCpuPressure = std::max(cpuPressure, state.cpuPressure);
    const double effectiveWritePressure = std::max(writePressure, state.writePressure);
    const double effectiveSendPressure = std::max(sendPressure, state.sendPressure);
    const bool pressureHigh = effectiveCpuPressure >= config_.cpuPressureHigh ||
                              effectiveWritePressure >= config_.writePressureHigh ||
                              effectiveSendPressure >= config_.sendPressureHigh;
    const bool queueHigh = state.queueFillRatio >= config_.highWatermarkRatio;
    const bool queueLow = state.queueFillRatio < config_.lowWatermarkRatio;
    const bool retryPressure = retryCount > lastRetryCount_;
    lastRetryCount_ = retryCount;

    decision.queueLow = queueLow;
    decision.queueHigh = queueHigh;
    decision.pressureHigh = pressureHigh;
    decision.retryPressure = retryPressure;

    if (config_.policy == SchedulerPolicy::Fixed) {
        decision.paused = state.paused;
        decision.reason = state.paused ? "paused" : "fixed";
        return decision;
    }

    if (queueHigh || pressureHigh || state.paused || retryPressure) {
        lowWatermarkStreak_ = 0;
        if (queueHigh || state.paused) {
            ++highWatermarkStreak_;
        }
        if (pressureHigh || retryPressure) {
            ++pressureStreak_;
        }
        const bool highReady =
            state.paused ||
            highWatermarkStreak_ >= config_.rampDownHighWatermarkHits ||
            pressureStreak_ >= config_.rampDownPressureHits ||
            retryPressure;
        if (highReady && currentConnections_ > 1) {
            --currentConnections_;
            decision.rampDown = true;
        }
        decision.targetConnections = currentConnections_;
        decision.paused = state.paused || queueHigh;
        if (state.paused) {
            decision.reason = "paused";
        } else if (queueHigh) {
            decision.reason = "high_watermark";
        } else if (effectiveSendPressure >= config_.sendPressureHigh) {
            decision.reason = "send_pressure";
        } else if (effectiveWritePressure >= config_.writePressureHigh) {
            decision.reason = "write_pressure";
        } else if (effectiveCpuPressure >= config_.cpuPressureHigh) {
            decision.reason = "cpu_pressure";
        } else {
            decision.reason = "retry_pressure";
        }
        return decision;
    }

    highWatermarkStreak_ = 0;
    pressureStreak_ = 0;
    if (queueLow) {
        ++lowWatermarkStreak_;
    } else {
        lowWatermarkStreak_ = 0;
    }

    if (queueLow && lowWatermarkStreak_ >= config_.rampUpLowWatermarkHits &&
        currentConnections_ < maxConnections_) {
        ++currentConnections_;
        decision.targetConnections = currentConnections_;
        decision.rampUp = true;
        decision.reason = "low_watermark";
        lowWatermarkStreak_ = 0;
        return decision;
    }

    decision.reason = "balanced";
    return decision;
}

FeedbackController::FeedbackController(FeedbackControllerConfig config)
    : config_(config),
      currentConnections_(std::max<std::uint32_t>(1, config.initialConnections)),
      maxConnections_(std::max<std::uint32_t>(1, config.maxConnections)) {
    if (currentConnections_ > maxConnections_) {
        currentConnections_ = maxConnections_;
    }
    config_.initialConnections = currentConnections_;
    config_.maxConnections = maxConnections_;
    config_.lowWatermarkRatio = std::max(0.0, config_.lowWatermarkRatio);
    config_.highWatermarkRatio =
        std::max(config_.lowWatermarkRatio, config_.highWatermarkRatio);
    config_.rampUpLowWatermarkHits = std::max<std::uint32_t>(1, config_.rampUpLowWatermarkHits);
    config_.rampDownPressureHits = std::max<std::uint32_t>(1, config_.rampDownPressureHits);
    config_.rampDownHighWatermarkHits =
        std::max<std::uint32_t>(1, config_.rampDownHighWatermarkHits);
}

std::uint32_t FeedbackController::currentConnections() const noexcept { return currentConnections_; }

const FeedbackControllerConfig& FeedbackController::config() const noexcept { return config_; }

void FeedbackController::reset(std::uint32_t connections) noexcept {
    currentConnections_ = std::max<std::uint32_t>(1, std::min(connections, maxConnections_));
    lastRetryCount_ = 0;
    lowWatermarkStreak_ = 0;
    highWatermarkStreak_ = 0;
    pressureStreak_ = 0;
}

}  // namespace cpnetflux::core::scheduler
