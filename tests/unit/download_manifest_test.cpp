#include "cpnetflux/checkpoint/download_manifest.h"

#include <gtest/gtest.h>

#include <string>

TEST(DownloadManifestTest, BuildsPaths) {
    EXPECT_EQ(cpnetflux::checkpoint::downloadManifestPathForOutput("/tmp/out"),
              "/tmp/out.cpnetflux.download.manifest");
    EXPECT_EQ(cpnetflux::checkpoint::downloadTempPathForOutput("/tmp/out", "abc"),
              "/tmp/out.part.abc");
}

TEST(DownloadManifestTest, SerializesAndParsesStableTextFormat) {
    cpnetflux::checkpoint::DownloadManifest manifest;
    manifest.transferId = "download-phase3c";
    manifest.sourcePath = "nested/source.bin";
    manifest.targetPath = "/tmp/grid flux/download.bin";
    manifest.tempPath = "/tmp/grid flux/download.bin.part.download-phase3c";
    manifest.totalSize = 4096;
    manifest.chunkSize = 1024;
    manifest.createdAtUnixNanos = 10;
    manifest.updatedAtUnixNanos = 20;
    manifest.state = cpnetflux::checkpoint::ManifestState::Transferring;
    manifest.verifiedChunks = {
        {0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x11111111U}},
        {2, 2048, 2048, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x22222222U}},
    };

    const auto serialized = cpnetflux::checkpoint::serializeDownloadManifest(manifest);
    ASSERT_TRUE(serialized.isOk()) << serialized.status().message();
    EXPECT_NE(serialized.value().find("manifest_version=1"), std::string::npos);
    EXPECT_NE(serialized.value().find("source_path_hex="), std::string::npos);
    EXPECT_NE(serialized.value().find("manifest_body_crc32c="), std::string::npos);

    const auto parsed = cpnetflux::checkpoint::parseDownloadManifest(serialized.value());
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().transferId, manifest.transferId);
    EXPECT_EQ(parsed.value().sourcePath, manifest.sourcePath);
    EXPECT_EQ(parsed.value().targetPath, manifest.targetPath);
    EXPECT_EQ(parsed.value().tempPath, manifest.tempPath);
    ASSERT_EQ(parsed.value().completedRanges.size(), 2U);
    EXPECT_EQ(parsed.value().completedRanges[1].begin, 2048U);
    ASSERT_EQ(parsed.value().verifiedChunks.size(), 2U);
}

TEST(DownloadManifestTest, RejectsCorruptBodyChecksum) {
    cpnetflux::checkpoint::DownloadManifest manifest;
    manifest.transferId = "download-corrupt";
    manifest.sourcePath = "source.bin";
    manifest.targetPath = "/tmp/out";
    manifest.tempPath = "/tmp/out.part.download-corrupt";
    manifest.totalSize = 1024;
    manifest.chunkSize = 1024;
    manifest.createdAtUnixNanos = 1;
    manifest.updatedAtUnixNanos = 1;
    manifest.verifiedChunks = {
        {0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x12345678U}},
    };

    auto serialized = cpnetflux::checkpoint::serializeDownloadManifest(manifest);
    ASSERT_TRUE(serialized.isOk()) << serialized.status().message();
    std::string corrupt = serialized.value();
    const std::size_t offset = corrupt.find("total_size=1024");
    ASSERT_NE(offset, std::string::npos);
    corrupt.replace(offset, std::string("total_size=1024").size(), "total_size=2048");

    EXPECT_FALSE(cpnetflux::checkpoint::parseDownloadManifest(corrupt).isOk());
}
