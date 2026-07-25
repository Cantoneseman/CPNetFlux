#include "gridflux/core/session/download_session.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace gridflux::core::session {
namespace {

common::Status validateInputs(const std::string& targetPath, const std::string& sourcePath,
                              const std::string& transferId, std::uint64_t chunkSize,
                              std::uint64_t manifestFlushIntervalChunks) {
    if (targetPath.empty()) {
        return common::Status::invalidArgument("target path must not be empty");
    }
    if (sourcePath.empty()) {
        return common::Status::invalidArgument("source path must not be empty");
    }
    if (!checkpoint::isValidTransferId(transferId)) {
        return common::Status::invalidArgument("invalid transfer_id");
    }
    if (chunkSize == 0) {
        return common::Status::invalidArgument("chunk_size must be greater than zero");
    }
    if (manifestFlushIntervalChunks == 0) {
        return common::Status::invalidArgument("manifest flush interval must be greater than zero");
    }
    return common::Status::ok();
}

bool sameChecksum(const checksum::ChecksumValue& left,
                  const checksum::ChecksumValue& right) noexcept {
    return left.algorithm == right.algorithm && left.value == right.value;
}

void sortVerifiedChunks(std::vector<checkpoint::ChunkChecksumRecord>* records) {
    std::sort(records->begin(), records->end(),
              [](const checkpoint::ChunkChecksumRecord& left,
                 const checkpoint::ChunkChecksumRecord& right) {
                  if (left.offset != right.offset) {
                      return left.offset < right.offset;
                  }
                  return left.chunkId < right.chunkId;
              });
}

common::Status readChecksumForRecord(const storage::PosixFile& file,
                                     const checkpoint::ChunkChecksumRecord& record,
                                     checksum::ChecksumBackend checksumBackend,
                                     checksum::ChecksumValue* value) {
    checksum::ChecksumComputer computer(record.checksum.algorithm, checksumBackend);
    std::vector<std::uint8_t> buffer(65536);
    std::uint64_t completed = 0;
    while (completed < record.length) {
        const std::size_t size = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(), record.length - completed));
        const common::Status readStatus =
            file.readAtAll(record.offset + completed, buffer.data(), size);
        if (!readStatus.isOk()) {
            return readStatus;
        }
        computer.update(buffer.data(), size);
        completed += size;
    }
    *value = computer.finalize();
    return common::Status::ok();
}

common::Status validateLoadedManifest(const checkpoint::DownloadManifest& manifest,
                                      const std::string& targetPath, const std::string& sourcePath,
                                      const std::string& transferId, std::uint64_t totalSize,
                                      std::uint64_t chunkSize,
                                      checksum::ChecksumAlgorithm checksumAlgorithm) {
    if (manifest.transferId != transferId) {
        return common::Status::invalidArgument("download manifest transfer_id mismatch");
    }
    if (manifest.targetPath != targetPath) {
        return common::Status::invalidArgument("download manifest target path mismatch");
    }
    if (manifest.sourcePath != sourcePath) {
        return common::Status::invalidArgument("download manifest source path mismatch");
    }
    if (manifest.tempPath != checkpoint::downloadTempPathForOutput(targetPath, transferId)) {
        return common::Status::invalidArgument("download manifest temp path mismatch");
    }
    if (manifest.totalSize != totalSize) {
        return common::Status::invalidArgument("download manifest total_size mismatch");
    }
    if (manifest.chunkSize != chunkSize) {
        return common::Status::invalidArgument("download manifest chunk_size mismatch");
    }
    if (manifest.checksumAlgorithm != checksumAlgorithm) {
        return common::Status::invalidArgument("download manifest checksum algorithm mismatch");
    }
    if (manifest.state == checkpoint::ManifestState::Committed) {
        return common::Status::invalidArgument("download manifest is already committed");
    }
    return common::Status::ok();
}

}  // namespace

