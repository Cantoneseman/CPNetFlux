#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/chunk/range_list.h"

namespace cpnetflux::checkpoint {

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

}  // namespace cpnetflux::checkpoint
