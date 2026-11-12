#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cpnetflux/core/session/download_session.h"
#include "cpnetflux/core/session/transfer_session.h"

namespace {
namespace session = cpnetflux::core::session;
namespace checksum = cpnetflux::checksum;

template <class Receiver>
class SessionRecordLookupTest : public ::testing::Test {
 protected:
    void SetUp() override {
        const auto pattern = (std::filesystem::temp_directory_path() / "cpnetflux-record-lookup-XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        ASSERT_NE(::mkdtemp(name.data()), nullptr);
        directory_ = name.data();
        output_ = (directory_ / "output.bin").string();
    }
    void TearDown() override {
        if (!directory_.empty()) std::filesystem::remove_all(directory_);
    }
    auto create(checksum::ChecksumAlgorithm algorithm, std::uint64_t size = 24) {
        if constexpr (std::is_same_v<Receiver, session::DownloadSession>)
            return Receiver::createNew(output_, "source.bin", "record-lookup", size, 4, algorithm,
                checksum::ChecksumBackend::Auto, session::ManifestFlushPolicy::FinalOnly);
        else
            return Receiver::createNew(output_, "record-lookup", size, 4, algorithm,
                checksum::ChecksumBackend::Auto, session::ManifestFlushPolicy::FinalOnly);
    }
    auto resume(checksum::ChecksumAlgorithm algorithm, std::uint64_t size = 24) {
        if constexpr (std::is_same_v<Receiver, session::DownloadSession>)
            return Receiver::resume(output_, "source.bin", "record-lookup", size, 4, algorithm,
                checksum::ChecksumBackend::Auto, session::ManifestFlushPolicy::FinalOnly);
        else
            return Receiver::resume(output_, "record-lookup", size, 4, algorithm,
                checksum::ChecksumBackend::Auto, session::ManifestFlushPolicy::FinalOnly);
    }
    static checksum::ChecksumValue crc(std::uint32_t value = 1) {
        return {checksum::ChecksumAlgorithm::Crc32c, value};
    }
    std::filesystem::path directory_;
    std::string output_;
};

using Receivers = ::testing::Types<session::TransferSession, session::DownloadSession>;
TYPED_TEST_SUITE(SessionRecordLookupTest, Receivers);

TYPED_TEST(SessionRecordLookupTest, OrderedAppendAvoidsScanningPriorRecordsAcrossMoveAndSave) {
    constexpr std::uint64_t count = 16384;
    auto created = this->create(checksum::ChecksumAlgorithm::Crc32c, count * 4);
    ASSERT_TRUE(created.isOk());
    session::VerifiedChunkRecordStats observed;
    created.value().setRecordStats(&observed);
    for (std::uint64_t index = 0; index < count / 2; ++index)
        ASSERT_TRUE(created.value().recordVerifiedChunk(index, index * 4, 4, this->crc()).isOk());
    ASSERT_TRUE(created.value().save().isOk());
    auto moved = std::move(created.value());
    for (std::uint64_t index = count / 2; index < count; ++index)
        ASSERT_TRUE(moved.recordVerifiedChunk(index, index * 4, 4, this->crc()).isOk());
    EXPECT_TRUE(moved.missingRanges().empty());
    EXPECT_EQ(observed.lookupCalls, count);
    EXPECT_LE(observed.candidateRecordsExamined, count * 2);
}

TYPED_TEST(SessionRecordLookupTest, OutOfOrderSaveAndResumePreserveIdentityConflicts) {
    auto created = this->create(checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(created.isOk());
    ASSERT_TRUE(created.value().recordVerifiedChunk(4, 8, 4, this->crc(3)).isOk());
    ASSERT_TRUE(created.value().recordVerifiedChunk(9, 0, 4, this->crc()).isOk());
    ASSERT_TRUE(created.value().save().isOk());
    auto resumed = this->resume(checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(resumed.isOk());
    EXPECT_TRUE(resumed.value().recordVerifiedChunk(4, 8, 4, this->crc(3)).isOk());
    EXPECT_FALSE(resumed.value().recordVerifiedChunk(55, 8, 4, this->crc(3)).isOk());
    EXPECT_FALSE(resumed.value().recordVerifiedChunk(4, 16, 4, this->crc(3)).isOk());
    EXPECT_TRUE(resumed.value().recordVerifiedChunk(5, 16, 4, this->crc(4)).isOk());
    EXPECT_FALSE(resumed.value().recordVerifiedChunk(10, 2, 4, this->crc()).isOk());
    EXPECT_TRUE(resumed.value().recordVerifiedChunk(11, 4, 4, this->crc(2)).isOk());
    ASSERT_TRUE(resumed.value().save().isOk());
    auto again = this->resume(checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(again.isOk());
    EXPECT_EQ(again.value().bytesCompleted(), 16U);
    EXPECT_TRUE(again.value().recordVerifiedChunk(11, 4, 4, this->crc(2)).isOk());
    EXPECT_FALSE(again.value().recordVerifiedChunk(11, 4, 4, this->crc(9)).isOk());
}

TYPED_TEST(SessionRecordLookupTest, ChecksumNoneAllowsRepeatedIdsButRejectsOffsetConflicts) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const checksum::ChecksumValue none{checksum::ChecksumAlgorithm::None, 0};
    auto created = this->create(checksum::ChecksumAlgorithm::None, 12);
    ASSERT_TRUE(created.isOk());
    ASSERT_TRUE(created.value().recordVerifiedChunk(maximum, 0, 4, none).isOk());
    ASSERT_TRUE(created.value().recordVerifiedChunk(maximum, 4, 4, none).isOk());
    EXPECT_FALSE(created.value().recordVerifiedChunk(7, 0, 4, none).isOk());
    ASSERT_TRUE(created.value().save().isOk());
    auto resumed = this->resume(checksum::ChecksumAlgorithm::None, 12);
    ASSERT_TRUE(resumed.isOk());
    EXPECT_TRUE(resumed.value().recordVerifiedChunk(maximum, 4, 4, none).isOk());
    EXPECT_TRUE(resumed.value().recordVerifiedChunk(maximum, 8, 4, none).isOk());
    EXPECT_TRUE(resumed.value().missingRanges().empty());
}

TYPED_TEST(SessionRecordLookupTest, CorruptionPruningAllowsRepairAfterMove) {
    auto created = this->create(checksum::ChecksumAlgorithm::Crc32c, 12);
    ASSERT_TRUE(created.isOk());
    auto file = cpnetflux::storage::PosixFile::openReadWriteExclusive(created.value().manifest().tempPath);
    ASSERT_TRUE(file.isOk());
    const std::string bytes = "AAAABBBBCCCC";
    ASSERT_TRUE(file.value().writeAtAll(0, reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()).isOk());
    std::vector<checksum::ChecksumValue> checksums;
    for (std::uint64_t index = 0; index < 3; ++index) {
        checksum::ChecksumComputer computer(checksum::ChecksumAlgorithm::Crc32c);
        computer.update(reinterpret_cast<const std::uint8_t*>(bytes.data() + index * 4), 4);
        checksums.push_back(computer.finalize());
        ASSERT_TRUE(created.value().recordVerifiedChunk(index, index * 4, 4, checksums.back()).isOk());
    }
    const std::uint8_t bad = 'X';
    ASSERT_TRUE(file.value().writeAtAll(4, &bad, 1).isOk());
    ASSERT_TRUE(created.value().verifyTempChunks(file.value()).isOk());
    EXPECT_EQ(created.value().stats().removedCorruptChunks, 1U);
    auto moved = std::move(created.value());
    ASSERT_TRUE(file.value().writeAtAll(4, reinterpret_cast<const std::uint8_t*>(bytes.data() + 4), 4).isOk());
    EXPECT_TRUE(moved.recordVerifiedChunk(1, 4, 4, checksums[1]).isOk());
    EXPECT_FALSE(moved.recordVerifiedChunk(0, 16, 4, checksums[0]).isOk());
    ASSERT_TRUE(moved.verifyTempChunks(file.value()).isOk());
    EXPECT_TRUE(moved.missingRanges().empty());
    ASSERT_TRUE(moved.save().isOk());
    auto resumed = this->resume(checksum::ChecksumAlgorithm::Crc32c, 12);
    ASSERT_TRUE(resumed.isOk());
    EXPECT_TRUE(resumed.value().recordVerifiedChunk(1, 4, 4, checksums[1]).isOk());
}

TYPED_TEST(SessionRecordLookupTest, ExtremeOffsetsAndIdsDoNotOverflowIdentityChecks) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto created = this->create(checksum::ChecksumAlgorithm::Crc32c, maximum);
    ASSERT_TRUE(created.isOk());
    EXPECT_TRUE(created.value().recordVerifiedChunk(maximum - 1, maximum - 2, 1, this->crc()).isOk());
    EXPECT_TRUE(created.value().recordVerifiedChunk(maximum, maximum - 1, 1, this->crc()).isOk());
    EXPECT_FALSE(created.value().recordVerifiedChunk(maximum, 0, 1, this->crc()).isOk());
    EXPECT_TRUE(created.value().recordVerifiedChunk(0, 0, 1, this->crc()).isOk());
    EXPECT_FALSE(created.value().recordVerifiedChunk(1, maximum, 1, this->crc()).isOk());
}
TYPED_TEST(SessionRecordLookupTest, NextIdQueriesAvoidRepeatedManifestScans) {
    constexpr std::uint64_t count = 8192;
    auto created = this->create(checksum::ChecksumAlgorithm::Crc32c, count * 4);
    ASSERT_TRUE(created.isOk());
    session::VerifiedChunkRecordStats observed;
    created.value().setRecordStats(&observed);
    for (std::uint64_t i = 0; i < count; ++i) {
        EXPECT_EQ(created.value().nextVerifiedChunkId(), i);
        ASSERT_TRUE(created.value().recordVerifiedChunk(i, i * 4, 4, this->crc()).isOk());
    }
    EXPECT_EQ(observed.recordIdQueries, count);
    EXPECT_LE(observed.recordIdCandidatesExamined, count);
}

TYPED_TEST(SessionRecordLookupTest, NextIdMatchesSparseManifestAcrossSaveResumeAndMove) {
    auto made = this->create(checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(made.isOk());
    auto check = [](const auto& value) {
        std::uint64_t expected = 0;
        for (const auto& r : value.manifest().verifiedChunks) {
            if (r.chunkId == UINT64_MAX) { expected = UINT64_MAX; break; }
            expected = std::max(expected, r.chunkId + 1);
        }
        EXPECT_EQ(value.nextVerifiedChunkId(), expected);
    };
    check(made.value());
    ASSERT_TRUE(made.value().recordVerifiedChunk(0, 0, 4, this->crc()).isOk());
    check(made.value());
    ASSERT_TRUE(made.value().recordVerifiedChunk(700, 8, 4, this->crc(3)).isOk());
    ASSERT_TRUE(made.value().recordVerifiedChunk(3, 4, 4, this->crc(2)).isOk());
    check(made.value());
    EXPECT_FALSE(made.value().recordVerifiedChunk(900, 0, 4, this->crc()).isOk());
    EXPECT_EQ(made.value().nextVerifiedChunkId(), 701U);
    ASSERT_TRUE(made.value().save().isOk());
    auto resumed = this->resume(checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(resumed.isOk());
    check(resumed.value());
    auto moved = std::move(resumed.value());
    check(moved);
    ASSERT_TRUE(moved.recordVerifiedChunk(UINT64_MAX - 1, 12, 4, this->crc()).isOk());
    check(moved);
    EXPECT_EQ(moved.nextVerifiedChunkId(), UINT64_MAX);
    ASSERT_TRUE(moved.recordVerifiedChunk(UINT64_MAX, 16, 4, this->crc()).isOk());
    check(moved);
    ASSERT_TRUE(moved.save().isOk());
    auto exhausted = this->resume(checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(exhausted.isOk());
    check(exhausted.value());
    EXPECT_EQ(exhausted.value().nextVerifiedChunkId(), UINT64_MAX);
}

TYPED_TEST(SessionRecordLookupTest, NextIdNoneStillUsesMaximumAndEmptyStartsAtZero) {
    auto made = this->create(checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(made.isOk());
    const checksum::ChecksumValue none{checksum::ChecksumAlgorithm::None, 0};
    EXPECT_EQ(made.value().nextVerifiedChunkId(), 0U);
    ASSERT_TRUE(made.value().recordVerifiedChunk(9, 4, 4, none).isOk());
    ASSERT_TRUE(made.value().recordVerifiedChunk(2, 0, 4, none).isOk());
    EXPECT_EQ(made.value().nextVerifiedChunkId(), 10U);
    ASSERT_TRUE(made.value().recordVerifiedChunk(UINT64_MAX, 8, 4, none).isOk());
    EXPECT_EQ(made.value().nextVerifiedChunkId(), UINT64_MAX);
}
TYPED_TEST(SessionRecordLookupTest, NextIdRebuildDropsPrunedMaximumAndResetsWhenEmpty) {
    auto made = this->create(checksum::ChecksumAlgorithm::Crc32c, 12);
    ASSERT_TRUE(made.isOk());
    auto file = cpnetflux::storage::PosixFile::openReadWriteExclusive(made.value().manifest().tempPath);
    ASSERT_TRUE(file.isOk());
    const std::string bytes = "AAAABBBBCCCC";
    ASSERT_TRUE(file.value().writeAtAll(0, reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()).isOk());
    const std::uint64_t ids[] = {1, 1000, 2};
    for (std::size_t i = 0; i < 3; ++i) {
        checksum::ChecksumComputer computer(checksum::ChecksumAlgorithm::Crc32c);
        computer.update(reinterpret_cast<const std::uint8_t*>(bytes.data() + i * 4), 4);
        ASSERT_TRUE(made.value().recordVerifiedChunk(ids[i], i * 4, 4, computer.finalize()).isOk());
    }
    EXPECT_EQ(made.value().nextVerifiedChunkId(), 1001U);
    const std::uint8_t bad = 'X';
    ASSERT_TRUE(file.value().writeAtAll(4, &bad, 1).isOk());
    ASSERT_TRUE(made.value().verifyTempChunks(file.value()).isOk());
    EXPECT_EQ(made.value().nextVerifiedChunkId(), 3U);
    auto resumed = this->resume(checksum::ChecksumAlgorithm::Crc32c, 12);
    ASSERT_TRUE(resumed.isOk());
    EXPECT_EQ(resumed.value().nextVerifiedChunkId(), 3U);
    ASSERT_TRUE(file.value().writeAtAll(0, &bad, 1).isOk());
    ASSERT_TRUE(file.value().writeAtAll(8, &bad, 1).isOk());
    ASSERT_TRUE(resumed.value().verifyTempChunks(file.value()).isOk());
    EXPECT_TRUE(resumed.value().manifest().verifiedChunks.empty());
    EXPECT_EQ(resumed.value().nextVerifiedChunkId(), 0U);
    auto again = this->resume(checksum::ChecksumAlgorithm::Crc32c, 12);
    ASSERT_TRUE(again.isOk());
    EXPECT_EQ(again.value().nextVerifiedChunkId(), 0U);
}

}  // namespace
