#pragma once

#include <cstdint>
#include <vector>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::chunk {

struct ChunkRange {
    std::uint64_t chunkId = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::uint32_t streamId = 0;
};

common::Result<std::vector<ChunkRange>> planChunks(std::uint64_t fileSize, std::uint64_t chunkSize,
                                                   std::uint32_t connections);

}  // namespace cpnetflux::core::chunk
