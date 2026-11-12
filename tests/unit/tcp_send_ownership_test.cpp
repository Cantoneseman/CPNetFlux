#include <gtest/gtest.h>
#include "../../tools/perf/tcp_send_ownership.h"

using cpnetflux::diagnostic::SendOwnership;

TEST(TcpSendOwnership, AcceptedBytesStayOwnedUntilInclusiveCompletion) {
    SendOwnership owner;
    ASSERT_TRUE(owner.begin(0, 65536));
    ASSERT_TRUE(owner.accepted(0, 65536));
    EXPECT_FALSE(owner.available(0));
    EXPECT_EQ(owner.heldBytes, 65536);
    ASSERT_TRUE(owner.complete(0, 0, false));
    EXPECT_TRUE(owner.available(0));
    EXPECT_EQ(owner.heldBytes, 0);
    EXPECT_EQ(owner.unmarkedCalls, 1);
}

TEST(TcpSendOwnership, PartialFrameCannotBeRecycledBetweenSendCalls) {
    SendOwnership owner;
    ASSERT_TRUE(owner.begin(2, 100));
    ASSERT_TRUE(owner.accepted(2, 40));
    ASSERT_TRUE(owner.complete(0, 0, true));
    EXPECT_FALSE(owner.available(2));
    ASSERT_TRUE(owner.accepted(2, 60));
    ASSERT_TRUE(owner.complete(1, 1, false));
    EXPECT_TRUE(owner.available(2));
    EXPECT_EQ(owner.copiedBytes, 40);
    EXPECT_EQ(owner.unmarkedBytes, 60);
}

TEST(TcpSendOwnership, MergedAndOutOfOrderCompletionRetainsOtherSlots) {
    SendOwnership owner;
    for (unsigned i=0;i<4;++i) { ASSERT_TRUE(owner.begin(i, 10)); ASSERT_TRUE(owner.accepted(i, 10)); }
    ASSERT_TRUE(owner.complete(2, 3, false));
    EXPECT_FALSE(owner.available(0));
    EXPECT_TRUE(owner.available(3));
    ASSERT_TRUE(owner.complete(0, 1, true));
    EXPECT_EQ(owner.pending(), 0);
    EXPECT_EQ(owner.copiedCalls, 2);
    EXPECT_EQ(owner.unmarkedCalls, 2);
}

TEST(TcpSendOwnership, InclusiveCompletionWrapsAtUint32Max) {
    SendOwnership owner(UINT32_MAX-1);
    for (unsigned i=0;i<3;++i) { ASSERT_TRUE(owner.begin(i, 1)); ASSERT_TRUE(owner.accepted(i, 1)); }
    ASSERT_EQ(owner.next(), 1);
    ASSERT_TRUE(owner.complete(UINT32_MAX-1, 0, false));
    EXPECT_EQ(owner.pending(), 0);
    EXPECT_EQ(owner.unmarkedCalls, 3);
}

TEST(TcpSendOwnership, InvalidRangeCannotPartiallyReleaseAnySlot) {
    SendOwnership owner;
    ASSERT_TRUE(owner.begin(0, 10)); ASSERT_TRUE(owner.accepted(0, 10));
    EXPECT_FALSE(owner.complete(0, 1, false));
    EXPECT_EQ(owner.pending(), 1);
    EXPECT_EQ(owner.heldBytes, 10);
    EXPECT_FALSE(owner.available(0));
    EXPECT_FALSE(owner.complete(1, 0, false));
    ASSERT_TRUE(owner.complete(0, 0, false));
    EXPECT_FALSE(owner.complete(0, 0, false));
}

TEST(TcpSendOwnership, FailedOrZeroSendDoesNotAllocateKernelId) {
    SendOwnership owner;
    ASSERT_TRUE(owner.begin(0, 4));
    EXPECT_FALSE(owner.accepted(0, 0));
    EXPECT_FALSE(owner.accepted(0, 5));
    EXPECT_EQ(owner.next(), 0);
    EXPECT_EQ(owner.pending(), 0);
    ASSERT_TRUE(owner.accepted(0, 4));
    EXPECT_EQ(owner.next(), 1);
}

TEST(TcpSendOwnership, FullLedgerBackpressuresWithoutOverwritingOldReference) {
    SendOwnership owner;
    ASSERT_TRUE(owner.begin(0, SendOwnership::kCalls+1));
    for (std::size_t i=0;i<SendOwnership::kCalls;++i) ASSERT_TRUE(owner.accepted(0, 1));
    EXPECT_FALSE(owner.canSend());
    EXPECT_FALSE(owner.accepted(0, 1));
    EXPECT_EQ(owner.pending(), SendOwnership::kCalls);
    ASSERT_TRUE(owner.complete(0, SendOwnership::kCalls-1, false));
    EXPECT_TRUE(owner.canSend());
    EXPECT_FALSE(owner.available(0));
    ASSERT_TRUE(owner.accepted(0, 1));
    ASSERT_TRUE(owner.complete(SendOwnership::kCalls, SendOwnership::kCalls, false));
    EXPECT_TRUE(owner.available(0));
}

TEST(TcpSendOwnership, BusyAndInvalidSlotsCannotBeReopened) {
    SendOwnership owner;
    EXPECT_FALSE(owner.begin(32, 1));
    EXPECT_FALSE(owner.begin(0, 0));
    ASSERT_TRUE(owner.begin(0, 10));
    EXPECT_FALSE(owner.begin(0, 10));
    EXPECT_FALSE(owner.available(32));
    EXPECT_FALSE(owner.accepted(32, 1));
    ASSERT_TRUE(owner.accepted(0, 10));
    EXPECT_FALSE(owner.accepted(0, 1));
    ASSERT_TRUE(owner.complete(0, 0, false));
    EXPECT_TRUE(owner.begin(0, 10));
}
