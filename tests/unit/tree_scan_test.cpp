#include "cpnetflux/core/tree/tree_scan.h"
#include "cpnetflux/checkpoint/download_manifest.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(TreeScanTest, ScansRegularFilesInStableOrder) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-scan-stable";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "b");
    std::filesystem::create_directories(root / "a");
    std::ofstream(root / "b" / "two.bin").put('2');
    std::ofstream(root / "a" / "one.bin").put('1');
    std::ofstream(root / "empty.bin");

    auto scanned = cpnetflux::core::tree::scanLocalTree(root.string());
    ASSERT_TRUE(scanned.isOk()) << scanned.status().message();
    ASSERT_EQ(scanned.value().size(), 3U);
    EXPECT_EQ(scanned.value()[0].relativePath, "a/one.bin");
    EXPECT_EQ(scanned.value()[1].relativePath, "b/two.bin");
    EXPECT_EQ(scanned.value()[2].relativePath, "empty.bin");

    std::filesystem::remove_all(root);
}

TEST(TreeScanTest, ValidatesTreeRelativePath) {
    EXPECT_TRUE(cpnetflux::core::tree::validateTreeRelativePath("nested/file.bin").isOk());
    EXPECT_FALSE(cpnetflux::core::tree::validateTreeRelativePath("").isOk());
    EXPECT_FALSE(cpnetflux::core::tree::validateTreeRelativePath("/abs").isOk());
    EXPECT_FALSE(cpnetflux::core::tree::validateTreeRelativePath("../escape").isOk());
    EXPECT_FALSE(cpnetflux::core::tree::validateTreeRelativePath("C:/drive").isOk());
    EXPECT_FALSE(cpnetflux::core::tree::validateTreeRelativePath("bad\\path").isOk());
}

TEST(TreeScanTest, ExcludesValidatedSidecarsAndKeepsUserFiles) {
    namespace fs = std::filesystem;
    namespace checkpoint = cpnetflux::checkpoint;
    const fs::path root = fs::temp_directory_path() / "cpnetflux-tree-scan-sidecars";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path uploadPayload = root / "upload.bin";
    const fs::path downloadPayload = root / "download.bin";
    std::ofstream(uploadPayload) << "upload";
    std::ofstream(downloadPayload) << "download";
    std::ofstream(root / "notes.cpnetflux.user.txt") << "ordinary user file";
    std::ofstream(root / "fake.cpnetflux.manifest") << "not a CPNetFlux manifest";

    checkpoint::TransferManifest uploadManifest;
    uploadManifest.transferId = "sidecar-test";
    uploadManifest.outputPath = uploadPayload.string();
    uploadManifest.tempPath = checkpoint::tempPathForOutput(uploadManifest.outputPath,
                                                            uploadManifest.transferId);
    uploadManifest.totalSize = 6;
    uploadManifest.chunkSize = 1024;
    uploadManifest.createdAtUnixNanos = 1;
    uploadManifest.updatedAtUnixNanos = 1;
    uploadManifest.state = checkpoint::ManifestState::Committed;
    auto uploadText = checkpoint::serializeTransferManifest(uploadManifest);
    ASSERT_TRUE(uploadText.isOk()) << uploadText.status().message();
    std::ofstream(checkpoint::manifestPathForOutput(uploadManifest.outputPath)) << uploadText.value();
    std::ofstream(root / "misplaced.cpnetflux.manifest") << uploadText.value();

    checkpoint::DownloadManifest downloadManifest;
    downloadManifest.transferId = "download-sidecar-test";
    downloadManifest.sourcePath = "/remote/download.bin";
    downloadManifest.targetPath = downloadPayload.string();
    downloadManifest.tempPath = checkpoint::downloadTempPathForOutput(
        downloadManifest.targetPath, downloadManifest.transferId);
    downloadManifest.totalSize = 8;
    downloadManifest.chunkSize = 1024;
    downloadManifest.createdAtUnixNanos = 1;
    downloadManifest.updatedAtUnixNanos = 1;
    downloadManifest.state = checkpoint::ManifestState::Committed;
    auto downloadText = checkpoint::serializeDownloadManifest(downloadManifest);
    ASSERT_TRUE(downloadText.isOk()) << downloadText.status().message();
    std::ofstream(checkpoint::downloadManifestPathForOutput(downloadManifest.targetPath))
        << downloadText.value();

    auto scanned = cpnetflux::core::tree::scanLocalTree(root.string());
    ASSERT_TRUE(scanned.isOk()) << scanned.status().message();
    std::vector<std::string> names;
    for (const auto& file : scanned.value()) names.push_back(file.relativePath);
    EXPECT_EQ(names, (std::vector<std::string>{
        "download.bin", "fake.cpnetflux.manifest", "misplaced.cpnetflux.manifest",
        "notes.cpnetflux.user.txt", "upload.bin"}));
    fs::remove_all(root);
}

TEST(TreeScanTest, RejectsSymlinkByDefault) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-scan-symlink";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "target.bin").put('x');
    std::error_code error;
    std::filesystem::create_symlink(root / "target.bin", root / "link.bin", error);
    if (error) {
        GTEST_SKIP() << "symlink creation unavailable";
    }
    auto scanned = cpnetflux::core::tree::scanLocalTree(root.string());
    EXPECT_FALSE(scanned.isOk());
    std::filesystem::remove_all(root);
}
