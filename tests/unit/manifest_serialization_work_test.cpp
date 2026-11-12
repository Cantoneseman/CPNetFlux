#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "cpnetflux/checkpoint/download_manifest.h"
#include "cpnetflux/checkpoint/manifest_save_stats.h"
#include "cpnetflux/checksum/crc32c.h"

namespace {
namespace checkpoint = cpnetflux::checkpoint;
namespace checksum = cpnetflux::checksum;
using Record = checkpoint::ChunkChecksumRecord;

std::string legacyHex(const std::string& text) {
    std::ostringstream result;
    for (unsigned char value : text)
        result << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(value);
    return result.str();
}

// Retains the pre-optimization stream-based wire text as an independent byte oracle.
template <class Manifest>
cpnetflux::common::Result<std::string> legacyBytes(const Manifest& manifest) {
    auto ordered = manifest.verifiedChunks;
    std::sort(ordered.begin(), ordered.end(), [](const Record& a, const Record& b) {
        return a.offset < b.offset || (a.offset == b.offset && a.chunkId < b.chunkId);
    });
    cpnetflux::core::chunk::RangeList coverage;
    for (const auto& record : ordered) {
        const auto status = coverage.insert(record.offset, record.length, manifest.totalSize);
        if (!status.isOk()) return status;
    }
    std::ostringstream ranges, records, body;
    for (std::size_t index = 0; index < coverage.ranges().size(); ++index) {
        if (index) ranges << ',';
        ranges << coverage.ranges()[index].begin << '-' << coverage.ranges()[index].end;
    }
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        if (index) records << ',';
        const auto& record = ordered[index];
        records << record.chunkId << ':' << record.offset << ':' << record.length << ':'
                << checksum::checksumAlgorithmName(record.checksum.algorithm) << ':'
                << std::hex << std::setw(8) << std::setfill('0') << record.checksum.value << std::dec;
    }
    body << "manifest_version=" << manifest.version << '\n'
         << "transfer_id=" << manifest.transferId << '\n';
    if constexpr (std::is_same_v<Manifest, checkpoint::DownloadManifest>) {
        body << "source_path_hex=" << legacyHex(manifest.sourcePath) << '\n'
             << "target_path_hex=" << legacyHex(manifest.targetPath) << '\n';
    } else {
        body << "output_path_hex=" << legacyHex(manifest.outputPath) << '\n';
    }
    body << "temp_path_hex=" << legacyHex(manifest.tempPath) << '\n'
         << "total_size=" << manifest.totalSize << '\n'
         << "chunk_size=" << manifest.chunkSize << '\n'
         << "checksum_algorithm=" << checksum::checksumAlgorithmName(manifest.checksumAlgorithm) << '\n'
         << "created_at_unix_ns=" << manifest.createdAtUnixNanos << '\n'
         << "updated_at_unix_ns=" << manifest.updatedAtUnixNanos << '\n'
         << "state=" << checkpoint::manifestStateName(manifest.state) << '\n'
         << "completed_ranges=" << ranges.str() << '\n'
         << "verified_chunks=" << records.str() << '\n';
    const auto text = body.str();
    body << "manifest_body_crc32c=" << std::hex << std::setw(8) << std::setfill('0')
         << checksum::crc32c(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()) << '\n';
    return body.str();
}

template <class Manifest>
auto serialize(const Manifest& manifest, checkpoint::ManifestSaveStats* stats = nullptr) {
    if constexpr (std::is_same_v<Manifest, checkpoint::DownloadManifest>)
        return checkpoint::serializeDownloadManifest(manifest, stats);
    else
        return checkpoint::serializeTransferManifest(manifest, stats);
}

