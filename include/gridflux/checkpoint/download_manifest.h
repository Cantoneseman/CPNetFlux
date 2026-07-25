#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gridflux/checkpoint/transfer_manifest.h"
#include "gridflux/checksum/checksum.h"
#include "gridflux/common/status.h"
#include "gridflux/core/chunk/range_list.h"

namespace gridflux::checkpoint {

inline constexpr std::uint32_t kDownloadManifestVersion = 1;

struct DownloadManifest {
    std::uint32_t version = kDownloadManifestVersion;
    std::string transferId;
    std::string sourcePath;
    std::string targetPath;
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

[[nodiscard]] std::string downloadManifestPathForOutput(const std::string& outputPath);
[[nodiscard]] std::string downloadTempPathForOutput(const std::string& outputPath,
                                                    const std::string& transferId);

[[nodiscard]] common::Result<std::string> serializeDownloadManifest(
    const DownloadManifest& manifest);
[[nodiscard]] common::Result<DownloadManifest> parseDownloadManifest(const std::string& text);
[[nodiscard]] common::Status saveDownloadManifestAtomic(const std::string& path,
                                                        const DownloadManifest& manifest);
[[nodiscard]] common::Result<DownloadManifest> loadDownloadManifest(const std::string& path);

}  // namespace gridflux::checkpoint
