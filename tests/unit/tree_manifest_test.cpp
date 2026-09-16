#include "cpnetflux/core/tree/tree_manifest.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

TEST(TreeManifestTest, BuildsManifestPaths) {
    EXPECT_EQ(cpnetflux::core::tree::treeManifestPathForUpload("/tmp/source"),
              "/tmp/source.cpnetflux.tree.upload.manifest");
    EXPECT_EQ(cpnetflux::core::tree::treeManifestPathForDownload("/tmp/dest"),
              "/tmp/dest.cpnetflux.tree.download.manifest");
}

TEST(TreeManifestTest, SerializesAndParsesRoundtrip) {
    cpnetflux::core::tree::TreeManifest manifest;
    manifest.mode = cpnetflux::core::tree::TreeTransferMode::Upload;
    manifest.rootLogicalPath = "/tmp/data set";
    manifest.createdAtUnixNanos = 10;
    manifest.updatedAtUnixNanos = 20;
    manifest.files = {
        {"b/two.bin", 2, 22, "22222222222222222222222222222222",
         cpnetflux::core::tree::TreeFileStatus::Completed, ""},
        {"a/one.bin", 1, 11, "11111111111111111111111111111111",
         cpnetflux::core::tree::TreeFileStatus::Pending, "waiting"},
    };

    auto serialized = cpnetflux::core::tree::serializeTreeManifest(manifest);
    ASSERT_TRUE(serialized.isOk()) << serialized.status().message();
    EXPECT_NE(serialized.value().find("manifest_body_crc32c="), std::string::npos);

    auto parsed = cpnetflux::core::tree::parseTreeManifest(serialized.value());
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().rootLogicalPath, manifest.rootLogicalPath);
    ASSERT_EQ(parsed.value().files.size(), 2U);
    EXPECT_EQ(parsed.value().files[0].relativePath, "a/one.bin");
    EXPECT_EQ(parsed.value().files[0].status, cpnetflux::core::tree::TreeFileStatus::Pending);
    EXPECT_EQ(parsed.value().files[0].error, "waiting");
}

TEST(TreeManifestTest, RejectsCorruptBodyChecksum) {
    cpnetflux::core::tree::TreeManifest manifest;
    manifest.rootLogicalPath = "/tmp/root";
    manifest.createdAtUnixNanos = 1;
    manifest.updatedAtUnixNanos = 1;
    manifest.files = {{"file.bin", 1, 2, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                       cpnetflux::core::tree::TreeFileStatus::Pending, ""}};

    auto serialized = cpnetflux::core::tree::serializeTreeManifest(manifest);
    ASSERT_TRUE(serialized.isOk()) << serialized.status().message();
    std::string corrupt = serialized.value();
    const std::size_t offset = corrupt.find("created_at_unix_ns=1");
    ASSERT_NE(offset, std::string::npos);
    corrupt.replace(offset, std::string("created_at_unix_ns=1").size(), "created_at_unix_ns=2");
    EXPECT_FALSE(cpnetflux::core::tree::parseTreeManifest(corrupt).isOk());
}

TEST(TreeManifestTest, SavesAndLoadsAtomicManifest) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-manifest";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path path = root / "tree.manifest";

    cpnetflux::core::tree::TreeManifest manifest;
    manifest.rootLogicalPath = root.string();
    manifest.createdAtUnixNanos = 1;
    manifest.updatedAtUnixNanos = 2;
    manifest.files = {{"file.bin", 1, 2, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                       cpnetflux::core::tree::TreeFileStatus::Completed, ""}};

    ASSERT_TRUE(cpnetflux::core::tree::saveTreeManifestAtomic(path.string(), manifest).isOk());
    auto loaded = cpnetflux::core::tree::loadTreeManifest(path.string());
    ASSERT_TRUE(loaded.isOk()) << loaded.status().message();
    EXPECT_EQ(loaded.value().files[0].relativePath, "file.bin");
    std::filesystem::remove_all(root);
}

TEST(TreeManifestTest, DetectsCompleteManifest) {
    cpnetflux::core::tree::TreeManifest manifest;
    manifest.rootLogicalPath = "root";
    manifest.files = {
        {"a.txt", 1, 10, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
         cpnetflux::core::tree::TreeFileStatus::Completed, ""},
        {"b.txt", 2, 20, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
         cpnetflux::core::tree::TreeFileStatus::Completed, ""},
    };
    EXPECT_TRUE(cpnetflux::core::tree::isTreeTransferComplete(manifest));
    manifest.files[1].status = cpnetflux::core::tree::TreeFileStatus::Changed;
    EXPECT_FALSE(cpnetflux::core::tree::isTreeTransferComplete(manifest));
}
