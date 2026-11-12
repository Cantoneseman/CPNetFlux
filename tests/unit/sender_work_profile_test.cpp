#include "cpnetflux/core/io/sender_work_profile.h"
#include <gtest/gtest.h>
#include <sstream>
using namespace cpnetflux::core::io;

TEST(SenderWorkProfile, DisabledDoesNotRecord) {
    SenderWorkProfile p; p.record(SenderWorkStage::ProducerCreate, 1, 2);
    EXPECT_TRUE(p.intervals.empty()); EXPECT_EQ(p.calls[0], 0U);
}
TEST(SenderWorkProfile, SplitJoinDoesNotMakePayloadAThreadSaving) {
    SenderWorkProfile p; p.enabled = true; p.join(1, 3, 4);
    EXPECT_DOUBLE_EQ(p.sums[2], 2); EXPECT_DOUBLE_EQ(p.sums[3], 1);
    p.join(5, 2, 6); EXPECT_DOUBLE_EQ(p.sums[2], 2); EXPECT_DOUBLE_EQ(p.sums[3], 2);
}
TEST(SenderWorkProfile, UnionIsAcrossLanesButCreditAndPayloadRemainSeparate) {
    SenderWorkProfile a,b; a.enabled=b.enabled=true;
    a.record(SenderWorkStage::ProducerCreate, 1, 3); b.record(SenderWorkStage::ProducerCreate, 2, 4);
    a.record(SenderWorkStage::CreditReady, 4, 5); a.record(SenderWorkStage::ProducerLoop, 1, 8);
    std::ostringstream out; appendSenderWorkProfile(out,true,true,{a,b});
    EXPECT_NE(out.str().find("\"thread_envelope_union_seconds\":3"),std::string::npos);
    EXPECT_NE(out.str().find("\"credit_ready\":{\"calls\":1,\"sum_seconds\":1,\"union_seconds\":1}"),std::string::npos);
}
TEST(SenderWorkProfile, ResultReaderMergesAfterJoinAndRetainsFailureCoverage) {
    SenderWorkProfile writer,reader; writer.enabled=reader.enabled=true;
    writer.startedMembers=2; writer.sentMembers=2; reader.completedMembers=1; reader.resultFailures=1;
    reader.record(SenderWorkStage::ResultRead,2,3); writer.merge(std::move(reader));
    EXPECT_EQ(writer.completedMembers,1U); EXPECT_EQ(writer.resultFailures,1U);
    std::ostringstream out; appendSenderWorkProfile(out,true,true,{writer});
    EXPECT_NE(out.str().find("\"started_members\":2,\"sent_members\":2,\"completed_members\":1"),std::string::npos);
    std::ostringstream receiver; appendSenderWorkProfile(receiver,true,false,{});
    EXPECT_NE(receiver.str().find("not_used_receiver"),std::string::npos);
}
TEST(SenderWorkProfile, InvalidAndOverflowCannotPassCompleteAccounting) {
    SenderWorkProfile p; p.enabled=true; p.record(SenderWorkStage::CreditReady,2,1);
    EXPECT_TRUE(p.truncated); EXPECT_TRUE(p.intervals.empty()); p.truncated=false;
    for(std::size_t i=0;i<=SenderWorkProfile::kMaxIntervals;++i) p.record(SenderWorkStage::ResultRead,1,2);
    EXPECT_TRUE(p.truncated); EXPECT_EQ(p.intervals.size(),SenderWorkProfile::kMaxIntervals);
    SenderWorkProfile reader; reader.enabled=true; reader.record(SenderWorkStage::ResultRead,2,3);
    p.merge(std::move(reader)); EXPECT_EQ(p.intervals.size(),SenderWorkProfile::kMaxIntervals);
}