common::Result<DownloadSession> DownloadSession::createNew(
    const std::string& targetPath, const std::string& sourcePath, const std::string& transferId,
    std::uint64_t totalSize, std::uint64_t chunkSize, checksum::ChecksumAlgorithm checksumAlgorithm,
    checksum::ChecksumBackend checksumBackend, ManifestFlushPolicy manifestFlushPolicy,
    std::uint64_t manifestFlushIntervalChunks) {
    const common::Status valid =
        validateInputs(targetPath, sourcePath, transferId, chunkSize, manifestFlushIntervalChunks);
    if (!valid.isOk()) {
        return valid;
    }
    auto resolvedBackend = checksum::resolveChecksumBackend(checksumAlgorithm, checksumBackend);
    if (!resolvedBackend.isOk()) {
        return resolvedBackend.status();
    }

    DownloadSession session;
    session.checksumBackend_ = resolvedBackend.value();
    session.manifestFlushPolicy_ = manifestFlushPolicy;
    session.manifestFlushIntervalChunks_ = manifestFlushIntervalChunks;
    session.manifestPath_ = checkpoint::downloadManifestPathForOutput(targetPath);
    session.manifest_.version = checkpoint::kDownloadManifestVersion;
    session.manifest_.transferId = transferId;
    session.manifest_.sourcePath = sourcePath;
    session.manifest_.targetPath = targetPath;
    session.manifest_.tempPath = checkpoint::downloadTempPathForOutput(targetPath, transferId);
    session.manifest_.totalSize = totalSize;
    session.manifest_.chunkSize = chunkSize;
    session.manifest_.checksumAlgorithm = checksumAlgorithm;
    session.manifest_.createdAtUnixNanos = checkpoint::nowUnixNanos();
    session.manifest_.updatedAtUnixNanos = session.manifest_.createdAtUnixNanos;
    session.manifest_.state = checkpoint::ManifestState::Transferring;
    session.stats_.missingChunks = session.completed_.missingRanges(totalSize).size();
    return session;
}

common::Result<DownloadSession> DownloadSession::resume(
    const std::string& targetPath, const std::string& sourcePath, const std::string& transferId,
    std::uint64_t totalSize, std::uint64_t chunkSize, checksum::ChecksumAlgorithm checksumAlgorithm,
    checksum::ChecksumBackend checksumBackend, ManifestFlushPolicy manifestFlushPolicy,
    std::uint64_t manifestFlushIntervalChunks) {
    const common::Status valid =
        validateInputs(targetPath, sourcePath, transferId, chunkSize, manifestFlushIntervalChunks);
    if (!valid.isOk()) {
        return valid;
    }
    auto resolvedBackend = checksum::resolveChecksumBackend(checksumAlgorithm, checksumBackend);
    if (!resolvedBackend.isOk()) {
        return resolvedBackend.status();
    }

    const std::string manifestPath = checkpoint::downloadManifestPathForOutput(targetPath);
    auto loaded = checkpoint::loadDownloadManifest(manifestPath);
    if (!loaded.isOk()) {
        return loaded.status();
    }
    const common::Status loadedValid =
        validateLoadedManifest(loaded.value(), targetPath, sourcePath, transferId, totalSize,
                               chunkSize, checksumAlgorithm);
    if (!loadedValid.isOk()) {
        return loadedValid;
    }

    DownloadSession session;
    session.checksumBackend_ = resolvedBackend.value();
    session.manifestFlushPolicy_ = manifestFlushPolicy;
    session.manifestFlushIntervalChunks_ = manifestFlushIntervalChunks;
    session.manifestPath_ = manifestPath;
    session.manifest_ = std::move(loaded.value());
    for (const checkpoint::ChunkChecksumRecord& record : session.manifest_.verifiedChunks) {
        const common::Status insertStatus =
            session.completed_.insert(record.offset, record.length, totalSize);
        if (!insertStatus.isOk()) {
            return insertStatus;
        }
    }
    session.manifest_.state = checkpoint::ManifestState::Transferring;
    session.manifest_.updatedAtUnixNanos = checkpoint::nowUnixNanos();
    session.stats_.loadedVerifiedChunks = session.manifest_.verifiedChunks.size();
    session.stats_.verifiedBytes = session.completed_.bytesCompleted();
    session.stats_.missingChunks = session.completed_.missingRanges(totalSize).size();
    session.stats_.skippedBytes = session.stats_.verifiedBytes;
    return session;
}

common::Status DownloadSession::save() {
    manifest_.updatedAtUnixNanos = checkpoint::nowUnixNanos();
    manifest_.completedRanges = completed_.ranges();
    sortVerifiedChunks(&manifest_.verifiedChunks);
    metrics::ScopedPhaseTimer timer(phaseStats_, metrics::TransferPhase::ManifestFlush);
    const common::Status status = checkpoint::saveDownloadManifestAtomic(manifestPath_, manifest_);
    timer.stop();
    if (status.isOk()) {
        dirtyVerifiedChunks_ = 0;
        stats_.manifestFlushCount += 1;
    }
    return status;
}

common::Status DownloadSession::flushManifest() { return save(); }

