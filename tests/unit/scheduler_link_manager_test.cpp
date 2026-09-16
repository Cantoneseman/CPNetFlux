#include "cpnetflux/core/scheduler/link_manager.h"

#include <gtest/gtest.h>

#include <cstdint>

TEST(SchedulerLinkManagerTest, TracksQueueAndInflightState) {
    cpnetflux::core::scheduler::LinkManager manager;

    cpnetflux::core::scheduler::LinkState probe;
    probe.profile.capacityGbps = 1.0;
    EXPECT_GE(manager.targetQueueBytes(probe, 64ULL * 1024ULL * 1024ULL, 10),
              64ULL * 1024ULL * 1024ULL);

    cpnetflux::core::scheduler::LinkProfile profile;
    profile.linkId = "link0";
    profile.remoteHost = "127.0.0.1";
    profile.dataPortRange = "50000-50010";
    profile.capacityGbps = 1.5;
    profile.initialConnections = 1;
    profile.maxConnections = 4;
    profile.targetQueueBytes = 123456;

    ASSERT_TRUE(manager.addLink(profile).isOk());
    const cpnetflux::core::scheduler::LinkState* state = manager.link("link0");
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->profile.targetQueueBytes, 123456U);

    EXPECT_TRUE(manager.setReadyBytes("link0", 4096).isOk());
    EXPECT_TRUE(manager.addInflightBytes("link0", 1024).isOk());
    state = manager.link("link0");
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->readyBytes, 4096U);
    EXPECT_EQ(state->inflightBytes, 1024U);
    EXPECT_FALSE(manager.shouldPause(*state));

    EXPECT_TRUE(manager.consumeReadyBytes("link0", 2048).isOk());
    EXPECT_TRUE(manager.releaseInflightBytes("link0", 1024).isOk());
    state = manager.link("link0");
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->readyBytes, 2048U);
    EXPECT_EQ(state->inflightBytes, 0U);

    EXPECT_TRUE(manager.updatePressure("link0", 90.0, 0.8, 0.9).isOk());
    EXPECT_TRUE(manager.setPaused("link0", true).isOk());
    EXPECT_TRUE(manager.setTargetConnections("link0", 3).isOk());
    EXPECT_TRUE(manager.recordWatermarkHit("link0", true, true).isOk());
    EXPECT_TRUE(manager.recordRamp("link0", true, true).isOk());
    EXPECT_TRUE(manager.addRetryCount("link0", 2).isOk());
    state = manager.link("link0");
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->targetConnections, 3U);
    EXPECT_EQ(state->currentConnections, 3U);
    EXPECT_EQ(state->lowWatermarkHits, 1U);
    EXPECT_EQ(state->highWatermarkHits, 1U);
    EXPECT_EQ(state->rampUpCount, 1U);
    EXPECT_EQ(state->rampDownCount, 1U);
    EXPECT_EQ(state->sendPressureHighCount, 1U);
    EXPECT_EQ(state->writePressureHighCount, 1U);
    EXPECT_EQ(state->cpuPressureHighCount, 1U);
    EXPECT_DOUBLE_EQ(state->maxSendPressure, 0.9);
    EXPECT_DOUBLE_EQ(state->maxWritePressure, 0.8);
    EXPECT_DOUBLE_EQ(state->maxCpuPressure, 90.0);
    EXPECT_EQ(state->pausedCount, 1U);
    EXPECT_EQ(state->retryCount, 2U);
}

TEST(SchedulerLinkManagerTest, RejectsDuplicateLinkIds) {
    cpnetflux::core::scheduler::LinkManager manager;
    cpnetflux::core::scheduler::LinkProfile profile;
    profile.linkId = "link1";
    profile.capacityGbps = 1.0;
    profile.initialConnections = 1;
    profile.maxConnections = 1;

    EXPECT_TRUE(manager.addLink(profile).isOk());
    EXPECT_FALSE(manager.addLink(profile).isOk());
}
