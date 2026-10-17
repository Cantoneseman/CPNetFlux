#include "cpnetflux/core/scheduler/link_set.h"
#include "cpnetflux/core/scheduler/scheduler.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace {

using cpnetflux::core::scheduler::LinkProfile;
using cpnetflux::core::scheduler::LinkSet;
using cpnetflux::core::scheduler::LinkSetCapability;
using cpnetflux::core::scheduler::decodeLinkSetCapability;
using cpnetflux::core::scheduler::encodeLinkSetCapability;
using cpnetflux::core::scheduler::summarizeLinkNegotiation;
using cpnetflux::core::scheduler::WorkItem;
using cpnetflux::core::scheduler::GlobalScheduler;
using cpnetflux::core::scheduler::SchedulerConfig;

LinkProfile profile(std::string id) {
    LinkProfile result;
    result.linkId = std::move(id);
    result.capacityGbps = 100.0;
    result.initialConnections = 1;
    result.maxConnections = 4;
    result.pendingWindow = 4;
    result.targetQueueBytes = 1024;
    result.remoteHost = result.linkId + ".example";
    result.dataPortRange = "24000-24015";
    return result;
}

TEST(SchedulerLinkSetTest, BuildsSupportedLinkCounts) {
    for (const auto count : std::array<std::size_t, 5>{1, 2, 4, 8, 10}) {
        LinkSet set(4);
        for (std::size_t i = 0; i < count; ++i) {
            ASSERT_TRUE(set.add(profile("link" + std::to_string(i))).isOk());
        }
        EXPECT_EQ(set.size(), count);
        EXPECT_EQ(set.linkIds().front(), "link0");
    }
}

TEST(SchedulerLinkSetTest, RejectsInvalidIdentityAndEndpoint) {
    LinkSet set;
    auto duplicate = profile("link0");
    ASSERT_TRUE(set.add(duplicate).isOk());
    EXPECT_FALSE(set.add(duplicate).isOk());
    auto endpointConflict = profile("link1");
    endpointConflict.remoteHost = "link0.example";
    EXPECT_FALSE(set.add(endpointConflict).isOk());

    auto zero = profile("link2");
    zero.capacityGbps = 0.0;
    EXPECT_FALSE(set.add(zero).isOk());
    auto zeroWindow = profile("link3");
    zeroWindow.pendingWindow = 0;
    EXPECT_FALSE(set.add(zeroWindow).isOk());
}

TEST(SchedulerLinkSetTest, AdvertisesAndDecodesStableCapabilitySummary) {
    LinkSet set(4);
    auto first = profile("link0");
    first.capabilityBits = 0x01;
    ASSERT_TRUE(set.add(first).isOk());
    auto second = profile("link1");
    second.capabilityBits = 0x08;
    ASSERT_TRUE(set.add(second).isOk());

    const auto advertised = set.advertisedCapability();
    EXPECT_EQ(advertised.maxLinks, 2U);
    EXPECT_EQ(advertised.maxPendingWindow, 4U);
    EXPECT_EQ(advertised.capabilityBits, 0x09U);
    const auto encoded = encodeLinkSetCapability(advertised);
    EXPECT_EQ(encoded, "links=2 pending_window=4 capabilities=9");

    const auto decoded = decodeLinkSetCapability(encoded);
    ASSERT_TRUE(decoded.isOk());
    EXPECT_EQ(decoded.value().maxLinks, advertised.maxLinks);
    EXPECT_EQ(decoded.value().maxPendingWindow, advertised.maxPendingWindow);
    EXPECT_EQ(decoded.value().capabilityBits, advertised.capabilityBits);

    const auto negotiation = set.negotiate(LinkSetCapability{2, 4, 0});
    ASSERT_TRUE(negotiation.isOk());
    EXPECT_EQ(summarizeLinkNegotiation(negotiation.value()),
              "requested_links=2 accepted_links=2 requested_pending_window=4 "
              "accepted_pending_window=4 accepted_ids=link0,link1 rejected_ids=- "
              "reason=fully_supported");
}

