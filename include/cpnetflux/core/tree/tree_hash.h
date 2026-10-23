#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"

namespace cpnetflux::core::tree {

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
