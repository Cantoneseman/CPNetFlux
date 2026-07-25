#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gridflux/checksum/checksum.h"
#include "gridflux/common/status.h"

namespace gridflux::core::tree {

inline constexpr std::uint32_t kTreeManifestVersion = 1;

enum class TreeTransferMode {
    Upload,
    Download,
};

enum class TreeFileStatus {
    Pending,
    Transferring,
    Completed,
    Failed,
    Changed,
};

struct TreeFileRecord {
    std::string relativePath;
    std::uint64_t size = 0;
    std::int64_t mtimeUnixSeconds = 0;
    std::string transferId;
    TreeFileStatus status = TreeFileStatus::Pending;
    std::string error;
};

struct TreeManifest {
    std::uint32_t version = kTreeManifestVersion;
    TreeTransferMode mode = TreeTransferMode::Upload;
    std::string rootLogicalPath;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    std::uint64_t createdAtUnixNanos = 0;
    std::uint64_t updatedAtUnixNanos = 0;
    std::vector<TreeFileRecord> files;
};

[[nodiscard]] std::string treeManifestPathForUpload(const std::string& sourceDir);
[[nodiscard]] std::string treeManifestPathForDownload(const std::string& destDir);
[[nodiscard]] const char* treeTransferModeName(TreeTransferMode mode) noexcept;
[[nodiscard]] const char* treeFileStatusName(TreeFileStatus status) noexcept;
[[nodiscard]] common::Result<TreeTransferMode> parseTreeTransferMode(const std::string& text);
[[nodiscard]] common::Result<TreeFileStatus> parseTreeFileStatus(const std::string& text);
[[nodiscard]] bool isTreeTransferComplete(const TreeManifest& manifest);

[[nodiscard]] common::Result<std::string> serializeTreeManifest(const TreeManifest& manifest);
[[nodiscard]] common::Result<TreeManifest> parseTreeManifest(const std::string& text);
[[nodiscard]] common::Status saveTreeManifestAtomic(const std::string& path,
                                                    const TreeManifest& manifest);
[[nodiscard]] common::Result<TreeManifest> loadTreeManifest(const std::string& path);

}  // namespace gridflux::core::tree
