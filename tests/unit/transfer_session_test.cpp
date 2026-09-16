#include "cpnetflux/core/session/transfer_session.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/core/session/final_verify_policy.h"
#include "cpnetflux/storage/posix_file.h"

namespace {

std::string outputPath(const char* name) {
    return (std::filesystem::temp_directory_path() /
            (std::string(name) + "." + std::to_string(::getpid())))
        .string();
}

void cleanupSessionFiles(const std::string& path, const std::string& transferId) {
    (void)cpnetflux::storage::PosixFile::removePath(
        cpnetflux::checkpoint::manifestPathForOutput(path));
    (void)cpnetflux::storage::PosixFile::removePath(
        cpnetflux::checkpoint::tempPathForOutput(path, transferId));
}

}  // namespace

TEST(TransferSessionTest, CreatesManifestAndComputesMissingRanges) {
    const std::string path = outputPath("cpnetflux-session-create");
    const std::string transferId = "session-create";
    cleanupSessionFiles(path, transferId);

    auto session = cpnetflux::core::session::TransferSession::createNew(
        path, transferId, 4096, 1024, cpnetflux::checksum::ChecksumAlgorithm::None);
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
    const std::string path = outputPath("cpnetflux-session-resume");
    const std::string transferId = "session-resume";
    cleanupSessionFiles(path, transferId);

    auto created = cpnetflux::core::session::TransferSession::createNew(
        path, transferId, 2048, 1024, cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());
    ASSERT_TRUE(created.value().recordCompletedRange(0, 1024).isOk());
    ASSERT_TRUE(created.value().flushManifest().isOk());

    auto resumed = cpnetflux::core::session::TransferSession::resume(
        path, transferId, 2048, 1024, cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(resumed.isOk()) << resumed.status().message();
    EXPECT_EQ(resumed.value().bytesCompleted(), 1024U);
    EXPECT_TRUE(resumed.value().recordCompletedRange(0, 1024).isOk());
    EXPECT_FALSE(resumed.value().recordCompletedRange(512, 1024).isOk());
    EXPECT_TRUE(resumed.value().recordCompletedRange(1024, 1024).isOk());
    EXPECT_TRUE(resumed.value().missingRanges().empty());

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, RejectsSizeMismatchOnResume) {
    const std::string path = outputPath("cpnetflux-session-mismatch");
    const std::string transferId = "session-mismatch";
    cleanupSessionFiles(path, transferId);

    auto created = cpnetflux::core::session::TransferSession::createNew(
        path, transferId, 2048, 1024, cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());

    EXPECT_FALSE(cpnetflux::core::session::TransferSession::resume(
                     path, transferId, 4096, 1024, cpnetflux::checksum::ChecksumAlgorithm::None)
                     .isOk());

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, FlushesManifestAfterConfiguredVerifiedChunkInterval) {
    const std::string path = outputPath("cpnetflux-session-flush-interval");
    const std::string transferId = "session-flush-interval";
    cleanupSessionFiles(path, transferId);

    auto created = cpnetflux::core::session::TransferSession::createNew(
        path, transferId, 2048, 1024, cpnetflux::checksum::ChecksumAlgorithm::None,
        cpnetflux::checksum::ChecksumBackend::Auto,
        cpnetflux::core::session::ManifestFlushPolicy::EveryNChunks, 2);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());

    ASSERT_TRUE(created.value().recordCompletedRange(0, 1024).isOk());
    auto loaded = cpnetflux::checkpoint::ManifestStore::load(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_TRUE(loaded.value().verifiedChunks.empty());

    ASSERT_TRUE(created.value().recordCompletedRange(1024, 1024).isOk());
    loaded = cpnetflux::checkpoint::ManifestStore::load(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().verifiedChunks.size(), 2U);
    EXPECT_EQ(created.value().stats().manifestFlushCount, 2U);

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, FinalOnlyManifestFlushDefersUntilForcedFlush) {
    const std::string path = outputPath("cpnetflux-session-final-only-flush");
    const std::string transferId = "session-final-only-flush";
    cleanupSessionFiles(path, transferId);

    auto created = cpnetflux::core::session::TransferSession::createNew(
        path, transferId, 2048, 1024, cpnetflux::checksum::ChecksumAlgorithm::None,
        cpnetflux::checksum::ChecksumBackend::Auto,
        cpnetflux::core::session::ManifestFlushPolicy::FinalOnly, 1);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());

    ASSERT_TRUE(created.value().recordCompletedRange(0, 1024).isOk());
    ASSERT_TRUE(created.value().recordCompletedRange(1024, 1024).isOk());
    auto loaded = cpnetflux::checkpoint::ManifestStore::load(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_TRUE(loaded.value().verifiedChunks.empty());

    ASSERT_TRUE(created.value().flushManifest().isOk());
    loaded = cpnetflux::checkpoint::ManifestStore::load(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().verifiedChunks.size(), 2U);

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, FailureAndCommitForceManifestFlush) {
    const std::string failedPath = outputPath("cpnetflux-session-failed-flush");
    const std::string failedTransferId = "session-failed-flush";
    cleanupSessionFiles(failedPath, failedTransferId);

    auto failed = cpnetflux::core::session::TransferSession::createNew(
        failedPath, failedTransferId, 1024, 1024, cpnetflux::checksum::ChecksumAlgorithm::None,
        cpnetflux::checksum::ChecksumBackend::Auto,
        cpnetflux::core::session::ManifestFlushPolicy::EveryNChunks, 16);
    ASSERT_TRUE(failed.isOk()) << failed.status().message();
    ASSERT_TRUE(failed.value().save().isOk());
    ASSERT_TRUE(failed.value().recordCompletedRange(0, 1024).isOk());
    ASSERT_TRUE(failed.value().markFailed().isOk());

    auto loadedFailed = cpnetflux::checkpoint::ManifestStore::load(failed.value().manifestPath());
    ASSERT_TRUE(loadedFailed.isOk()) << loadedFailed.status().message();
    EXPECT_EQ(loadedFailed.value().verifiedChunks.size(), 1U);
    EXPECT_EQ(loadedFailed.value().state, cpnetflux::checkpoint::ManifestState::Failed);
    cleanupSessionFiles(failedPath, failedTransferId);

    const std::string committedPath = outputPath("cpnetflux-session-committed-flush");
    const std::string committedTransferId = "session-committed-flush";
    cleanupSessionFiles(committedPath, committedTransferId);

    auto committed = cpnetflux::core::session::TransferSession::createNew(
        committedPath, committedTransferId, 1024, 1024, cpnetflux::checksum::ChecksumAlgorithm::None,
        cpnetflux::checksum::ChecksumBackend::Auto,
        cpnetflux::core::session::ManifestFlushPolicy::EveryNChunks, 16);
    ASSERT_TRUE(committed.isOk()) << committed.status().message();
    ASSERT_TRUE(committed.value().save().isOk());
    ASSERT_TRUE(committed.value().recordCompletedRange(0, 1024).isOk());
    ASSERT_TRUE(committed.value().markCommitted().isOk());

    auto loadedCommitted =
        cpnetflux::checkpoint::ManifestStore::load(committed.value().manifestPath());
    ASSERT_TRUE(loadedCommitted.isOk()) << loadedCommitted.status().message();
    EXPECT_EQ(loadedCommitted.value().verifiedChunks.size(), 1U);
    EXPECT_EQ(loadedCommitted.value().state, cpnetflux::checkpoint::ManifestState::Committed);
    cleanupSessionFiles(committedPath, committedTransferId);
}

TEST(TransferSessionTest, VerifiesTempChunksAndMarksCorruptChunkMissing) {
    const std::string path = outputPath("cpnetflux-session-verify");
    const std::string transferId = "session-verify";
    cleanupSessionFiles(path, transferId);

    auto fileResult = cpnetflux::storage::PosixFile::openReadWriteExclusive(
        cpnetflux::checkpoint::tempPathForOutput(path, transferId));
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());

    const std::vector<std::uint8_t> good(1024, 7);
    ASSERT_TRUE(file.writeAtAll(0, good.data(), good.size()).isOk());

    cpnetflux::checksum::ChecksumComputer computer(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    computer.update(good.data(), good.size());

    auto created = cpnetflux::core::session::TransferSession::createNew(
        path, transferId, 1024, 1024, cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
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
    const std::string path = outputPath("cpnetflux-session-duplicate-checksum");
    const std::string transferId = "session-duplicate-checksum";
    cleanupSessionFiles(path, transferId);

    auto created = cpnetflux::core::session::TransferSession::createNew(
        path, transferId, 1024, 1024, cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value()
                    .recordVerifiedChunk(
                        0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x11111111U})
                    .isOk());

    EXPECT_TRUE(created.value()
                    .recordVerifiedChunk(
                        0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x11111111U})
                    .isOk());
    EXPECT_FALSE(created.value()
                     .recordVerifiedChunk(
                         0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x22222222U})
                     .isOk());

    cleanupSessionFiles(path, transferId);
}

