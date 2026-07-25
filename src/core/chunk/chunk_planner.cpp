#include "gridflux/core/chunk/chunk_planner.h"

#include <algorithm>
#include <limits>

namespace gridflux::core::chunk {
namespace {

constexpr std::uint32_t kMaxConnections = 64;

}  // namespace

common::Result<std::vector<ChunkRange>> planChunks(std::uint64_t fileSize, std::uint64_t chunkSize,
                                                   std::uint32_t connections) {
    if (chunkSize == 0) {
        return common::Status::invalidArgument("chunk size must be greater than zero");
    }
    if (connections == 0 || connections > kMaxConnections) {
        return common::Status::invalidArgument("connections must be in range 1..64");
    }

    std::vector<ChunkRange> chunks;
    if (fileSize == 0) {
        return chunks;
    }

    const std::uint64_t chunkCount = fileSize / chunkSize + (fileSize % chunkSize == 0 ? 0 : 1);
    if (chunkCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return common::Status::invalidArgument("chunk plan is too large for this platform");
    }
    chunks.reserve(static_cast<std::size_t>(chunkCount));

    std::uint64_t offset = 0;
    std::uint64_t chunkId = 0;
    while (offset < fileSize) {
        const std::uint64_t length = std::min(chunkSize, fileSize - offset);
        chunks.push_back(
            ChunkRange{chunkId, offset, length, static_cast<std::uint32_t>(chunkId % connections)});
        offset += length;
        chunkId += 1;
    }

    return chunks;
}

}  // namespace gridflux::core::chunk
