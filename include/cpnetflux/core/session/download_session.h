#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpnetflux/checkpoint/download_manifest.h"
#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/chunk/range_list.h"
#include "cpnetflux/core/metrics/transfer_phase_stats.h"
#include "cpnetflux/core/session/manifest_flush_policy.h"
#include "cpnetflux/core/session/transfer_session_config.h"
#include "cpnetflux/storage/posix_file.h"

namespace cpnetflux::core::session {

struct DownloadSessionStats {
    std::uint64_t loadedVerifiedChunks = 0;
    std::uint64_t removedCorruptChunks = 0;
    std::uint64_t missingChunks = 0;
    std::uint64_t skippedBytes = 0;
    std::uint64_t resentBytes = 0;
    std::uint64_t verifiedBytes = 0;
    std::uint64_t manifestFlushCount = 0;
};

class DownloadSession {
   public:
    DownloadSession() = default;

    [[nodiscard]] static common::Result<DownloadSession> createNew(
        const std::string& targetPath, const std::string& sourcePath, const std::string& transferId,
        std::uint64_t totalSize, std::uint64_t chunkSize,
        checksum::ChecksumAlgorithm checksumAlgorithm,
        checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto,
        ManifestFlushPolicy manifestFlushPolicy = ManifestFlushPolicy::EveryNChunks,
        std::uint64_t manifestFlushIntervalChunks = kDefaultManifestFlushIntervalChunks);
    [[nodiscard]] static common::Result<DownloadSession> resume(
        const std::string& targetPath, const std::string& sourcePath, const std::string& transferId,
        std::uint64_t totalSize, std::uint64_t chunkSize,
        checksum::ChecksumAlgorithm checksumAlgorithm,
        checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto,
        ManifestFlushPolicy manifestFlushPolicy = ManifestFlushPolicy::EveryNChunks,
        std::uint64_t manifestFlushIntervalChunks = kDefaultManifestFlushIntervalChunks);

    [[nodiscard]] common::Status save();
    [[nodiscard]] common::Status flushManifest();
    [[nodiscard]] common::Status recordVerifiedChunk(std::uint64_t chunkId, std::uint64_t offset,
                                                     std::uint64_t length,
                                                     checksum::ChecksumValue checksumValue);
    [[nodiscard]] common::Status verifyTempChunks(const storage::PosixFile& tempFile);
    [[nodiscard]] common::Status markFailed();
    [[nodiscard]] common::Status markCommitted();

    [[nodiscard]] std::vector<chunk::CompletedRange> missingRanges() const;
    [[nodiscard]] std::uint64_t bytesCompleted() const noexcept;
    [[nodiscard]] const DownloadSessionStats& stats() const noexcept;
    [[nodiscard]] checksum::ChecksumBackend checksumBackend() const noexcept;
    [[nodiscard]] ManifestFlushPolicy manifestFlushPolicy() const noexcept;
    [[nodiscard]] std::uint64_t manifestFlushIntervalChunks() const noexcept;
    [[nodiscard]] const checkpoint::DownloadManifest& manifest() const noexcept;
    [[nodiscard]] const std::string& manifestPath() const noexcept;
    void setPhaseStats(metrics::TransferPhaseStats* phaseStats) noexcept;

   private:
    checkpoint::DownloadManifest manifest_;
    chunk::RangeList completed_;
    std::string manifestPath_;
    checksum::ChecksumBackend checksumBackend_ = checksum::ChecksumBackend::Software;
    ManifestFlushPolicy manifestFlushPolicy_ = ManifestFlushPolicy::EveryNChunks;
    std::uint64_t manifestFlushIntervalChunks_ = kDefaultManifestFlushIntervalChunks;
    std::uint64_t dirtyVerifiedChunks_ = 0;
    DownloadSessionStats stats_;
    metrics::TransferPhaseStats* phaseStats_ = nullptr;
};

}  // namespace cpnetflux::core::session
