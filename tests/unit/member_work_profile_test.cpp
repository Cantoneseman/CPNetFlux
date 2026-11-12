#include "cpnetflux/core/io/member_work_profile.h"
#include <gtest/gtest.h>
#include <sstream>
using namespace cpnetflux::core::io;

TEST(MemberWorkProfile, UnionCountsOverlappingLanesOnce) {
    EXPECT_DOUBLE_EQ(memberWorkUnionSeconds({{1, 4}, {2, 3}, {3, 5}, {8, 9}, {9, 9}}), 5);
    EXPECT_DOUBLE_EQ(memberWorkUnionSeconds({}), 0);
}
TEST(MemberWorkProfile, JoinSplitKeepsPayloadOutOfThreadTail) {
    MemberWorkSample sample;
    sample.splitJoin(1, 3, 5);
    EXPECT_DOUBLE_EQ(sample.intervals[1].end - sample.intervals[1].begin, 2);
    EXPECT_DOUBLE_EQ(sample.intervals[2].end - sample.intervals[2].begin, 2);
    sample.splitJoin(3, 1, 5);
    EXPECT_DOUBLE_EQ(sample.intervals[1].end - sample.intervals[1].begin, 0);
    EXPECT_DOUBLE_EQ(sample.intervals[2].end - sample.intervals[2].begin, 2);
}
TEST(MemberWorkProfile, DisabledDoesNotCollectAndIncompleteIsExplicit) {
    MemberWorkProfile profile; MemberWorkSample sample;
    sample.set(MemberWorkStage::Commit, 1, 2);
    profile.record(sample, {});
    EXPECT_TRUE(profile.intervals.empty()); EXPECT_EQ(profile.completedMembers, 0U);
    profile.enabled = true; profile.startedMembers = 2;
    profile.record(sample, {});
    std::ostringstream out; appendMemberWorkProfile(out, true, true, {profile});
    EXPECT_NE(out.str().find("\"started_members\":2,\"completed_members\":1"), std::string::npos);
}
TEST(MemberWorkProfile, BoundedAndInvalidAccountingCannotLookComplete) {
    MemberWorkProfile profile; profile.enabled = true;
    MemberWorkSample sample; sample.set(MemberWorkStage::Commit, 2, 1);
    profile.record(sample, {}); EXPECT_TRUE(profile.truncated); EXPECT_TRUE(profile.intervals.empty());
    profile.truncated = false; sample.set(MemberWorkStage::Commit, 1, 2);
    for (std::size_t i = 0; i <= MemberWorkProfile::kMaxIntervals; ++i) profile.record(sample, {});
    EXPECT_TRUE(profile.truncated); EXPECT_EQ(profile.intervals.size(), MemberWorkProfile::kMaxIntervals);
}
TEST(MemberWorkProfile, AggregateRetainsNestedSaveScopeAndSenderRole) {
    MemberWorkProfile a, b; a.enabled = b.enabled = true;
    MemberWorkSample sample; sample.set(MemberWorkStage::ThreadCreate, 1, 3);
    cpnetflux::checkpoint::ManifestSaveStats saves; saves.attempts = 3; saves.saveSeconds = .1;
    a.record(sample, saves); b.record(sample, saves);
    std::ostringstream out; appendMemberWorkProfile(out, true, true, {a, b});
    EXPECT_NE(out.str().find("\"thread_envelope_union_seconds\":2"), std::string::npos);
    EXPECT_NE(out.str().find("\"attempts\":6"), std::string::npos);
    std::ostringstream sender; appendMemberWorkProfile(sender, true, false, {});
    EXPECT_NE(sender.str().find("not_used_sender"), std::string::npos);
}
