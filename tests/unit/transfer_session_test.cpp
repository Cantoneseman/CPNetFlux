#include "gridflux/core/session/transfer_session.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "gridflux/checkpoint/manifest_store.h"
#include "gridflux/checksum/checksum.h"
#include "gridflux/core/session/final_verify_policy.h"
#include "gridflux/storage/posix_file.h"

namespace {

std::string outputPath(const char* name) {
    return (std::filesystem::temp_directory_path() /
            (std::string(name) + "." + std::to_string(::getpid())))
        .string();
}

void cleanupSessionFiles(const std::string& path, const std::string& transferId) {
    (void)gridflux::storage::PosixFile::removePath(
        gridflux::checkpoint::manifestPathForOutput(path));
    (void)gridflux::storage::PosixFile::removePath(
        gridflux::checkpoint::tempPathForOutput(path, transferId));
}

}  // namespace

TEST(TransferSessionTest, CreatesManifestAndComputesMissingRanges) {
    const std::string path = outputPath("gridflux-session-create");
    const std::string transferId = "session-create";
    cleanupSessionFiles(path, transferId);

    auto session = gridflux::core::session::TransferSession::createNew(
        path, transferId, 4096, 1024, gridflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(session.isOk()) << session.status().message();
    ASSERT_TRUE(session.value().save().isOk());

    EXPECT_TRUE(session.value().recordCompletedRange(0, 1024).isOk());
    EXPECT_TRUE(session.value().recordCompletedRange(2048, 1024).isOk());
    ASSERT_TRUE(session.value().flushManifest().isOk());

    const auto missing = session.value().missingRanges();
    ASSERT_EQ(missing.size(), 2U);
    EXPECT_EQ(missing[0].begin, 1024U);
    EXPECT_EQ(missing[0].end, 2048U);
    EXPECT_EQ(missing[1].begin, 3072U);
    EXPECT_EQ(missing[1].end, 4096U);

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, ResumesFromManifestAndRejectsOverlapRange) {
    const std::string path = outputPath("gridflux-session-resume");
    const std::string transferId = "session-resume";
    cleanupSessionFiles(path, transferId);

    auto created = gridflux::core::session::TransferSession::createNew(
        path, transferId, 2048, 1024, gridflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());
    ASSERT_TRUE(created.value().recordCompletedRange(0, 1024).isOk());
    ASSERT_TRUE(created.value().flushManifest().isOk());

    auto resumed = gridflux::core::session::TransferSession::resume(
        path, transferId, 2048, 1024, gridflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(resumed.isOk()) << resumed.status().message();
    EXPECT_EQ(resumed.value().bytesCompleted(), 1024U);
    EXPECT_TRUE(resumed.value().recordCompletedRange(0, 1024).isOk());
    EXPECT_FALSE(resumed.value().recordCompletedRange(512, 1024).isOk());
    EXPECT_TRUE(resumed.value().recordCompletedRange(1024, 1024).isOk());
    EXPECT_TRUE(resumed.value().missingRanges().empty());

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, RejectsSizeMismatchOnResume) {
    const std::string path = outputPath("gridflux-session-mismatch");
    const std::string transferId = "session-mismatch";
    cleanupSessionFiles(path, transferId);

    auto created = gridflux::core::session::TransferSession::createNew(
        path, transferId, 2048, 1024, gridflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());

    EXPECT_FALSE(gridflux::core::session::TransferSession::resume(
                     path, transferId, 4096, 1024, gridflux::checksum::ChecksumAlgorithm::None)
                     .isOk());

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, FlushesManifestAfterConfiguredVerifiedChunkInterval) {
    const std::string path = outputPath("gridflux-session-flush-interval");
    const std::string transferId = "session-flush-interval";
    cleanupSessionFiles(path, transferId);

    auto created = gridflux::core::session::TransferSession::createNew(
        path, transferId, 2048, 1024, gridflux::checksum::ChecksumAlgorithm::None,
        gridflux::checksum::ChecksumBackend::Auto,
        gridflux::core::session::ManifestFlushPolicy::EveryNChunks, 2);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());

    ASSERT_TRUE(created.value().recordCompletedRange(0, 1024).isOk());
    auto loaded = gridflux::checkpoint::ManifestStore::load(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_TRUE(loaded.value().verifiedChunks.empty());

    ASSERT_TRUE(created.value().recordCompletedRange(1024, 1024).isOk());
    loaded = gridflux::checkpoint::ManifestStore::load(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().verifiedChunks.size(), 2U);
    EXPECT_EQ(created.value().stats().manifestFlushCount, 2U);

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, FinalOnlyManifestFlushDefersUntilForcedFlush) {
    const std::string path = outputPath("gridflux-session-final-only-flush");
    const std::string transferId = "session-final-only-flush";
    cleanupSessionFiles(path, transferId);

    auto created = gridflux::core::session::TransferSession::createNew(
        path, transferId, 2048, 1024, gridflux::checksum::ChecksumAlgorithm::None,
        gridflux::checksum::ChecksumBackend::Auto,
        gridflux::core::session::ManifestFlushPolicy::FinalOnly, 1);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());

    ASSERT_TRUE(created.value().recordCompletedRange(0, 1024).isOk());
    ASSERT_TRUE(created.value().recordCompletedRange(1024, 1024).isOk());
    auto loaded = gridflux::checkpoint::ManifestStore::load(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_TRUE(loaded.value().verifiedChunks.empty());

    ASSERT_TRUE(created.value().flushManifest().isOk());
    loaded = gridflux::checkpoint::ManifestStore::load(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().verifiedChunks.size(), 2U);

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, FailureAndCommitForceManifestFlush) {
    const std::string failedPath = outputPath("gridflux-session-failed-flush");
    const std::string failedTransferId = "session-failed-flush";
    cleanupSessionFiles(failedPath, failedTransferId);

    auto failed = gridflux::core::session::TransferSession::createNew(
        failedPath, failedTransferId, 1024, 1024, gridflux::checksum::ChecksumAlgorithm::None,
        gridflux::checksum::ChecksumBackend::Auto,
        gridflux::core::session::ManifestFlushPolicy::EveryNChunks, 16);
    ASSERT_TRUE(failed.isOk()) << failed.status().message();
    ASSERT_TRUE(failed.value().save().isOk());
    ASSERT_TRUE(failed.value().recordCompletedRange(0, 1024).isOk());
    ASSERT_TRUE(failed.value().markFailed().isOk());

    auto loadedFailed = gridflux::checkpoint::ManifestStore::load(failed.value().manifestPath());
    ASSERT_TRUE(loadedFailed.isOk()) << loadedFailed.status().message();
    EXPECT_EQ(loadedFailed.value().verifiedChunks.size(), 1U);
    EXPECT_EQ(loadedFailed.value().state, gridflux::checkpoint::ManifestState::Failed);
    cleanupSessionFiles(failedPath, failedTransferId);

    const std::string committedPath = outputPath("gridflux-session-committed-flush");
    const std::string committedTransferId = "session-committed-flush";
    cleanupSessionFiles(committedPath, committedTransferId);

    auto committed = gridflux::core::session::TransferSession::createNew(
        committedPath, committedTransferId, 1024, 1024, gridflux::checksum::ChecksumAlgorithm::None,
        gridflux::checksum::ChecksumBackend::Auto,
        gridflux::core::session::ManifestFlushPolicy::EveryNChunks, 16);
    ASSERT_TRUE(committed.isOk()) << committed.status().message();
    ASSERT_TRUE(committed.value().save().isOk());
    ASSERT_TRUE(committed.value().recordCompletedRange(0, 1024).isOk());
    ASSERT_TRUE(committed.value().markCommitted().isOk());

    auto loadedCommitted =
        gridflux::checkpoint::ManifestStore::load(committed.value().manifestPath());
    ASSERT_TRUE(loadedCommitted.isOk()) << loadedCommitted.status().message();
    EXPECT_EQ(loadedCommitted.value().verifiedChunks.size(), 1U);
    EXPECT_EQ(loadedCommitted.value().state, gridflux::checkpoint::ManifestState::Committed);
    cleanupSessionFiles(committedPath, committedTransferId);
}

TEST(TransferSessionTest, VerifiesTempChunksAndMarksCorruptChunkMissing) {
    const std::string path = outputPath("gridflux-session-verify");
    const std::string transferId = "session-verify";
    cleanupSessionFiles(path, transferId);

    auto fileResult = gridflux::storage::PosixFile::openReadWriteExclusive(
        gridflux::checkpoint::tempPathForOutput(path, transferId));
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    gridflux::storage::PosixFile file = std::move(fileResult.value());

    const std::vector<std::uint8_t> good(1024, 7);
    ASSERT_TRUE(file.writeAtAll(0, good.data(), good.size()).isOk());

    gridflux::checksum::ChecksumComputer computer(gridflux::checksum::ChecksumAlgorithm::Crc32c);
    computer.update(good.data(), good.size());

    auto created = gridflux::core::session::TransferSession::createNew(
        path, transferId, 1024, 1024, gridflux::checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().recordVerifiedChunk(0, 0, 1024, computer.finalize()).isOk());
    EXPECT_TRUE(created.value().missingRanges().empty());

    const std::vector<std::uint8_t> bad{9};
    ASSERT_TRUE(file.writeAtAll(10, bad.data(), bad.size()).isOk());
    ASSERT_TRUE(created.value().verifyTempChunks(file).isOk());

    const auto missing = created.value().missingRanges();
    ASSERT_EQ(missing.size(), 1U);
    EXPECT_EQ(missing[0].begin, 0U);
    EXPECT_EQ(missing[0].end, 1024U);

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, RejectsChecksumMismatchForDuplicateChunk) {
    const std::string path = outputPath("gridflux-session-duplicate-checksum");
    const std::string transferId = "session-duplicate-checksum";
    cleanupSessionFiles(path, transferId);

    auto created = gridflux::core::session::TransferSession::createNew(
        path, transferId, 1024, 1024, gridflux::checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value()
                    .recordVerifiedChunk(
                        0, 0, 1024, {gridflux::checksum::ChecksumAlgorithm::Crc32c, 0x11111111U})
                    .isOk());

    EXPECT_TRUE(created.value()
                    .recordVerifiedChunk(
                        0, 0, 1024, {gridflux::checksum::ChecksumAlgorithm::Crc32c, 0x11111111U})
                    .isOk());
    EXPECT_FALSE(created.value()
                     .recordVerifiedChunk(
                         0, 0, 1024, {gridflux::checksum::ChecksumAlgorithm::Crc32c, 0x22222222U})
                     .isOk());

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, FinalVerifyPolicyEligibilityIsStrict) {
    using gridflux::core::session::FinalVerifyPolicy;
    using gridflux::core::session::canCommitWithVerifiedChunksFinalVerify;
    using gridflux::core::session::canUseVerifiedChunksFinalVerify;

    EXPECT_TRUE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::VerifiedChunks,
                                                gridflux::checksum::ChecksumAlgorithm::Crc32c,
                                                1024, 1024, false));
    EXPECT_FALSE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::Full,
                                                 gridflux::checksum::ChecksumAlgorithm::Crc32c,
                                                 1024, 1024, false));
    EXPECT_FALSE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::VerifiedChunks,
                                                 gridflux::checksum::ChecksumAlgorithm::None, 1024,
                                                 1024, false));
    EXPECT_FALSE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::VerifiedChunks,
                                                 gridflux::checksum::ChecksumAlgorithm::Crc32c,
                                                 1024, 512, false));
    EXPECT_FALSE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::VerifiedChunks,
                                                 gridflux::checksum::ChecksumAlgorithm::Crc32c,
                                                 1024, 1024, true));
    EXPECT_TRUE(canCommitWithVerifiedChunksFinalVerify(
        FinalVerifyPolicy::VerifiedChunks, gridflux::checksum::ChecksumAlgorithm::Crc32c, 1024,
        1024, false, true));
    EXPECT_FALSE(canCommitWithVerifiedChunksFinalVerify(
        FinalVerifyPolicy::VerifiedChunks, gridflux::checksum::ChecksumAlgorithm::Crc32c, 1024,
        1024, false, false));
}
