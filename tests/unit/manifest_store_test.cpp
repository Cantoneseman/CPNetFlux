#include "cpnetflux/checkpoint/manifest_store.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <string>

#include "cpnetflux/storage/posix_file.h"

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
