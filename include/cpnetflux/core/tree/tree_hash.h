#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"

namespace cpnetflux::core::tree {

enum class TreeHashPathMode { Original, Cached };

[[nodiscard]] common::Result<TreeHashPathMode> parseTreeHashPathMode(std::string_view value);
[[nodiscard]] const char* treeHashPathModeName(TreeHashPathMode mode) noexcept;

// Reads the canonical path/size/content CRC. Cached changes path resolution only.
[[nodiscard]] common::Result<std::string> treeHashFromFiles(
    const std::string& root, TreeHashPathMode mode = TreeHashPathMode::Original,
    checksum::ChecksumBackend backend = checksum::ChecksumBackend::Auto,
    checksum::ChecksumBackend* actualBackend = nullptr);

struct TreeHashChunkCrc {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::uint32_t crc32c = 0;
};

struct TreeHashFileCrc {
    std::string relativePath;
    std::uint64_t size = 0;
    std::vector<TreeHashChunkCrc> chunks;
};

// Builds the existing path/size/content tree CRC without rereading file contents.
// Every nonempty file must have an exact, gap-free, nonoverlapping chunk cover.
[[nodiscard]] common::Result<std::string> treeHashFromVerifiedChunks(
    std::vector<TreeHashFileCrc> files,
    checksum::ChecksumBackend prefixBackend = checksum::ChecksumBackend::Auto,
    checksum::ChecksumBackend* actualBackend = nullptr);

}  // namespace cpnetflux::core::tree