TEST(SchedulerLinkSetTest, RejectsMalformedCapabilityAdvertisement) {
    for (const auto& encoded : {"links=0 pending_window=4 capabilities=0",
                                "links=11 pending_window=4 capabilities=0",
                                "links=2 pending_window=0 capabilities=0",
                                "links=2 pending_window=4",
                                "links=2 pending_window=4 capabilities=0 extra=1",
                                "links=2 links=3 pending_window=4 capabilities=0"}) {
        EXPECT_FALSE(decodeLinkSetCapability(encoded).isOk()) << encoded;
    }
}

TEST(SchedulerLinkSetTest, NegotiatesPeerBoundsExplicitly) {
    LinkSet set(4);
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(set.add(profile("link" + std::to_string(i))).isOk());
    }
    auto peer = set.negotiate(LinkSetCapability{4, 4, 0});
    ASSERT_TRUE(peer.isOk());
    EXPECT_EQ(peer.value().acceptedLinks, 4U);
    EXPECT_EQ(peer.value().acceptedPendingWindow, 4U);
    EXPECT_EQ(peer.value().reason, "fully_supported");

    auto tooSmall = set.negotiate(LinkSetCapability{2, 4, 0});
    EXPECT_FALSE(tooSmall.isOk());
    EXPECT_NE(tooSmall.status().message().find("exceed"), std::string::npos);
}

TEST(SchedulerLinkSetTest, ChoosesLeastLoadedHealthyLinkDeterministically) {
    LinkSet set;
    ASSERT_TRUE(set.add(profile("link0")).isOk());
    ASSERT_TRUE(set.add(profile("link1")).isOk());
    ASSERT_TRUE(set.setRuntime("link0", 200, 2, 10).isOk());
    ASSERT_TRUE(set.setRuntime("link1", 100, 4, 10).isOk());

    WorkItem item;
    auto choice = set.choose(item);
    ASSERT_TRUE(choice.isOk());
    EXPECT_EQ(choice.value(), "link1");
    ASSERT_TRUE(set.setDraining("link1", true).isOk());
    EXPECT_EQ(set.choose(item).value(), "link0");
    ASSERT_TRUE(set.setHealthy("link0", false).isOk());
    EXPECT_FALSE(set.choose(item).isOk());
}

TEST(SchedulerLinkSetTest, ReservesAndReleasesBytesExactlyOnce) {
    LinkSet set;
    auto p = profile("link0");
    p.targetQueueBytes = 100;
    ASSERT_TRUE(set.add(p).isOk());
    ASSERT_TRUE(set.reserve("link0", "lease-a", 60).isOk());
    ASSERT_FALSE(set.reserve("link0", "lease-b", 41).isOk());
    ASSERT_TRUE(set.release("link0", "lease-a", 20).isOk());
    EXPECT_EQ(set.runtime("link0")->inflightBytes, 40U);
    ASSERT_TRUE(set.release("link0", "lease-a", 40).isOk());
    EXPECT_EQ(set.runtime("link0")->inflightBytes, 0U);
    EXPECT_FALSE(set.release("link0", "lease-a", 1).isOk());
    EXPECT_FALSE(set.release("link0", "missing", 1).isOk());
}

}  // namespace


TEST(SchedulerLinkSetTest, GlobalSchedulerExposesSingleLinkCompatibilityView) {
    SchedulerConfig config;
    config.taskId = "linkset-compat";
    config.linkId = "link0";
    config.capacityGbps = 100.0;
    config.initialConnections = 1;
    config.maxConnections = 4;
    auto scheduler = GlobalScheduler::create(config);
    ASSERT_TRUE(scheduler.isOk());
    EXPECT_EQ(scheduler.value().linkSet().size(), 1U);
    ASSERT_NE(scheduler.value().linkSet().profile("link0"), nullptr);
    EXPECT_DOUBLE_EQ(scheduler.value().linkSet().profile("link0")->capacityGbps, 100.0);
}
