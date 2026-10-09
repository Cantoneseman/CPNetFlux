#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {

struct FakePipeline {
    struct Candidate {
        std::size_t index = 0;
        bool controlReady = false;
        bool listenerReserved = false;
        bool cancelRequested = false;
    };

    explicit FakePipeline(bool enabled) : enabled(enabled) {}

    bool reserve(std::size_t next) {
        if (!enabled || candidate.has_value() || next >= files.size() || !files[next]) {
            return false;
        }
        candidate = Candidate{next, true, true};
        return true;
    }

    bool handoff(std::size_t index) {
        if (!candidate.has_value() || candidate->index != index || !candidate->controlReady) {
            return false;
        }
        files[index] = false;
        candidate.reset();
        return true;
    }

    void cancelCurrent() {
        if (candidate.has_value()) {
            candidate->cancelRequested = true;
        }
    }

    void failCurrent() {
        cancelCurrent();
        candidate.reset();
    }

    bool enabled = false;
    std::vector<bool> files{true, true, true};
    std::optional<Candidate> candidate;
};

TEST(TreePipelineStateTest, ReservesSwapsAndCleansCandidateOnCurrentFailure) {
    FakePipeline pipeline(true);
    ASSERT_TRUE(pipeline.reserve(1));
    ASSERT_TRUE(pipeline.candidate.has_value());
    EXPECT_TRUE(pipeline.candidate->listenerReserved);
    EXPECT_TRUE(pipeline.handoff(1));
    EXPECT_FALSE(pipeline.candidate.has_value());
    EXPECT_FALSE(pipeline.files[1]);

    ASSERT_TRUE(pipeline.reserve(2));
    pipeline.failCurrent();
    EXPECT_FALSE(pipeline.candidate.has_value());
    EXPECT_TRUE(pipeline.files[2]);
}


TEST(TreePipelineStateTest, CancellationIsRequestedBeforeCandidateCleanup) {
    FakePipeline pipeline(true);
    ASSERT_TRUE(pipeline.reserve(1));
    ASSERT_TRUE(pipeline.candidate.has_value());
    pipeline.cancelCurrent();
    ASSERT_TRUE(pipeline.candidate.has_value());
    EXPECT_TRUE(pipeline.candidate->cancelRequested);
    pipeline.failCurrent();
    EXPECT_FALSE(pipeline.candidate.has_value());
    EXPECT_TRUE(pipeline.files[1]);
}

TEST(TreePipelineStateTest, DepthZeroNeverReserves) {
    FakePipeline pipeline(false);
    EXPECT_FALSE(pipeline.reserve(1));
    EXPECT_FALSE(pipeline.candidate.has_value());
}

TEST(TreePipelineStateTest, CandidateIsSingleAndIndexSpecific) {
    FakePipeline pipeline(true);
    ASSERT_TRUE(pipeline.reserve(1));
    EXPECT_FALSE(pipeline.reserve(2));
    EXPECT_FALSE(pipeline.handoff(2));
    EXPECT_TRUE(pipeline.handoff(1));
}

}  // namespace