TEST(TransferSessionTest, FinalVerifyPolicyEligibilityIsStrict) {
    using cpnetflux::core::session::FinalVerifyPolicy;
    using cpnetflux::core::session::canCommitWithVerifiedChunksFinalVerify;
    using cpnetflux::core::session::canUseVerifiedChunksFinalVerify;

    EXPECT_TRUE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::VerifiedChunks,
                                                cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                1024, 1024, false));
    EXPECT_FALSE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::Full,
                                                 cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                 1024, 1024, false));
    EXPECT_FALSE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::VerifiedChunks,
                                                 cpnetflux::checksum::ChecksumAlgorithm::None, 1024,
                                                 1024, false));
    EXPECT_FALSE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::VerifiedChunks,
                                                 cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                 1024, 512, false));
    EXPECT_FALSE(canUseVerifiedChunksFinalVerify(FinalVerifyPolicy::VerifiedChunks,
                                                 cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                 1024, 1024, true));
    EXPECT_TRUE(canCommitWithVerifiedChunksFinalVerify(
        FinalVerifyPolicy::VerifiedChunks, cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 1024,
        1024, false, true));
    EXPECT_FALSE(canCommitWithVerifiedChunksFinalVerify(
        FinalVerifyPolicy::VerifiedChunks, cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 1024,
        1024, false, false));
}