common::Status DownloadSession::recordVerifiedChunk(std::uint64_t chunkId, std::uint64_t offset,
                                                    std::uint64_t length,
                                                    checksum::ChecksumValue checksumValue) {
    if (checksumValue.algorithm != manifest_.checksumAlgorithm) {
        return common::Status::invalidArgument(
            "download chunk checksum algorithm does not match manifest");
    }
    if (length == 0 || offset > manifest_.totalSize || length > manifest_.totalSize - offset) {
        return common::Status::invalidArgument("download chunk range exceeds transfer size");
    }

    for (const checkpoint::ChunkChecksumRecord& record : manifest_.verifiedChunks) {
        const bool sameIdentity =
            record.offset == offset ||
            (manifest_.checksumAlgorithm != checksum::ChecksumAlgorithm::None &&
             record.chunkId == chunkId);
        if (!sameIdentity) {
            continue;
        }
        if (record.chunkId == chunkId && record.offset == offset && record.length == length &&
            sameChecksum(record.checksum, checksumValue)) {
            return common::Status::ok();
        }
        return common::Status::invalidArgument("download verified chunk checksum mismatch");
    }

    const common::Status insertStatus = completed_.insert(offset, length, manifest_.totalSize);
    if (!insertStatus.isOk()) {
        return insertStatus;
    }
    manifest_.verifiedChunks.push_back(
        checkpoint::ChunkChecksumRecord{chunkId, offset, length, checksumValue});
    manifest_.completedRanges = completed_.ranges();
    manifest_.state = checkpoint::ManifestState::Transferring;
    dirtyVerifiedChunks_ += 1;
    stats_.resentBytes += length;
    stats_.verifiedBytes = completed_.bytesCompleted();
    stats_.missingChunks = completed_.missingRanges(manifest_.totalSize).size();
    if (manifestFlushPolicy_ == ManifestFlushPolicy::EveryNChunks &&
        dirtyVerifiedChunks_ >= manifestFlushIntervalChunks_) {
        return flushManifest();
    }
    return common::Status::ok();
}

common::Status DownloadSession::verifyTempChunks(const storage::PosixFile& tempFile) {
    if (manifest_.checksumAlgorithm == checksum::ChecksumAlgorithm::None ||
        manifest_.verifiedChunks.empty()) {
        stats_.verifiedBytes = completed_.bytesCompleted();
        stats_.missingChunks = completed_.missingRanges(manifest_.totalSize).size();
        return common::Status::ok();
    }

    std::vector<checkpoint::ChunkChecksumRecord> kept;
    chunk::RangeList verified;
    bool changed = false;
    for (const checkpoint::ChunkChecksumRecord& record : manifest_.verifiedChunks) {
        checksum::ChecksumValue actual;
        const common::Status readStatus =
            readChecksumForRecord(tempFile, record, checksumBackend_, &actual);
        if (!readStatus.isOk() || !sameChecksum(actual, record.checksum)) {
            changed = true;
            stats_.removedCorruptChunks += 1;
            continue;
        }
        const common::Status insertStatus =
            verified.insert(record.offset, record.length, manifest_.totalSize);
        if (!insertStatus.isOk()) {
            return insertStatus;
        }
        kept.push_back(record);
    }

    if (!changed) {
        stats_.verifiedBytes = completed_.bytesCompleted();
        stats_.missingChunks = completed_.missingRanges(manifest_.totalSize).size();
        return common::Status::ok();
    }

    manifest_.verifiedChunks = std::move(kept);
    completed_ = std::move(verified);
    manifest_.completedRanges = completed_.ranges();
    manifest_.state = checkpoint::ManifestState::Transferring;
    stats_.verifiedBytes = completed_.bytesCompleted();
    stats_.missingChunks = completed_.missingRanges(manifest_.totalSize).size();
    stats_.skippedBytes = stats_.verifiedBytes;
    return save();
}

common::Status DownloadSession::markFailed() {
    manifest_.state = checkpoint::ManifestState::Failed;
    return save();
}

common::Status DownloadSession::markCommitted() {
    manifest_.state = checkpoint::ManifestState::Committed;
    return save();
}

std::vector<chunk::CompletedRange> DownloadSession::missingRanges() const {
    return completed_.missingRanges(manifest_.totalSize);
}

std::uint64_t DownloadSession::bytesCompleted() const noexcept {
    return completed_.bytesCompleted();
}

const DownloadSessionStats& DownloadSession::stats() const noexcept { return stats_; }

checksum::ChecksumBackend DownloadSession::checksumBackend() const noexcept {
    return checksumBackend_;
}

ManifestFlushPolicy DownloadSession::manifestFlushPolicy() const noexcept {
    return manifestFlushPolicy_;
}

std::uint64_t DownloadSession::manifestFlushIntervalChunks() const noexcept {
    return manifestFlushIntervalChunks_;
}

const checkpoint::DownloadManifest& DownloadSession::manifest() const noexcept { return manifest_; }

const std::string& DownloadSession::manifestPath() const noexcept { return manifestPath_; }

void DownloadSession::setPhaseStats(metrics::TransferPhaseStats* phaseStats) noexcept {
    phaseStats_ = phaseStats;
}

}  // namespace gridflux::core::session