template <class Manifest>
class ManifestSerializationWorkTest : public ::testing::Test {
 protected:
    Manifest fixture(std::uint64_t count = 64) {
        Manifest manifest;
        manifest.transferId = "serialize-oracle";
        const auto path = std::string("/tmp/a b\t\n\\") + char(0) + char(0xff);
        if constexpr (std::is_same_v<Manifest, checkpoint::DownloadManifest>) {
            manifest.sourcePath = path + "source";
            manifest.targetPath = path + "target";
        } else {
            manifest.outputPath = path + "output";
        }
        manifest.tempPath = path + "temp";
        manifest.totalSize = count * 8;
        manifest.chunkSize = 4;
        manifest.createdAtUnixNanos = 1;
        manifest.updatedAtUnixNanos = std::numeric_limits<std::uint64_t>::max();
        manifest.state = checkpoint::ManifestState::Transferring;
        manifest.completedRanges = {{0, 1}}; // Serializer must derive coverage from records.
        for (std::uint64_t index = 0; index < count; ++index)
            manifest.verifiedChunks.push_back({index, index * 8, 4,
                {checksum::ChecksumAlgorithm::Crc32c, static_cast<std::uint32_t>(index * 7919)}});
        return manifest;
    }
};
using Manifests = ::testing::Types<checkpoint::TransferManifest, checkpoint::DownloadManifest>;
TYPED_TEST_SUITE(ManifestSerializationWorkTest, Manifests);

TYPED_TEST(ManifestSerializationWorkTest, LegacyBytesCoverEmptyShuffledSparseAndExtremeRecords) {
    std::mt19937 generator(0x43504eU);
    for (std::uint64_t count : {0U, 1U, 64U, 257U}) {
        auto manifest = this->fixture(count);
        std::shuffle(manifest.verifiedChunks.begin(), manifest.verifiedChunks.end(), generator);
        const auto before = legacyBytes(manifest);
        const auto after = serialize(manifest);
        ASSERT_TRUE(before.isOk());
        ASSERT_TRUE(after.isOk()) << after.status().message();
        EXPECT_EQ(after.value(), before.value());
    }
    auto extreme = this->fixture(0);
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    extreme.totalSize = maximum;
    extreme.verifiedChunks = {{maximum, maximum - 2, 1, {checksum::ChecksumAlgorithm::Crc32c, UINT32_MAX}},
                             {0, 0, 1, {checksum::ChecksumAlgorithm::Crc32c, 0}}};
    ASSERT_TRUE(serialize(extreme).isOk());
    EXPECT_EQ(serialize(extreme).value(), legacyBytes(extreme).value());
    extreme.checksumAlgorithm = checksum::ChecksumAlgorithm::None;
    for (auto& record : extreme.verifiedChunks) {
        record.chunkId = maximum;
        record.checksum = {checksum::ChecksumAlgorithm::None, 0};
    }
    EXPECT_EQ(serialize(extreme).value(), legacyBytes(extreme).value());
}

TYPED_TEST(ManifestSerializationWorkTest, OrderedRecordsRequireNoRecordCopyOrSort) {
    const auto manifest = this->fixture();
    checkpoint::ManifestSaveStats stats;
    const auto result = serialize(manifest, &stats);
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), legacyBytes(manifest).value());
    EXPECT_EQ(stats.recordCopyItems, 0U);
    EXPECT_EQ(stats.recordSortCalls, 0U);
    EXPECT_EQ(stats.recordPreparationCalls, 1U);
}

TYPED_TEST(ManifestSerializationWorkTest, ShuffledRecordsAreCopiedAndSortedAtMostOnceWithoutMutation) {
    auto manifest = this->fixture(257);
    std::reverse(manifest.verifiedChunks.begin(), manifest.verifiedChunks.end());
    const auto original = manifest.verifiedChunks;
    checkpoint::ManifestSaveStats stats;
    const auto result = serialize(manifest, &stats);
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), legacyBytes(manifest).value());
    EXPECT_LE(stats.recordCopyItems, original.size());
    EXPECT_LE(stats.recordSortCalls, 1U);
    for (std::size_t index = 0; index < original.size(); ++index) {
        EXPECT_EQ(manifest.verifiedChunks[index].chunkId, original[index].chunkId);
        EXPECT_EQ(manifest.verifiedChunks[index].offset, original[index].offset);
    }
}

TYPED_TEST(ManifestSerializationWorkTest, OverlapZeroLengthAndOverflowRemainRejected) {
    auto manifest = this->fixture(2);
    for (auto offset : std::array<std::uint64_t, 3>{0, 2, UINT64_MAX}) {
        manifest.verifiedChunks[1].offset = offset;
        EXPECT_FALSE(serialize(manifest).isOk());
        EXPECT_FALSE(legacyBytes(manifest).isOk());
    }
    manifest.verifiedChunks[1].offset = 8;
    manifest.verifiedChunks[1].length = 0;
    EXPECT_FALSE(serialize(manifest).isOk());
    EXPECT_FALSE(legacyBytes(manifest).isOk());
}
}  // namespace
