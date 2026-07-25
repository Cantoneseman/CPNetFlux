#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gridflux/checksum/checksum.h"
#include "gridflux/common/status.h"
#include "gridflux/core/chunk/range_list.h"

namespace gridflux::checkpoint {

inline constexpr std::uint32_t kTransferManifestVersionV1 = 1;
inline constexpr std::uint32_t kTransferManifestVersion = 2;

enum class ManifestState {
    Created,
    Transferring,
    Failed,
    Committed,
};

struct ChunkChecksumRecord {
    std::uint64_t chunkId = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    checksum::ChecksumValue checksum;
};

struct TransferManifest {
    std::uint32_t version = kTransferManifestVersion;
    std::string transferId;
    std::string outputPath;
    std::string tempPath;
    std::uint64_t totalSize = 0;
    std::uint64_t chunkSize = 0;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    std::uint64_t createdAtUnixNanos = 0;
    std::uint64_t updatedAtUnixNanos = 0;
    ManifestState state = ManifestState::Created;
    std::vector<core::chunk::CompletedRange> completedRanges;
    std::vector<ChunkChecksumRecord> verifiedChunks;
};

[[nodiscard]] bool isValidTransferId(const std::string& transferId) noexcept;
[[nodiscard]] std::string manifestPathForOutput(const std::string& outputPath);
[[nodiscard]] std::string tempPathForOutput(const std::string& outputPath,
                                            const std::string& transferId);
[[nodiscard]] std::uint64_t nowUnixNanos() noexcept;

[[nodiscard]] common::Result<std::string> serializeTransferManifest(
    const TransferManifest& manifest);
[[nodiscard]] common::Result<TransferManifest> parseTransferManifest(const std::string& text);

[[nodiscard]] const char* manifestStateName(ManifestState state) noexcept;
[[nodiscard]] common::Result<ManifestState> parseManifestState(const std::string& text);

}  // namespace gridflux::checkpoint
