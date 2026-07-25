#include "gridflux/checkpoint/manifest_store.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <string>

#include "gridflux/storage/posix_file.h"

namespace {

std::string testPath(const char* name) {
    return (std::filesystem::temp_directory_path() /
            (std::string(name) + "." + std::to_string(::getpid())))
        .string();
}

gridflux::checkpoint::TransferManifest makeManifest(const std::string& outputPath) {
    gridflux::checkpoint::TransferManifest manifest;
    manifest.transferId = "manifest-store-test";
    manifest.outputPath = outputPath;
    manifest.tempPath = outputPath + ".part.manifest-store-test";
    manifest.totalSize = 2048;
    manifest.chunkSize = 1024;
    manifest.createdAtUnixNanos = 10;
    manifest.updatedAtUnixNanos = 20;
    manifest.state = gridflux::checkpoint::ManifestState::Transferring;
    manifest.verifiedChunks = {
        {0, 0, 1024, {gridflux::checksum::ChecksumAlgorithm::Crc32c, 0x12345678U}},
    };
    return manifest;
}

}  // namespace

TEST(ManifestStoreTest, SavesAndLoadsManifestAtomically) {
    const std::string outputPath = testPath("gridflux-manifest-output");
    const std::string manifestPath = gridflux::checkpoint::manifestPathForOutput(outputPath);
    const gridflux::checkpoint::TransferManifest manifest = makeManifest(outputPath);

    ASSERT_TRUE(gridflux::checkpoint::ManifestStore::saveAtomic(manifestPath, manifest).isOk());
    const auto loaded = gridflux::checkpoint::ManifestStore::load(manifestPath);
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().transferId, manifest.transferId);
    EXPECT_EQ(loaded.value().completedRanges.size(), 1U);

    (void)gridflux::storage::PosixFile::removePath(manifestPath);
}

TEST(ManifestStoreTest, RejectsMissingManifest) {
    const std::string manifestPath = testPath("gridflux-missing-manifest");
    (void)gridflux::storage::PosixFile::removePath(manifestPath);

    EXPECT_FALSE(gridflux::checkpoint::ManifestStore::load(manifestPath).isOk());
}
