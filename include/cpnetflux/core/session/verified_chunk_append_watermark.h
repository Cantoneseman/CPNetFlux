#pragma once

#include <algorithm>
#include <cstdint>

#include "cpnetflux/checkpoint/transfer_manifest.h"

namespace cpnetflux::core::session {

// Monotonic new identities cannot match any prior offset or checked chunk ID.
// Maxima remain valid when save() sorts records; irregular inserts use the full check.
class VerifiedChunkAppendWatermark {
 public:
    bool canSkipLookup(std::uint64_t chunkId, std::uint64_t offset,
                       checksum::ChecksumAlgorithm algorithm, bool empty) const noexcept {
        return empty || (offset > maxOffset_ &&
            (algorithm == checksum::ChecksumAlgorithm::None || chunkId > maxChunkId_));
    }
    void observe(const checkpoint::ChunkChecksumRecord& record) noexcept {
        maxOffset_ = std::max(maxOffset_, record.offset);
        maxChunkId_ = std::max(maxChunkId_, record.chunkId);
    }
    void rebuild(const std::vector<checkpoint::ChunkChecksumRecord>& records) noexcept {
        maxOffset_ = maxChunkId_ = 0;
        for (const auto& record : records) observe(record);
    }
 private:
    std::uint64_t maxOffset_ = 0, maxChunkId_ = 0;
};

}  // namespace cpnetflux::core::session
