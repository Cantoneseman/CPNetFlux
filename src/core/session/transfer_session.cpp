#include "cpnetflux/core/session/transfer_session.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/checksum/checksum.h"

namespace cpnetflux::core::session {
namespace {

common::Status validateSessionInputs(const std::string& outputPath, const std::string& transferId,
                                     std::uint64_t chunkSize,
                                     std::uint64_t manifestFlushIntervalChunks) {
    if (outputPath.empty()) {
        return common::Status::invalidArgument("output path must not be empty");
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

common::Status validateLoadedManifest(const checkpoint::TransferManifest& manifest,
                                      const std::string& outputPath, const std::string& transferId,
                                      std::uint64_t totalSize, std::uint64_t chunkSize,
                                      checksum::ChecksumAlgorithm checksumAlgorithm) {
    if (manifest.transferId != transferId) {
        return common::Status::invalidArgument("manifest transfer_id does not match request");
    }
    if (manifest.outputPath != outputPath) {
        return common::Status::invalidArgument("manifest output path does not match request");
    }
    if (manifest.tempPath != checkpoint::tempPathForOutput(outputPath, transferId)) {
        return common::Status::invalidArgument("manifest temp path does not match transfer_id");
    }
    if (manifest.totalSize != totalSize) {
        return common::Status::invalidArgument("manifest total_size does not match request");
    }
    if (manifest.chunkSize != chunkSize) {
        return common::Status::invalidArgument("manifest chunk_size does not match request");
    }
    if (manifest.state == checkpoint::ManifestState::Committed) {
        return common::Status::invalidArgument("manifest is already committed");
    }
    if (manifest.version == checkpoint::kTransferManifestVersionV1 &&
        checksumAlgorithm != checksum::ChecksumAlgorithm::None) {
        return common::Status::invalidArgument("manifest version 1 lacks chunk checksum records");
    }
    if (manifest.version == checkpoint::kTransferManifestVersion &&
        manifest.checksumAlgorithm != checksumAlgorithm) {
        return common::Status::invalidArgument(
            "manifest checksum algorithm does not match request");
    }
    return common::Status::ok();
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

bool sameChecksum(const checksum::ChecksumValue& left,
                  const checksum::ChecksumValue& right) noexcept {
    return left.algorithm == right.algorithm && left.value == right.value;
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

}  // namespace

common::Result<TransferSession> TransferSession::createNew(
    const std::string& outputPath, const std::string& transferId, std::uint64_t totalSize,
    std::uint64_t chunkSize, checksum::ChecksumAlgorithm checksumAlgorithm,
    checksum::ChecksumBackend checksumBackend, ManifestFlushPolicy manifestFlushPolicy,
    std::uint64_t manifestFlushIntervalChunks) {
    const common::Status valid =
        validateSessionInputs(outputPath, transferId, chunkSize, manifestFlushIntervalChunks);
    if (!valid.isOk()) {
        return valid;
    }
    auto resolvedBackend = checksum::resolveChecksumBackend(checksumAlgorithm, checksumBackend);
    if (!resolvedBackend.isOk()) {
        return resolvedBackend.status();
    }

    TransferSession session;
    session.checksumBackend_ = resolvedBackend.value();
    session.manifestFlushPolicy_ = manifestFlushPolicy;
    session.manifestFlushIntervalChunks_ = manifestFlushIntervalChunks;
    session.manifestPath_ = checkpoint::manifestPathForOutput(outputPath);
    session.manifest_.version = checkpoint::kTransferManifestVersion;
    session.manifest_.transferId = transferId;
    session.manifest_.outputPath = outputPath;
    session.manifest_.tempPath = checkpoint::tempPathForOutput(outputPath, transferId);
    session.manifest_.totalSize = totalSize;
    session.manifest_.chunkSize = chunkSize;
    session.manifest_.checksumAlgorithm = checksumAlgorithm;
    session.manifest_.createdAtUnixNanos = checkpoint::nowUnixNanos();
    session.manifest_.updatedAtUnixNanos = session.manifest_.createdAtUnixNanos;
    session.manifest_.state = checkpoint::ManifestState::Transferring;
    session.stats_.missingChunks = totalSize == 0 ? 0 : 1;

    return session;
}

common::Result<TransferSession> TransferSession::resume(
    const std::string& outputPath, const std::string& transferId, std::uint64_t totalSize,
    std::uint64_t chunkSize, checksum::ChecksumAlgorithm checksumAlgorithm,
    checksum::ChecksumBackend checksumBackend, ManifestFlushPolicy manifestFlushPolicy,
    std::uint64_t manifestFlushIntervalChunks) {
    const common::Status valid =
        validateSessionInputs(outputPath, transferId, chunkSize, manifestFlushIntervalChunks);
    if (!valid.isOk()) {
        return valid;
    }
    auto resolvedBackend = checksum::resolveChecksumBackend(checksumAlgorithm, checksumBackend);
    if (!resolvedBackend.isOk()) {
        return resolvedBackend.status();
    }

    const std::string manifestPath = checkpoint::manifestPathForOutput(outputPath);
    auto loaded = checkpoint::ManifestStore::load(manifestPath);
    if (!loaded.isOk()) {
        return loaded.status();
    }

    const common::Status loadedValid = validateLoadedManifest(
        loaded.value(), outputPath, transferId, totalSize, chunkSize, checksumAlgorithm);
    if (!loadedValid.isOk()) {
        return loadedValid;
    }

    TransferSession session;
    session.checksumBackend_ = resolvedBackend.value();
    session.manifestFlushPolicy_ = manifestFlushPolicy;
    session.manifestFlushIntervalChunks_ = manifestFlushIntervalChunks;
    session.manifestPath_ = manifestPath;
    session.manifest_ = std::move(loaded.value());
    session.manifest_.checksumAlgorithm = checksumAlgorithm;

    if (session.manifest_.version == checkpoint::kTransferManifestVersionV1) {
        for (const chunk::CompletedRange& range : session.manifest_.completedRanges) {
            const std::uint64_t length = range.end - range.begin;
            const common::Status insertStatus =
                session.completed_.insert(range.begin, length, totalSize);
            if (!insertStatus.isOk()) {
                return insertStatus;
            }
            session.manifest_.verifiedChunks.push_back(checkpoint::ChunkChecksumRecord{
                range.begin,
                range.begin,
                length,
                checksum::ChecksumValue{checksum::ChecksumAlgorithm::None, 0},
            });
        }
    } else {
        for (const checkpoint::ChunkChecksumRecord& record : session.manifest_.verifiedChunks) {
            const common::Status insertStatus =
                session.completed_.insert(record.offset, record.length, totalSize);
            if (!insertStatus.isOk()) {
                return insertStatus;
            }
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

common::Status TransferSession::save() {
    manifest_.updatedAtUnixNanos = checkpoint::nowUnixNanos();
    manifest_.version = checkpoint::kTransferManifestVersion;
    manifest_.completedRanges = completed_.ranges();
    sortVerifiedChunks(&manifest_.verifiedChunks);
    metrics::ScopedPhaseTimer timer(phaseStats_, metrics::TransferPhase::ManifestFlush);
    const common::Status status = checkpoint::ManifestStore::saveAtomic(manifestPath_, manifest_);
    timer.stop();
    if (status.isOk()) {
        dirtyVerifiedChunks_ = 0;
        stats_.manifestFlushCount += 1;
    }
    return status;
}

common::Status TransferSession::flushManifest() { return save(); }

common::Status TransferSession::recordCompletedRange(std::uint64_t offset, std::uint64_t length) {
    return recordVerifiedChunk(offset, offset, length,
                               checksum::ChecksumValue{checksum::ChecksumAlgorithm::None, 0});
}

common::Status TransferSession::recordVerifiedChunk(std::uint64_t chunkId, std::uint64_t offset,
                                                    std::uint64_t length,
                                                    checksum::ChecksumValue checksumValue) {
    if (checksumValue.algorithm != manifest_.checksumAlgorithm) {
        return common::Status::invalidArgument("chunk checksum algorithm does not match manifest");
    }
    if (length == 0 || offset > manifest_.totalSize || length > manifest_.totalSize - offset) {
        return common::Status::invalidArgument("chunk range exceeds transfer size");
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
        return common::Status::invalidArgument("verified chunk checksum mismatch");
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
    stats_.verifiedBytes = completed_.bytesCompleted();
    stats_.missingChunks = completed_.missingRanges(manifest_.totalSize).size();
    if (manifestFlushPolicy_ == ManifestFlushPolicy::EveryNChunks &&
        dirtyVerifiedChunks_ >= manifestFlushIntervalChunks_) {
        return flushManifest();
    }
    return common::Status::ok();
}

common::Status TransferSession::verifyTempChunks(const storage::PosixFile& tempFile) {
    if (manifest_.checksumAlgorithm == checksum::ChecksumAlgorithm::None ||
        manifest_.verifiedChunks.empty()) {
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

common::Status TransferSession::markFailed() {
    manifest_.state = checkpoint::ManifestState::Failed;
    return save();
}

common::Status TransferSession::markCommitted() {
    manifest_.state = checkpoint::ManifestState::Committed;
    return save();
}

std::vector<chunk::CompletedRange> TransferSession::missingChunks() const {
    return completed_.missingRanges(manifest_.totalSize);
}

std::vector<chunk::CompletedRange> TransferSession::missingRanges() const {
    return missingChunks();
}

std::uint64_t TransferSession::bytesCompleted() const noexcept {
    return completed_.bytesCompleted();
}

const TransferSessionStats& TransferSession::stats() const noexcept { return stats_; }

checksum::ChecksumBackend TransferSession::checksumBackend() const noexcept {
    return checksumBackend_;
}

ManifestFlushPolicy TransferSession::manifestFlushPolicy() const noexcept {
    return manifestFlushPolicy_;
}

std::uint64_t TransferSession::manifestFlushIntervalChunks() const noexcept {
    return manifestFlushIntervalChunks_;
}

const checkpoint::TransferManifest& TransferSession::manifest() const noexcept { return manifest_; }

const std::string& TransferSession::manifestPath() const noexcept { return manifestPath_; }

void TransferSession::setPhaseStats(metrics::TransferPhaseStats* phaseStats) noexcept {
    phaseStats_ = phaseStats;
}

}  // namespace cpnetflux::core::session
