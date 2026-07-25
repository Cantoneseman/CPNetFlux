#include "gridflux/core/tree/tree_manifest.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

TEST(TreeManifestTest, BuildsManifestPaths) {
    EXPECT_EQ(gridflux::core::tree::treeManifestPathForUpload("/tmp/source"),
              "/tmp/source.gridflux.tree.upload.manifest");
    EXPECT_EQ(gridflux::core::tree::treeManifestPathForDownload("/tmp/dest"),
              "/tmp/dest.gridflux.tree.download.manifest");
}

TEST(TreeManifestTest, SerializesAndParsesRoundtrip) {
    gridflux::core::tree::TreeManifest manifest;
    manifest.mode = gridflux::core::tree::TreeTransferMode::Upload;
    manifest.rootLogicalPath = "/tmp/data set";
    manifest.createdAtUnixNanos = 10;
    manifest.updatedAtUnixNanos = 20;
    manifest.files = {
        {"b/two.bin", 2, 22, "22222222222222222222222222222222",
         gridflux::core::tree::TreeFileStatus::Completed, ""},
        {"a/one.bin", 1, 11, "11111111111111111111111111111111",
         gridflux::core::tree::TreeFileStatus::Pending, "waiting"},
    };

    auto serialized = gridflux::core::tree::serializeTreeManifest(manifest);
    ASSERT_TRUE(serialized.isOk()) << serialized.status().message();
    EXPECT_NE(serialized.value().find("manifest_body_crc32c="), std::string::npos);

    auto parsed = gridflux::core::tree::parseTreeManifest(serialized.value());
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().rootLogicalPath, manifest.rootLogicalPath);
    ASSERT_EQ(parsed.value().files.size(), 2U);
    EXPECT_EQ(parsed.value().files[0].relativePath, "a/one.bin");
    EXPECT_EQ(parsed.value().files[0].status, gridflux::core::tree::TreeFileStatus::Pending);
    EXPECT_EQ(parsed.value().files[0].error, "waiting");
}

TEST(TreeManifestTest, RejectsCorruptBodyChecksum) {
    gridflux::core::tree::TreeManifest manifest;
    manifest.rootLogicalPath = "/tmp/root";
    manifest.createdAtUnixNanos = 1;
    manifest.updatedAtUnixNanos = 1;
    manifest.files = {{"file.bin", 1, 2, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                       gridflux::core::tree::TreeFileStatus::Pending, ""}};

    auto serialized = gridflux::core::tree::serializeTreeManifest(manifest);
    ASSERT_TRUE(serialized.isOk()) << serialized.status().message();
    std::string corrupt = serialized.value();
    const std::size_t offset = corrupt.find("created_at_unix_ns=1");
    ASSERT_NE(offset, std::string::npos);
    corrupt.replace(offset, std::string("created_at_unix_ns=1").size(), "created_at_unix_ns=2");
    EXPECT_FALSE(gridflux::core::tree::parseTreeManifest(corrupt).isOk());
}

TEST(TreeManifestTest, SavesAndLoadsAtomicManifest) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gridflux-tree-manifest";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path path = root / "tree.manifest";

    gridflux::core::tree::TreeManifest manifest;
    manifest.rootLogicalPath = root.string();
    manifest.createdAtUnixNanos = 1;
    manifest.updatedAtUnixNanos = 2;
    manifest.files = {{"file.bin", 1, 2, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                       gridflux::core::tree::TreeFileStatus::Completed, ""}};

    ASSERT_TRUE(gridflux::core::tree::saveTreeManifestAtomic(path.string(), manifest).isOk());
    auto loaded = gridflux::core::tree::loadTreeManifest(path.string());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().files[0].relativePath, "file.bin");
    std::filesystem::remove_all(root);
}

TEST(TreeManifestTest, DetectsCompleteManifest) {
    gridflux::core::tree::TreeManifest manifest;
    manifest.rootLogicalPath = "root";
    manifest.files = {
        {"a.txt", 1, 10, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
         gridflux::core::tree::TreeFileStatus::Completed, ""},
        {"b.txt", 2, 20, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
         gridflux::core::tree::TreeFileStatus::Completed, ""},
    };
    EXPECT_TRUE(gridflux::core::tree::isTreeTransferComplete(manifest));
    manifest.files[1].status = gridflux::core::tree::TreeFileStatus::Changed;
    EXPECT_FALSE(gridflux::core::tree::isTreeTransferComplete(manifest));
}
