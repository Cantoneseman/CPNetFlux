#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "cpnetflux/core/io/tree_pipeline_identity.h"

namespace {
using cpnetflux::core::io::detail::TreePipelineCandidateIdentity;
using cpnetflux::core::io::detail::treePipelineCandidateIdentityMatches;

TEST(TreePipelineCandidateIdentityTest, RejectsChangedIndexPathDirectionAndGeneration) {
    const TreePipelineCandidateIdentity identity{3, "nested/file.bin", true, 7};
    EXPECT_TRUE(treePipelineCandidateIdentityMatches(identity, 3, "nested/file.bin", true, 7));
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(identity, 4, "nested/file.bin", true, 7));
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(identity, 3, "nested/other.bin", true, 7));
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(identity, 3, "nested/file.bin", false, 7));
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(identity, 3, "nested/file.bin", true, 8));
}

TEST(TreePipelineCandidateIdentityTest, AcceptsDownloadIdentityAtMaximumGeneration) {
    const TreePipelineCandidateIdentity identity{0, "empty.bin", false, UINT64_MAX};
    EXPECT_TRUE(treePipelineCandidateIdentityMatches(identity, 0, "empty.bin", false, UINT64_MAX));
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(identity, 0, "empty.bin", true, UINT64_MAX));
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(identity, 0, "empty.bin", false, UINT64_MAX - 1));
}
TEST(TreePipelineCandidateIdentityTest, RejectsOtherSlotEvenWhenGenerationAndFileMatch) {
    const TreePipelineCandidateIdentity identity{8, "nested/file.bin", true, 3, 2};
    EXPECT_TRUE(treePipelineCandidateIdentityMatches(identity, 8, "nested/file.bin", true, 3, 2));
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(identity, 8, "nested/file.bin", true, 3, 1));
}

TEST(TreePipelineCandidateIdentityTest, RejectsPreviousLeaseAfterSameSlotIsReused) {
    const TreePipelineCandidateIdentity first{8, "nested/file.bin", false, 9, 1};
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(first, 8, "nested/file.bin", false, 10, 1));
    const TreePipelineCandidateIdentity next{9, "next.bin", false, 10, 1};
    EXPECT_TRUE(treePipelineCandidateIdentityMatches(next, 9, "next.bin", false, 10, 1));
    EXPECT_FALSE(treePipelineCandidateIdentityMatches(first, 9, "next.bin", false, 10, 1));
}

}  // namespace
