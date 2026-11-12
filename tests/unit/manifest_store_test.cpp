#include "cpnetflux/checkpoint/manifest_store.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <string>

#include "cpnetflux/storage/posix_file.h"
#include "cpnetflux/checkpoint/download_manifest.h"

namespace {

std::string testPath(const char* name) {
    return (std::filesystem::temp_directory_path() /
            (std::string(name) + "." + std::to_string(::getpid())))
        .string();
}

cpnetflux::checkpoint::TransferManifest makeManifest(const std::string& outputPath) {
    cpnetflux::checkpoint::TransferManifest manifest;
    manifest.transferId = "manifest-store-test";
    manifest.outputPath = outputPath;
    manifest.tempPath = outputPath + ".part.manifest-store-test";
    manifest.totalSize = 2048;
    manifest.chunkSize = 1024;
    manifest.createdAtUnixNanos = 10;
    manifest.updatedAtUnixNanos = 20;
    manifest.state = cpnetflux::checkpoint::ManifestState::Transferring;
    manifest.verifiedChunks = {
        {0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x12345678U}},
    };
    return manifest;
}

}  // namespace

TEST(ManifestStoreTest, SavesAndLoadsManifestAtomically) {
    const std::string outputPath = testPath("cpnetflux-manifest-output");
    const std::string manifestPath = cpnetflux::checkpoint::manifestPathForOutput(outputPath);
    const cpnetflux::checkpoint::TransferManifest manifest = makeManifest(outputPath);

    ASSERT_TRUE(cpnetflux::checkpoint::ManifestStore::saveAtomic(manifestPath, manifest).isOk());
    const auto loaded = cpnetflux::checkpoint::ManifestStore::load(manifestPath);
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().transferId, manifest.transferId);
    EXPECT_EQ(loaded.value().completedRanges.size(), 1U);

    (void)cpnetflux::storage::PosixFile::removePath(manifestPath);
}

TEST(ManifestStoreTest, RejectsMissingManifest) {
    const std::string manifestPath = testPath("cpnetflux-missing-manifest");
    (void)cpnetflux::storage::PosixFile::removePath(manifestPath);

    EXPECT_FALSE(cpnetflux::checkpoint::ManifestStore::load(manifestPath).isOk());
}

TEST(ManifestStoreTest, ObservesActualBytesAndFailedRenameForBothFormats) {
    namespace fs = std::filesystem;
    using namespace cpnetflux::checkpoint;
    const auto root=testPath("cpnetflux-manifest-observation");
    fs::create_directories(root);
    const auto path=root+"/upload.manifest";
    const auto manifest=makeManifest(root+"/output");
    ManifestSaveStats stats;
    ASSERT_TRUE(ManifestStore::saveAtomic(path,manifest,&stats).isOk());
    EXPECT_EQ(stats.attempts,1U);
    EXPECT_EQ(stats.renameCalls,1U);
    EXPECT_EQ(stats.serializedBytes,fs::file_size(path));
    EXPECT_EQ(stats.failures,0U);
    const auto bad=root+"/directory";
    fs::create_directory(bad);
    EXPECT_FALSE(ManifestStore::saveAtomic(bad,manifest,&stats).isOk());
    EXPECT_EQ(stats.attempts,2U);
    EXPECT_EQ(stats.renameCalls,2U);
    EXPECT_EQ(stats.failures,1U);
    EXPECT_EQ(stats.serializedBytes,2*fs::file_size(path));
    DownloadManifest download;
    download.transferId=manifest.transferId;
    download.sourcePath="source";
    download.targetPath=manifest.outputPath;
    download.tempPath=manifest.tempPath;
    download.totalSize=manifest.totalSize;
    download.chunkSize=manifest.chunkSize;
    download.createdAtUnixNanos=10;
    download.updatedAtUnixNanos=20;
    ManifestSaveStats received;
    const auto downloaded=root+"/download.manifest";
    ASSERT_TRUE(saveDownloadManifestAtomic(downloaded,download,&received).isOk());
    EXPECT_EQ(received.serializedBytes,fs::file_size(downloaded));
    EXPECT_FALSE(saveDownloadManifestAtomic(bad,download,&received).isOk());
    EXPECT_EQ(received.attempts,2U);
    EXPECT_EQ(received.renameCalls,2U);
    EXPECT_EQ(received.failures,1U);
    // Failure before serialization cannot claim any filesystem operation.
    auto invalid=manifest;
    invalid.chunkSize=0;
    ManifestSaveStats rejected;
    EXPECT_FALSE(ManifestStore::saveAtomic(path,invalid,&rejected).isOk());
    EXPECT_EQ(rejected.attempts,1U);
    EXPECT_EQ(rejected.serializedBytes,0U);
    EXPECT_EQ(rejected.renameCalls,0U);
    EXPECT_EQ(rejected.failures,1U);
    fs::remove_all(root);
}
