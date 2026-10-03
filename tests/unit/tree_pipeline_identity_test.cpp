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
}  // namespace
