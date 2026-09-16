#include "cpnetflux/core/session/download_session.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "cpnetflux/checkpoint/download_manifest.h"
#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/storage/posix_file.h"

namespace {

std::string outputPath(const char* name) {
    return (std::filesystem::temp_directory_path() /
            (std::string(name) + "." + std::to_string(::getpid())))
        .string();
}

void cleanupDownloadFiles(const std::string& path, const std::string& transferId) {
    (void)cpnetflux::storage::PosixFile::removePath(
        cpnetflux::checkpoint::downloadManifestPathForOutput(path));
    (void)cpnetflux::storage::PosixFile::removePath(
        cpnetflux::checkpoint::downloadTempPathForOutput(path, transferId));
}

}  // namespace

TEST(DownloadSessionTest, CreatesManifestAndComputesMissingRanges) {
    const std::string path = outputPath("cpnetflux-download-session-create");
    const std::string transferId = "download-session-create";
    cleanupDownloadFiles(path, transferId);

    auto session = cpnetflux::core::session::DownloadSession::createNew(
        path, "source.bin", transferId, 4096, 1024, cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(session.isOk()) << session.status().message();
    ASSERT_TRUE(session.value().save().isOk());
    EXPECT_TRUE(
        session.value()
            .recordVerifiedChunk(0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::None, 0})
            .isOk());
    EXPECT_TRUE(
        session.value()
            .recordVerifiedChunk(2, 2048, 1024, {cpnetflux::checksum::ChecksumAlgorithm::None, 0})
            .isOk());

    const auto missing = session.value().missingRanges();
    ASSERT_EQ(missing.size(), 2U);
    EXPECT_EQ(missing[0].begin, 1024U);
    EXPECT_EQ(missing[0].end, 2048U);
    EXPECT_EQ(missing[1].begin, 3072U);
    EXPECT_EQ(missing[1].end, 4096U);

    cleanupDownloadFiles(path, transferId);
}

TEST(DownloadSessionTest, ResumesAndRejectsMetadataMismatch) {
    const std::string path = outputPath("cpnetflux-download-session-resume");
    const std::string transferId = "download-session-resume";
    cleanupDownloadFiles(path, transferId);

    auto created = cpnetflux::core::session::DownloadSession::createNew(
        path, "source.bin", transferId, 2048, 1024, cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());
    ASSERT_TRUE(
        created.value()
            .recordVerifiedChunk(0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::None, 0})
            .isOk());
    ASSERT_TRUE(created.value().flushManifest().isOk());

    auto resumed = cpnetflux::core::session::DownloadSession::resume(
        path, "source.bin", transferId, 2048, 1024, cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(resumed.isOk()) << resumed.status().message();
    EXPECT_EQ(resumed.value().bytesCompleted(), 1024U);

    EXPECT_FALSE(
        cpnetflux::core::session::DownloadSession::resume(
            path, "other.bin", transferId, 2048, 1024, cpnetflux::checksum::ChecksumAlgorithm::None)
            .isOk());

    cleanupDownloadFiles(path, transferId);
}

TEST(DownloadSessionTest, VerifiesTempChunksAndMarksCorruptChunkMissing) {
    const std::string path = outputPath("cpnetflux-download-session-verify");
    const std::string transferId = "download-session-verify";
    cleanupDownloadFiles(path, transferId);

    auto fileResult = cpnetflux::storage::PosixFile::openReadWriteExclusive(
        cpnetflux::checkpoint::downloadTempPathForOutput(path, transferId));
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());

    const std::vector<std::uint8_t> good(1024, 7);
    ASSERT_TRUE(file.writeAtAll(0, good.data(), good.size()).isOk());

    cpnetflux::checksum::ChecksumComputer computer(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    computer.update(good.data(), good.size());

    auto session = cpnetflux::core::session::DownloadSession::createNew(
        path, "source.bin", transferId, 1024, 1024, cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(session.isOk()) << session.status().message();
    ASSERT_TRUE(session.value().recordVerifiedChunk(0, 0, 1024, computer.finalize()).isOk());

    const std::vector<std::uint8_t> bad{9};
    ASSERT_TRUE(file.writeAtAll(10, bad.data(), bad.size()).isOk());
    ASSERT_TRUE(session.value().verifyTempChunks(file).isOk());

    const auto missing = session.value().missingRanges();
    ASSERT_EQ(missing.size(), 1U);
    EXPECT_EQ(missing[0].begin, 0U);
    EXPECT_EQ(missing[0].end, 1024U);
    EXPECT_EQ(session.value().stats().removedCorruptChunks, 1U);

    cleanupDownloadFiles(path, transferId);
}

TEST(DownloadSessionTest, DuplicateChecksumIsIdempotentButMismatchFails) {
    const std::string path = outputPath("cpnetflux-download-session-duplicate");
    const std::string transferId = "download-session-duplicate";
    cleanupDownloadFiles(path, transferId);

    auto session = cpnetflux::core::session::DownloadSession::createNew(
        path, "source.bin", transferId, 1024, 1024, cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(session.isOk()) << session.status().message();
    EXPECT_TRUE(session.value()
                    .recordVerifiedChunk(
                        0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x11111111U})
                    .isOk());
    EXPECT_TRUE(session.value()
                    .recordVerifiedChunk(
                        0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x11111111U})
                    .isOk());
    EXPECT_FALSE(session.value()
                     .recordVerifiedChunk(
                         0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x22222222U})
                     .isOk());

    cleanupDownloadFiles(path, transferId);
}

