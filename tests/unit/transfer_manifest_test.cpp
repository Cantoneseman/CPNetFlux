#include "cpnetflux/checkpoint/transfer_manifest.h"

#include <gtest/gtest.h>

#include <string>

TEST(TransferManifestTest, ValidatesTransferId) {
    EXPECT_TRUE(cpnetflux::checkpoint::isValidTransferId("phase2a-smoke_01"));
    EXPECT_FALSE(cpnetflux::checkpoint::isValidTransferId(""));
    EXPECT_FALSE(cpnetflux::checkpoint::isValidTransferId("../bad"));
    EXPECT_FALSE(cpnetflux::checkpoint::isValidTransferId("bad/id"));
}

TEST(TransferManifestTest, BuildsManifestAndTempPaths) {
    EXPECT_EQ(cpnetflux::checkpoint::manifestPathForOutput("/tmp/out"),
              "/tmp/out.cpnetflux.manifest");
    EXPECT_EQ(cpnetflux::checkpoint::tempPathForOutput("/tmp/out", "abc"), "/tmp/out.part.abc");
}

TEST(TransferManifestTest, SerializesAndParsesStableTextFormat) {
    cpnetflux::checkpoint::TransferManifest manifest;
    manifest.transferId = "phase2a";
    manifest.outputPath = "/tmp/grid flux/out.bin";
    manifest.tempPath = "/tmp/grid flux/out.bin.part.phase2a";
    manifest.totalSize = 4096;
    manifest.chunkSize = 1024;
    manifest.createdAtUnixNanos = 10;
    manifest.updatedAtUnixNanos = 20;
    manifest.state = cpnetflux::checkpoint::ManifestState::Transferring;
    manifest.verifiedChunks = {
        {0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x11111111U}},
        {2, 2048, 2048, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x22222222U}},
    };

    const auto serialized = cpnetflux::checkpoint::serializeTransferManifest(manifest);
    ASSERT_TRUE(serialized.isOk()) << serialized.status().message();
    EXPECT_NE(serialized.value().find("manifest_version=2"), std::string::npos);
    EXPECT_NE(serialized.value().find("checksum_algorithm=crc32c"), std::string::npos);
    EXPECT_NE(serialized.value().find("manifest_body_crc32c="), std::string::npos);

    const auto parsed = cpnetflux::checkpoint::parseTransferManifest(serialized.value());
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().transferId, "phase2a");
    EXPECT_EQ(parsed.value().outputPath, "/tmp/grid flux/out.bin");
    EXPECT_EQ(parsed.value().tempPath, "/tmp/grid flux/out.bin.part.phase2a");
    EXPECT_EQ(parsed.value().totalSize, 4096U);
    EXPECT_EQ(parsed.value().chunkSize, 1024U);
    ASSERT_EQ(parsed.value().completedRanges.size(), 2U);
    EXPECT_EQ(parsed.value().completedRanges[1].begin, 2048U);
    EXPECT_EQ(parsed.value().completedRanges[1].end, 4096U);
    ASSERT_EQ(parsed.value().verifiedChunks.size(), 2U);
    EXPECT_EQ(parsed.value().verifiedChunks[0].checksum.value, 0x11111111U);
}

TEST(TransferManifestTest, RejectsCorruptManifest) {
    EXPECT_FALSE(cpnetflux::checkpoint::parseTransferManifest("not-a-manifest\n").isOk());

    cpnetflux::checkpoint::TransferManifest manifest;
    manifest.transferId = "phase2a";
    manifest.outputPath = "/tmp/out";
    manifest.tempPath = "/tmp/out.part.phase2a";
    manifest.totalSize = 1024;
    manifest.chunkSize = 1024;
    manifest.createdAtUnixNanos = 1;
    manifest.updatedAtUnixNanos = 1;
    manifest.verifiedChunks = {
        {0, 0, 2048, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0}},
    };

    EXPECT_FALSE(cpnetflux::checkpoint::serializeTransferManifest(manifest).isOk());
}

TEST(TransferManifestTest, RejectsModifiedBodyChecksum) {
    cpnetflux::checkpoint::TransferManifest manifest;
    manifest.transferId = "phase2b";
    manifest.outputPath = "/tmp/out";
    manifest.tempPath = "/tmp/out.part.phase2b";
    manifest.totalSize = 1024;
    manifest.chunkSize = 1024;
    manifest.createdAtUnixNanos = 1;
    manifest.updatedAtUnixNanos = 1;
    manifest.verifiedChunks = {
        {0, 0, 1024, {cpnetflux::checksum::ChecksumAlgorithm::Crc32c, 0x12345678U}},
    };

    auto serialized = cpnetflux::checkpoint::serializeTransferManifest(manifest);
    ASSERT_TRUE(serialized.isOk()) << serialized.status().message();
    std::string corrupt = serialized.value();
    const std::size_t offset = corrupt.find("total_size=1024");
    ASSERT_NE(offset, std::string::npos);
    corrupt.replace(offset, std::string("total_size=1024").size(), "total_size=2048");

    EXPECT_FALSE(cpnetflux::checkpoint::parseTransferManifest(corrupt).isOk());
}

TEST(TransferManifestTest, ParsesVersionOneManifestForChecksumNoneCompatibility) {
    const std::string manifest =
        "manifest_version=1\n"
        "transfer_id=phase2a\n"
        "output_path_hex=2f746d702f6f7574\n"
        "temp_path_hex=2f746d702f6f75742e706172742e70686173653261\n"
        "total_size=2048\n"
        "chunk_size=1024\n"
        "created_at_unix_ns=1\n"
        "updated_at_unix_ns=2\n"
        "state=failed\n"
        "completed_ranges=0-1024\n";

    const auto parsed = cpnetflux::checkpoint::parseTransferManifest(manifest);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().version, cpnetflux::checkpoint::kTransferManifestVersionV1);
    EXPECT_EQ(parsed.value().checksumAlgorithm, cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_EQ(parsed.value().completedRanges.size(), 1U);
    EXPECT_TRUE(parsed.value().verifiedChunks.empty());
}
