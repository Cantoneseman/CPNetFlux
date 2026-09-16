#include "cpnetflux/core/scheduler/feedback_controller.h"

#include <gtest/gtest.h>

namespace {

cpnetflux::core::scheduler::LinkState makeState(double queueFillRatio,
                                               std::uint32_t targetConnections = 1) {
    cpnetflux::core::scheduler::LinkState state;
    state.profile.linkId = "link0";
    state.profile.capacityGbps = 0.01;
    state.profile.initialConnections = 1;
    state.profile.maxConnections = 4;
    state.queueFillRatio = queueFillRatio;
    state.targetConnections = targetConnections;
    return state;
}

}  // namespace

TEST(SchedulerFeedbackControllerTest, FixedPolicyDoesNotChangeTarget) {
    cpnetflux::core::scheduler::FeedbackControllerConfig config;
    config.policy = cpnetflux::core::scheduler::SchedulerPolicy::Fixed;
    config.initialConnections = 3;
    config.maxConnections = 4;
    cpnetflux::core::scheduler::FeedbackController controller(config);

    auto decision = controller.evaluate(makeState(0.1, 3), 95.0, 1.0, 1.0);

    EXPECT_EQ(decision.targetConnections, 3U);
    EXPECT_FALSE(decision.rampUp);
    EXPECT_FALSE(decision.rampDown);
    EXPECT_EQ(decision.reason, "fixed");
}

TEST(SchedulerFeedbackControllerTest, QueueLowRampsUpAdaptivePolicy) {
    cpnetflux::core::scheduler::FeedbackControllerConfig config;
    config.policy = cpnetflux::core::scheduler::SchedulerPolicy::Adaptive;
    config.initialConnections = 1;
    config.maxConnections = 4;
    config.rampUpLowWatermarkHits = 1;
    cpnetflux::core::scheduler::FeedbackController controller(config);

    auto decision = controller.evaluate(makeState(0.1), 0.0, 0.0, 0.0);

    EXPECT_TRUE(decision.queueLow);
    EXPECT_TRUE(decision.rampUp);
    EXPECT_EQ(decision.targetConnections, 2U);
    EXPECT_EQ(decision.reason, "low_watermark");
}

TEST(SchedulerFeedbackControllerTest, QueueHighRampsDownAdaptivePolicy) {
    cpnetflux::core::scheduler::FeedbackControllerConfig config;
    config.policy = cpnetflux::core::scheduler::SchedulerPolicy::Adaptive;
    config.initialConnections = 3;
    config.maxConnections = 4;
    cpnetflux::core::scheduler::FeedbackController controller(config);

    auto decision = controller.evaluate(makeState(2.5, 3), 0.0, 0.0, 0.0);

    EXPECT_TRUE(decision.queueHigh);
    EXPECT_TRUE(decision.rampDown);
    EXPECT_TRUE(decision.paused);
    EXPECT_EQ(decision.targetConnections, 2U);
    EXPECT_EQ(decision.reason, "high_watermark");
}

TEST(SchedulerFeedbackControllerTest, PressureRampsDownAdaptivePolicy) {
    cpnetflux::core::scheduler::FeedbackControllerConfig config;
    config.policy = cpnetflux::core::scheduler::SchedulerPolicy::Adaptive;
    config.initialConnections = 3;
    config.maxConnections = 4;
    cpnetflux::core::scheduler::FeedbackController controller(config);

    auto decision = controller.evaluate(makeState(1.0, 3), 0.0, 0.9, 0.0);

    EXPECT_TRUE(decision.pressureHigh);
    EXPECT_TRUE(decision.rampDown);
    EXPECT_EQ(decision.targetConnections, 2U);
    EXPECT_EQ(decision.reason, "write_pressure");
}

TEST(SchedulerFeedbackControllerTest, AdaptivePolicyUsesLowWatermarkHysteresis) {
    cpnetflux::core::scheduler::FeedbackControllerConfig config;
    config.policy = cpnetflux::core::scheduler::SchedulerPolicy::Adaptive;
    config.initialConnections = 1;
    config.maxConnections = 4;
    config.rampUpLowWatermarkHits = 2;
    cpnetflux::core::scheduler::FeedbackController controller(config);

    auto first = controller.evaluate(makeState(0.1), 0.0, 0.0, 0.0);
    EXPECT_TRUE(first.queueLow);
    EXPECT_FALSE(first.rampUp);
    EXPECT_EQ(first.targetConnections, 1U);

    auto balanced = controller.evaluate(makeState(1.0), 0.0, 0.0, 0.0);
    EXPECT_FALSE(balanced.rampUp);
    EXPECT_EQ(balanced.targetConnections, 1U);

    (void)controller.evaluate(makeState(0.1), 0.0, 0.0, 0.0);
    auto secondLow = controller.evaluate(makeState(0.1), 0.0, 0.0, 0.0);
    EXPECT_TRUE(secondLow.rampUp);
    EXPECT_EQ(secondLow.targetConnections, 2U);
}