TEST(DownloadSessionTest, FlushesManifestAfterConfiguredVerifiedChunkInterval) {
    const std::string path = outputPath("cpnetflux-download-session-flush-interval");
    const std::string transferId = "download-session-flush-interval";
    cleanupDownloadFiles(path, transferId);

    auto created = cpnetflux::core::session::DownloadSession::createNew(
        path, "source.bin", transferId, 2048, 1024,
        cpnetflux::checksum::ChecksumAlgorithm::None, cpnetflux::checksum::ChecksumBackend::Auto,
        cpnetflux::core::session::ManifestFlushPolicy::EveryNChunks, 2);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());

    ASSERT_TRUE(
        created.value()
            .recordVerifiedChunk(0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::None, 0})
            .isOk());
    auto loaded = cpnetflux::checkpoint::loadDownloadManifest(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_TRUE(loaded.value().verifiedChunks.empty());

    ASSERT_TRUE(
        created.value()
            .recordVerifiedChunk(1, 1024, 1024, {cpnetflux::checksum::ChecksumAlgorithm::None, 0})
            .isOk());
    loaded = cpnetflux::checkpoint::loadDownloadManifest(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().verifiedChunks.size(), 2U);
    EXPECT_EQ(created.value().stats().manifestFlushCount, 2U);

    cleanupDownloadFiles(path, transferId);
}

TEST(DownloadSessionTest, FinalOnlyManifestFlushDefersUntilForcedFlush) {
    const std::string path = outputPath("cpnetflux-download-session-final-only");
    const std::string transferId = "download-session-final-only";
    cleanupDownloadFiles(path, transferId);

    auto created = cpnetflux::core::session::DownloadSession::createNew(
        path, "source.bin", transferId, 2048, 1024,
        cpnetflux::checksum::ChecksumAlgorithm::None, cpnetflux::checksum::ChecksumBackend::Auto,
        cpnetflux::core::session::ManifestFlushPolicy::FinalOnly, 1);
    ASSERT_TRUE(created.isOk()) << created.status().message();
    ASSERT_TRUE(created.value().save().isOk());

    ASSERT_TRUE(
        created.value()
            .recordVerifiedChunk(0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::None, 0})
            .isOk());
    ASSERT_TRUE(
        created.value()
            .recordVerifiedChunk(1, 1024, 1024, {cpnetflux::checksum::ChecksumAlgorithm::None, 0})
            .isOk());
    auto loaded = cpnetflux::checkpoint::loadDownloadManifest(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_TRUE(loaded.value().verifiedChunks.empty());

    ASSERT_TRUE(created.value().flushManifest().isOk());
    loaded = cpnetflux::checkpoint::loadDownloadManifest(created.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().verifiedChunks.size(), 2U);

    cleanupDownloadFiles(path, transferId);
}

TEST(DownloadSessionTest, FailureAndCommitForceManifestFlush) {
    const std::string path = outputPath("cpnetflux-download-session-force-flush");
    const std::string transferId = "download-session-force-flush";
    cleanupDownloadFiles(path, transferId);

    auto session = cpnetflux::core::session::DownloadSession::createNew(
        path, "source.bin", transferId, 1024, 1024,
        cpnetflux::checksum::ChecksumAlgorithm::None, cpnetflux::checksum::ChecksumBackend::Auto,
        cpnetflux::core::session::ManifestFlushPolicy::EveryNChunks, 16);
    ASSERT_TRUE(session.isOk()) << session.status().message();
    ASSERT_TRUE(session.value().save().isOk());
    ASSERT_TRUE(
        session.value()
            .recordVerifiedChunk(0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::None, 0})
            .isOk());
    ASSERT_TRUE(session.value().markCommitted().isOk());

    auto loaded = cpnetflux::checkpoint::loadDownloadManifest(session.value().manifestPath());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().verifiedChunks.size(), 1U);
    EXPECT_EQ(loaded.value().state, cpnetflux::checkpoint::ManifestState::Committed);

    cleanupDownloadFiles(path, transferId);
}
