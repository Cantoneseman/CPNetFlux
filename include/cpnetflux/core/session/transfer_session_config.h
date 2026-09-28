#pragma once

#include <cstdint>
#include <string>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/core/session/manifest_flush_policy.h"
#include "cpnetflux/storage/file_io.h"

namespace cpnetflux::core::session {

inline constexpr std::uint64_t kDefaultManifestFlushIntervalChunks = 16;

struct TransferSessionConfig {
    std::string transferId;
    std::uint64_t totalSize = 0;
    std::uint64_t chunkSize = 0;
    std::uint32_t connections = 1;
    bool resume = false;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto;
    ManifestFlushPolicy manifestFlushPolicy = ManifestFlushPolicy::EveryNChunks;
    std::uint64_t manifestFlushIntervalChunks = kDefaultManifestFlushIntervalChunks;
    storage::FileIoConfig fileIo;
};

}  // namespace cpnetflux::core::session
