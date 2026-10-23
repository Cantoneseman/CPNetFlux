#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/core/io/payload_pipeline.h"
#include <cstdlib>
#include <ctime>
#include <string_view>
#include "cpnetflux/core/io/work_executor.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <variant>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <ostream>
#include <iomanip>
#include <functional>
#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/checkpoint/download_manifest.h"
#include "cpnetflux/core/session/transfer_session.h"
#include "cpnetflux/core/session/download_session.h"
#include "cpnetflux/core/tree/tree_scan.h"
#include "cpnetflux/core/chunk/range_planner.h"
#include "cpnetflux/storage/posix_file.h"
#include "cpnetflux/core/protocol/compressed_data.h"

namespace cpnetflux::core::io {
// One worker per transfer context; never hold the queue lock across a file lock.
// Callbacks own weak file references, so shutdown joins before files are destroyed.
class RangeCheckpointQueue {
    using Clock = std::chrono::steady_clock;
    struct Job { Clock::time_point deadline; std::function<void()> run; };
    std::mutex mutex_;
    std::condition_variable changed_;
    std::unordered_map<const void*, Job> jobs_;
    bool stopping_ = false;
    std::thread worker_{[this] {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            if (jobs_.empty()) { changed_.wait(lock); continue; }
            auto next = std::min_element(jobs_.begin(), jobs_.end(),
                [](const auto& a, const auto& b) { return a.second.deadline < b.second.deadline; });
            if (Clock::now() < next->second.deadline) {
                const auto deadline = next->second.deadline;
                changed_.wait_until(lock, deadline);
                continue;
            }
            auto run = std::move(next->second.run);
            jobs_.erase(next);
            lock.unlock();
            run();
            lock.lock();
        }
    }};
 public:
    void schedule(const void* key, Clock::time_point deadline, std::function<void()> run) {
        std::lock_guard lock(mutex_);
        jobs_.insert_or_assign(key, Job{deadline, std::move(run)});
        changed_.notify_one();
    }
    void cancel(const void* key) {
        std::lock_guard lock(mutex_);
        jobs_.erase(key);
        changed_.notify_one();
    }
    ~RangeCheckpointQueue() {
        { std::lock_guard lock(mutex_); stopping_ = true; changed_.notify_one(); }
        worker_.join();
    }
};
struct PersistentTransferContext {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<void>> files;
    std::vector<std::function<common::Status()>> integrityChecks;
    PersistentTransferPolicy policy;
    std::shared_ptr<RangeCheckpointQueue> checkpoints; // destroyed/joined before files
};
std::shared_ptr<PersistentTransferContext> makePersistentTransferContext(const PersistentTransferPolicy& policy) {
    auto context = std::make_shared<PersistentTransferContext>();
    context->policy = policy;
    return context;
}
namespace {
using common::Status;
using protocol::FrameStatusCode;
using protocol::FrameType;
namespace fs = std::filesystem;
using Session = std::variant<session::TransferSession, session::DownloadSession>;
using Clock = std::chrono::steady_clock;

FrameStatusCode fileFailureCode(const Status& status) {
    if (status.isOk()) return FrameStatusCode::Ok;
    if (status.message().find("output already exists") != std::string::npos)
        return FrameStatusCode::OutputExists;
    return FrameStatusCode::WriteFailed;
}

Status receiverFileStatus(FrameStatusCode code, const std::string& relativePath) {
    if (code == FrameStatusCode::Ok) return Status::ok();
    if (code == FrameStatusCode::OutputExists)
        return Status::invalidArgument("persistent output already exists: " + relativePath);
    return Status::runtimeError("persistent receiver rejected file: " + relativePath);
}
double secondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }
struct Timer {
    double* value; Clock::time_point start = Clock::now();
    ~Timer() { if (value) *value += secondsSince(start); }
};

bool observeRanges(const PersistentTreeStats& stats) {
    const auto* setting = std::getenv("CPNETFLUX_RANGE_TIMING");
    return stats.phaseTiming && (!setting || std::string_view(setting) != "off");
}
common::Result<std::size_t> diagnosticPipelineDepth() {
    const auto* setting = std::getenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH");
    if (!setting) return std::size_t{2};
    const std::string_view text(setting);
    if (text == "2") return std::size_t{2};
    if (text == "4") return std::size_t{4};
    if (text == "8") return std::size_t{8};
    if (text == "16") return std::size_t{16};
    return Status::invalidArgument("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH must be 2, 4, 8 or 16");
}
common::Result<std::uint32_t> rangeCheckpointBatch(const PersistentTransferPolicy& policy) {
    if (policy.rangeCheckpointBatchChunks) {
        const auto n = policy.rangeCheckpointBatchChunks;
        if (n == 1 || n == 4 || n == 16) return n;
        return Status::invalidArgument("range checkpoint batch must be 1, 4 or 16");
    }
    const auto* setting = std::getenv("CPNETFLUX_RANGE_CHECKPOINT_BATCH");
    if (!setting || std::string_view(setting) == "1") return std::uint32_t{1};
    if (std::string_view(setting) == "4") return std::uint32_t{4};
    if (std::string_view(setting) == "16") return std::uint32_t{16};
    return Status::invalidArgument("CPNETFLUX_RANGE_CHECKPOINT_BATCH must be 1, 4 or 16");
}
double threadCpuSeconds(bool enabled) {
    if (!enabled) return 0;
    timespec now{};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) return 0;
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec)*1e-9;
}
class ObservedEntryLock {
 public:
    ObservedEntryLock(std::mutex& mutex, RangeObservation* observation)
        : observation_(observation), lock_(mutex, std::defer_lock) {
        const auto started = observation_ ? Clock::now() : Clock::time_point{};
        lock_.lock();
        if (observation_) {
            acquired_ = Clock::now();
            ++observation_->entryLockCalls;
            observation_->entryLockWaitSeconds += std::chrono::duration<double>(acquired_-started).count();
        }
    }
    ~ObservedEntryLock() {
        if (observation_) observation_->entryLockHoldSeconds += secondsSince(acquired_);
    }
 private:
    RangeObservation* observation_;
    std::unique_lock<std::mutex> lock_;
    Clock::time_point acquired_{};
};
// The shared transaction must never retain a lane's stack observation pointer.
class ScopedManifestObservation {
 public:
    ScopedManifestObservation(Session& session, RangeObservation* observation) : session_(session) {
        std::visit([&](auto& s) { s.setManifestSaveStats(observation ? &observation->manifestSave : nullptr); }, session_);
    }
    ~ScopedManifestObservation() {
        std::visit([](auto& s) { s.setManifestSaveStats(nullptr); }, session_);
    }
 private:
    Session& session_;
};
class FileTransaction {
 public:
    explicit FileTransaction(PersistentFileTiming* timing, const PersistentTransferPolicy& policy = {},
                             storage::FileIoStats* ioStats = nullptr)
        : timing_(timing), policy_(policy), ioContext_(policy.options.fileIo), ioStats_(ioStats) {}
    Status begin(const std::string& root, const PersistentFileIdentity& id,
                 bool download, const std::string& remoteRoot, bool resume, RangeObservation* observation = nullptr) {
        std::lock_guard publicationLock(publicationMutex_);
        download_ = download;
        auto valid = tree::validateTreeRelativePath(id.relativePath);
        if (!valid.isOk()) return valid;
        std::error_code error;
        fs::path parent = fs::canonical(root, error);
        if (error) return Status::runtimeError("invalid persistent destination root");
        const auto relative = fs::path(id.relativePath);
        for (const auto& part : relative.parent_path()) {
            parent /= part;
            const auto type = fs::symlink_status(parent, error);
            if (type.type() == fs::file_type::not_found) {
                error.clear();
                fs::create_directory(parent, error);
            } else if (!fs::is_directory(type) || fs::is_symlink(type)) {
                return Status::invalidArgument("persistent destination parent is not a safe directory");
            }
            if (error) return Status::runtimeError("create persistent destination directory failed");
        }
        output_ = policy_.outputPath.empty() ? (parent / relative.filename()).string() : policy_.outputPath;
        auto available = ioContext_.validateAvailable();
        if (!available.isOk()) return available;
        const auto existing = fs::symlink_status(output_, error);
        if (existing.type() != fs::file_type::not_found && !policy_.options.overwrite) {
            if (!resume || error || !fs::is_regular_file(existing))
                return Status::invalidArgument("persistent output already exists");
            std::error_code sizeError;
            const auto size = fs::file_size(output_, sizeError);
            if (sizeError || size != id.totalSize) return Status::invalidArgument("persistent output identity mismatch");
            auto loaded = [&]() -> common::Result<checkpoint::DownloadManifest> {
                if (download) return checkpoint::loadDownloadManifest(checkpoint::downloadManifestPathForOutput(output_));
                auto uploaded = checkpoint::ManifestStore::load(checkpoint::manifestPathForOutput(output_));
                if (!uploaded.isOk()) return uploaded.status();
                const auto& upload = uploaded.value();
                if (upload.transferId != id.transferId || upload.chunkSize != id.chunkSize)
                    return Status::invalidArgument("committed upload identity mismatch");
                checkpoint::DownloadManifest converted;
                converted.state = upload.state; converted.totalSize = upload.totalSize;
                converted.checksumAlgorithm = upload.checksumAlgorithm;
                converted.sourcePath = (fs::path(remoteRoot)/relative).lexically_normal().generic_string();
                converted.verifiedChunks = upload.verifiedChunks;
                return converted;
            }();
            if (!loaded.isOk()) return loaded.status();
            const auto& m = loaded.value();
            if (m.state != checkpoint::ManifestState::Committed || m.totalSize != id.totalSize ||
                m.checksumAlgorithm != id.checksumAlgorithm || m.sourcePath !=
                    (fs::path(remoteRoot)/relative).lexically_normal().generic_string())
                return Status::invalidArgument("committed download identity mismatch");
            auto existingFile = storage::PosixFile::openReadOnly(output_);
            if (!existingFile.isOk()) return existingFile.status();
            struct stat existingMetadata{};
            if (::fstat(existingFile.value().fd(), &existingMetadata) != 0 ||
                existingMetadata.st_mtim.tv_sec != id.mtimeUnixSeconds)
                return Status::invalidArgument("committed output mtime mismatch");
            chunk::RangeList coverage;
            std::vector<std::uint8_t> checkBuffer(65536);
            for (const auto& record : m.verifiedChunks) {
                checksum::ChecksumComputer computer(id.checksumAlgorithm,policy_.options.checksumBackend);
                for (std::uint64_t pos=0;pos<record.length;) {
                    auto count = static_cast<std::size_t>(std::min<std::uint64_t>(checkBuffer.size(),record.length-pos));
                    auto read = existingFile.value().readAtAll(record.offset+pos,checkBuffer.data(),count);
                    if (!read.isOk()) return read;
                    computer.update(checkBuffer.data(),count);pos+=count;
                }
                if (computer.finalize().value != record.checksum.value)
                    return Status::invalidArgument("committed download checksum mismatch");
                auto inserted = coverage.insert(record.offset,record.length,id.totalSize);
                if (!inserted.isOk()) return inserted;
            }
            if (!coverage.missingRanges(id.totalSize).empty())
                return Status::invalidArgument("committed download coverage mismatch");
            skipped_ = true;
            identity_ = id;
            active_ = true;
            return Status::ok();
        }
        if (resume) {
            const auto manifest = download ? checkpoint::downloadManifestPathForOutput(output_)
                                           : checkpoint::manifestPathForOutput(output_);
            const auto info = fs::symlink_status(manifest, error);
            if (info.type() == fs::file_type::not_found) {
                // A peer can fail after FILE_BEGIN but before this file's checkpoint
                // exists. Create exclusively; never treat a corrupt manifest as absent.
                error.clear();
                resume = false;
            } else if (error || !fs::is_regular_file(info) || fs::is_symlink(info)) {
                return Status::invalidArgument("persistent checkpoint is not a safe regular file");
            }
        }
        if (resume) {
            if (download) {
                std::string sessionTransferId = id.transferId;
                const auto manifestPath = checkpoint::downloadManifestPathForOutput(output_);
                auto loaded = checkpoint::loadDownloadManifest(manifestPath);
                if (loaded.isOk() && loaded.value().sourcePath ==
                        (fs::path(remoteRoot) / relative).lexically_normal().generic_string() &&
                    loaded.value().totalSize == id.totalSize &&
                    loaded.value().chunkSize == id.chunkSize &&
                    loaded.value().checksumAlgorithm == id.checksumAlgorithm) {
                    sessionTransferId = loaded.value().transferId;
                }
                auto resumed = session::DownloadSession::resume(output_,
                    (fs::path(remoteRoot) / relative).lexically_normal().generic_string(),
                    sessionTransferId, id.totalSize, id.chunkSize, id.checksumAlgorithm,policy_.options.checksumBackend,
                    policy_.options.manifestFlushPolicy,policy_.options.manifestFlushIntervalChunks);
                if (!resumed.isOk()) return resumed.status();
                session_ = std::move(resumed.value());
            } else {
                auto resumed = session::TransferSession::resume(output_, id.transferId,
                    id.totalSize, id.chunkSize, id.checksumAlgorithm,policy_.options.checksumBackend,
                    policy_.options.manifestFlushPolicy,policy_.options.manifestFlushIntervalChunks);
                if (!resumed.isOk()) return resumed.status();
                session_ = std::move(resumed.value());
            }
        } else if (download) {
            auto created = session::DownloadSession::createNew(output_,
                (fs::path(remoteRoot) / relative).lexically_normal().generic_string(), id.transferId, id.totalSize,
                id.chunkSize, id.checksumAlgorithm,policy_.options.checksumBackend,
                    policy_.options.manifestFlushPolicy,policy_.options.manifestFlushIntervalChunks);
            if (!created.isOk()) return created.status();
            session_ = std::move(created.value());
        } else {
            auto created = session::TransferSession::createNew(output_, id.transferId, id.totalSize,
                id.chunkSize, id.checksumAlgorithm,policy_.options.checksumBackend,
                    policy_.options.manifestFlushPolicy,policy_.options.manifestFlushIntervalChunks);
            if (!created.isOk()) return created.status();
            session_ = std::move(created.value());
        }
        temp_ = std::visit([](auto& s) { return s.manifest().tempPath; }, session_);
        const auto manifest = std::visit([](auto& s) { return s.manifestPath(); }, session_);
        if (!resume && fs::symlink_status(manifest, error).type() != fs::file_type::not_found)
            return Status::invalidArgument("persistent manifest already exists; use resume");
        auto file = storage::PosixFile::openReadWriteExclusive(temp_);
        if (resume) file = storage::PosixFile::openReadWrite(temp_);
        if (!file.isOk()) return file.status();
        file_ = std::move(file.value());
        active_ = true;
        identity_ = id;
        checksumComputer_ = checksum::ChecksumComputer(id.checksumAlgorithm,policy_.options.checksumBackend);
        if (resume) {
            auto verified = std::visit([&](auto& s) { return s.verifyTempChunks(file_); }, session_);
            if (!verified.isOk()) return verified;
        }
        auto resized = file_.resize(id.totalSize);
        if (!resized.isOk()) return resized;
        if (policy_.options.preallocateMode == storage::PreallocateMode::Full) {
            auto allocated = file_.preallocate(id.totalSize);
            if (!allocated.isOk()) return allocated;
        }
        writer_ = std::make_unique<storage::BufferedFileWriter>(file_,ioContext_,ioStats_);
        auto advice = storage::applyFileIoAdvice(file_,policy_.options.fileIo.advice,0,id.totalSize);
        if (!advice.isOk()) return advice;
        ScopedManifestObservation observing(session_, observation);
        Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
        return resume ? common::Status::ok() : std::visit([](auto& s) { return s.save(); }, session_);
    }
    Status write(std::uint64_t offset, const std::vector<std::uint8_t>& bytes) {
        if (skipped_) return Status::ok();
        const auto writing = Clock::now();
        auto status = writer_->write(offset, bytes.data(), bytes.size());
        if (timing_) timing_->writeSeconds += secondsSince(writing);
        if (!status.isOk()) return status;
        const auto end = offset + bytes.size();
        std::size_t consumed = 0;
        while (consumed < bytes.size()) {
            const auto chunkOffset = (offset + consumed) / identity_.chunkSize * identity_.chunkSize;
            if (chunkBytes_ == 0) recorded_ = chunkOffset;
            if (chunkOffset != recorded_) return Status::invalidArgument("persistent chunk order mismatch");
            const auto length = std::min<std::uint64_t>(identity_.chunkSize, identity_.totalSize - recorded_);
            const auto part = std::min<std::size_t>(length - chunkBytes_, bytes.size() - consumed);
            checksumComputer_.update(bytes.data() + consumed, part);
            chunkBytes_ += part;
            consumed += part;
            if (chunkBytes_ != length) continue;
            auto flushed = writer_->flush();
            if (!flushed.isOk()) return flushed;
            Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
            status = std::visit([&](auto& s) { return s.recordVerifiedChunk(
                recorded_ / identity_.chunkSize, recorded_, length,
                checksumComputer_.finalize()); }, session_);
            if (!status.isOk()) return status;
            recorded_ += length;
            chunkBytes_ = 0;
            checksumComputer_ = checksum::ChecksumComputer(identity_.checksumAlgorithm,policy_.options.checksumBackend);
        }
        return Status::ok();
    }
    Status commit(RangeObservation* observation = nullptr) {
        ScopedManifestObservation observing(session_, observation);
        std::lock_guard publicationLock(publicationMutex_);
        if (skipped_) { active_ = false; return Status::ok(); }
        auto flushed = writer_->flush();
        if (!flushed.isOk()) return flushed;
        if (!std::visit([](const auto& s) { return s.missingRanges().empty(); }, session_))
            return Status::invalidArgument("persistent commit requires complete verified coverage");
        const bool fullVerify = policy_.options.finalVerifyPolicy == session::FinalVerifyPolicy::Full ||
            identity_.checksumAlgorithm == checksum::ChecksumAlgorithm::None;
        if (fullVerify) {
            auto verified = std::visit([&](auto& s) { return s.verifyTempChunks(file_); }, session_);
            if (!verified.isOk()) return verified;
        }
        if (!std::visit([](const auto& s) { return s.missingRanges().empty(); }, session_))
            return Status::invalidArgument("persistent final verification found missing ranges");
        timespec times[2]{{0, UTIME_OMIT}, {identity_.mtimeUnixSeconds, 0}};
        if (::futimens(file_.fd(), times) != 0) return Status::runtimeError("persistent mtime failed");
        common::Status status;
        { Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
          status = std::visit([](auto& s) { return s.flushManifest(); }, session_); }
        if (!status.isOk()) return status;
        if (policy_.options.commitSyncPolicy != session::CommitSyncPolicy::None) {
            status = file_.sync();
            if (!status.isOk()) return status;
        }
        if (policy_.options.overwrite) {
            if (::rename(temp_.c_str(),output_.c_str()) != 0)
                return Status::runtimeError("persistent replace failed");
        } else if (::link(temp_.c_str(), output_.c_str()) != 0)
            return Status::runtimeError("persistent publish failed (output exists or IO error)");
        if (policy_.options.commitSyncPolicy == session::CommitSyncPolicy::FsyncFileAndDir) {
            status = storage::PosixFile::fsyncParentDirectory(output_);
            if (!status.isOk()) return status;
        }
        published_ = true;
        { Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
          status = std::visit([](auto& s) { return s.markCommitted(); }, session_); }
        if (!status.isOk()) return status;
        if (!policy_.options.overwrite && ::unlink(temp_.c_str()) != 0)
            return Status::runtimeError("persistent temp unlink failed");
        active_ = false;
        // The batch context retains committed transactions for its final audit,
        // but their output handles are no longer needed. The audit reopens the
        // published path on demand through verifyPublished().
        writer_.reset();
        file_ = storage::PosixFile{};
        return Status::ok();
    }
    std::uint64_t loadedVerifiedChunks() const {
        return skipped_ ? 0 : std::visit([](const auto& s) { return s.stats().loadedVerifiedChunks; },session_);
    }
    std::uint64_t removedCorruptChunks() const {
        return skipped_ ? 0 : std::visit([](const auto& s) { return s.stats().removedCorruptChunks; },session_);
    }
    Status verifyPublished() const {
        std::lock_guard publicationLock(publicationMutex_);
        if (!published_ && !skipped_) return Status::ok();
        auto verify = [&](const auto& manifest) -> Status {
            if (manifest.state != checkpoint::ManifestState::Committed ||
                manifest.totalSize != identity_.totalSize ||
                manifest.chunkSize != identity_.chunkSize ||
                manifest.checksumAlgorithm != identity_.checksumAlgorithm)
                return Status::invalidArgument("committed output identity mismatch: " + identity_.relativePath);
            std::error_code error;
            const auto type = fs::symlink_status(output_, error);
            if (error || !fs::is_regular_file(type) || fs::is_symlink(type))
                return Status::invalidArgument("committed output is not a safe regular file: " + identity_.relativePath);
            auto file = storage::PosixFile::openReadOnly(output_);
            if (!file.isOk()) return file.status();
            struct stat metadata{};
            if (::fstat(file.value().fd(), &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
                metadata.st_size < 0 || static_cast<std::uint64_t>(metadata.st_size) != identity_.totalSize ||
                metadata.st_mtim.tv_sec != identity_.mtimeUnixSeconds)
                return Status::invalidArgument("committed output metadata mismatch: " + identity_.relativePath);
            chunk::RangeList coverage;
            std::vector<std::uint8_t> buffer(65536);
            for (const auto& record : manifest.verifiedChunks) {
                checksum::ChecksumComputer computer(identity_.checksumAlgorithm,policy_.options.checksumBackend);
                for (std::uint64_t position = 0; position < record.length;) {
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>(buffer.size(),record.length-position));
                    auto read = file.value().readAtAll(record.offset+position,buffer.data(),count);
                    if (!read.isOk()) return read;
                    computer.update(buffer.data(),count);
                    position += count;
                }
                if (computer.finalize().value != record.checksum.value)
                    return Status::invalidArgument("committed output checksum mismatch: " + identity_.relativePath);
                auto inserted = coverage.insert(record.offset,record.length,identity_.totalSize);
                if (!inserted.isOk()) return inserted;
            }
            if (!coverage.missingRanges(identity_.totalSize).empty())
                return Status::invalidArgument("committed output coverage mismatch: " + identity_.relativePath);
            return Status::ok();
        };
        if (download_) {
            auto manifest = checkpoint::loadDownloadManifest(
                checkpoint::downloadManifestPathForOutput(output_));
            if (!manifest.isOk()) return manifest.status();
            return verify(manifest.value());
        }
        auto manifest = checkpoint::ManifestStore::load(checkpoint::manifestPathForOutput(output_));
        if (!manifest.isOk()) return manifest.status();
        return verify(manifest.value());
    }
    std::vector<chunk::CompletedRange> missingRanges() const {
        return skipped_ ? std::vector<chunk::CompletedRange>{} :
            std::visit([](const auto& s) { return s.missingRanges(); },session_);
    }
    bool rangeComplete(std::uint64_t offset, std::uint64_t length) const {
        if (!active_ || length == 0 || offset > identity_.totalSize || length > identity_.totalSize - offset)
            return false;
        if (skipped_) return true;
        const auto missing = std::visit([](const auto& s) { return s.missingRanges(); }, session_);
        for (const auto& range : missing) {
            if (range.begin < offset + length && offset < range.end) return false;
        }
        return true;
    }
    Status writeRange(std::uint64_t offset, const std::vector<std::uint8_t>& bytes, RangeObservation* observation) {
        if (skipped_) return Status::ok();
        if (!active_ || offset > identity_.totalSize || bytes.size() > identity_.totalSize - offset)
            return Status::invalidArgument("persistent range write exceeds file boundary");
        if (observation) ++observation->offsetWriteCalls;
        checkpoint::OptionalManifestTimer writing(observation ? &observation->offsetWriteSeconds : nullptr);
        const auto started = Clock::now();
        auto status = writer_->write(offset, bytes.data(), bytes.size());
        if (timing_) timing_->writeSeconds += secondsSince(started);
        return status;
    }
    Status recordRange(std::uint64_t rangeId, std::uint64_t offset, std::uint64_t length,
                       checksum::ChecksumValue checksum, RangeObservation* observation, std::uint32_t batch) {
        ScopedManifestObservation observing(session_, observation);
        if (skipped_) return Status::ok();
        if (!active_ || rangeId == 0 || length == 0 || offset > identity_.totalSize ||
            length > identity_.totalSize - offset)
            return Status::invalidArgument("persistent range completion is invalid");
        Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
        if (observation) ++observation->chunkFlushCalls;
        checkpoint::OptionalManifestTimer flushing(observation ? &observation->chunkFlushSeconds : nullptr);
        auto flushed = writer_->flush();
        flushing.stop();
        checkpoint::OptionalManifestTimer checkpointing(observation ? &observation->checkpointSeconds : nullptr);
        if (!flushed.isOk()) return flushed;
        const auto recordId=std::visit([](const auto& s) {
            std::uint64_t next=0;
            for (const auto& r:s.manifest().verifiedChunks) {
                if (r.chunkId==UINT64_MAX) return UINT64_MAX;
                next=std::max(next,r.chunkId+1);
            }
            return next;
        },session_);
        if (recordId==UINT64_MAX) return Status::invalidArgument("checkpoint record ID exhausted");
        auto status = std::visit([&](auto& s) { return s.recordVerifiedChunk(recordId, offset, length, checksum, batch > 1); }, session_);
        if (!status.isOk()) return status;
        checkpointing.stop();
        if (pendingRangeChunks_++ == 0) oldestRangeChunk_ = Clock::now();
        if (pendingRangeChunks_ >= batch || Clock::now() >= rangeCheckpointDeadline())
            return flushRangeCheckpoint(observation);
        return Status::ok();
    }
    bool hasPendingRangeCheckpoint() const { return pendingRangeChunks_ != 0; }
    Clock::time_point rangeCheckpointDeadline() const {
        return oldestRangeChunk_ + std::chrono::milliseconds(100);
    }
    Status flushRangeCheckpoint(RangeObservation* observation) {
        if (!pendingRangeChunks_) return Status::ok();
        ScopedManifestObservation observing(session_, observation);
        checkpoint::OptionalManifestTimer checkpointing(observation ? &observation->checkpointSeconds : nullptr);
        auto status = std::visit([](auto& s) { return s.flushManifest(); }, session_);
        if (status.isOk()) pendingRangeChunks_ = 0;
        return status;
    }
    ~FileTransaction() {
        // Partial files/checkpoints remain readable by the V3 persistent resume path.
        if (active_ && !published_ && !skipped_) (void)std::visit([](auto& s) { return s.markFailed(); }, session_);
    }
 private:
    mutable std::mutex publicationMutex_;
    PersistentFileTiming* timing_;
    PersistentTransferPolicy policy_;
    storage::FileIoContext ioContext_;
    storage::FileIoStats* ioStats_;
    std::unique_ptr<storage::BufferedFileWriter> writer_;
    Session session_;
    storage::PosixFile file_;
    PersistentFileIdentity identity_;
    checksum::ChecksumComputer checksumComputer_{checksum::ChecksumAlgorithm::None};
    std::string output_, temp_;
    std::uint64_t pendingRangeChunks_ = 0;
    Clock::time_point oldestRangeChunk_{};
    std::uint64_t recorded_ = 0;
    std::uint64_t chunkBytes_ = 0;
    bool active_ = false, published_ = false, skipped_ = false, download_ = false;
};

Status verifyCommittedFilesInternal(const std::shared_ptr<PersistentTransferContext>& context) {
    std::vector<std::function<Status()>> checks;
    {
        std::lock_guard lock(context->mutex);
        checks = context->integrityChecks;
    }
    for (const auto& check : checks) {
        const auto status = check();
        if (!status.isOk()) return status;
    }
    return Status::ok();
}

class RangeFileCoordinator {
 public:
    struct Slot {
        PersistentFileIdentity id;
        bool active = true, completed = false;
        std::vector<chunk::CompletedRange> missing;
        std::size_t next = 0;
        std::uint64_t position = 0;
        checksum::ChecksumComputer checksum{checksum::ChecksumAlgorithm::None};
    };
    struct Entry {
        std::mutex mutex;
        std::unique_ptr<FileTransaction> transaction;
        PersistentFileIdentity base;
        std::unordered_map<std::uint64_t, Slot> slots;
        std::uint64_t rangeCount = 0, completed = 0;
        bool committed = false;
        std::uint64_t loadedVerifiedChunks = 0;
        std::uint64_t removedCorruptChunks = 0;
        bool statsReported = false;
        std::uint32_t checkpointBatch = 1;
        std::uint64_t checkpointEpoch = 0;
        std::weak_ptr<RangeCheckpointQueue> checkpointQueue;
        RangeObservation background;
        bool observeBackground = false;
        Status checkpointFailure;
    };
    static common::Result<std::shared_ptr<Entry>> begin(PersistentTransferContext& context,
        const std::string& root, const PersistentFileIdentity& id, bool download,
        const std::string& remoteRoot, bool resume, RangeObservation* observation) {
        if (!id.rangeId || !id.rangeCount || id.rangeId > id.rangeCount ||
            !id.rangeLength || id.rangeOffset > id.totalSize ||
            id.rangeLength > id.totalSize-id.rangeOffset)
            return Status::invalidArgument("invalid range identity/bounds");
        auto batch = rangeCheckpointBatch(context.policy);
        if (!batch.isOk()) return batch.status();
        const auto key = (fs::path(root) / id.relativePath).lexically_normal().string();
        std::shared_ptr<Entry> entry;
        {
            std::lock_guard lock(context.mutex);
            auto found = context.files.find(key);
            if (found != context.files.end()) entry = std::static_pointer_cast<Entry>(found->second);
            if (!entry) {
                entry = std::make_shared<Entry>();
                entry->base = id;
                entry->base.rangeId = entry->base.rangeOffset = entry->base.rangeLength =
                    entry->base.rangeCount = entry->base.attempt = 0;
                entry->rangeCount = id.rangeCount;
                entry->checkpointBatch = batch.value();
                entry->observeBackground = observation != nullptr;
                if (entry->checkpointBatch > 1) {
                    if (!context.checkpoints) context.checkpoints = std::make_shared<RangeCheckpointQueue>();
                    entry->checkpointQueue = context.checkpoints;
                }
                // The transaction outlives any channel's stack timing object.
                entry->transaction = std::make_unique<FileTransaction>(nullptr,context.policy);
                auto status = entry->transaction->begin(root,entry->base,download,remoteRoot,resume,observation);
                if (!status.isOk()) return status;
                entry->loadedVerifiedChunks = entry->transaction->loadedVerifiedChunks();
                entry->removedCorruptChunks = entry->transaction->removedCorruptChunks();
                context.files.emplace(key,entry);
                std::weak_ptr<Entry> weakEntry = entry;
                context.integrityChecks.emplace_back([weakEntry] {
                    auto current = weakEntry.lock();
                    if (!current) return Status::ok();
                    std::lock_guard entryLock(current->mutex);
                    if (!current->checkpointFailure.isOk()) return current->checkpointFailure;
                    return current->transaction->verifyPublished();
                });
            }
        }
        ObservedEntryLock lock(entry->mutex, observation);
        if (!entry->checkpointFailure.isOk()) return entry->checkpointFailure;
        auto base = id;
        base.rangeId = base.rangeOffset = base.rangeLength = base.rangeCount = base.attempt = 0;
        // A recovered file can become fully verified before every original range
        // is replayed. Later ranges may only receive an empty missing-set handshake;
        // write() still rejects all DATA after publication.
        if (!samePersistentIdentity(entry->base,base) || entry->rangeCount != id.rangeCount)
            return Status::invalidArgument("range coordinator identity mismatch or replay");
        auto found = entry->slots.find(id.rangeId);
        if (found == entry->slots.end()) {
            for (const auto& [rangeId, slot] : entry->slots) {
                if (slot.id.rangeOffset < id.rangeOffset+id.rangeLength &&
                    id.rangeOffset < slot.id.rangeOffset+slot.id.rangeLength)
                    return Status::invalidArgument("overlapping range claim");
            }
            found = entry->slots.emplace(id.rangeId, Slot{}).first;
        } else {
            auto& previous = found->second;
            if (id.attempt <= previous.id.attempt)
                return Status::invalidArgument("range slot already claimed");
            // A data reset leaves the receiver's slot in the batch context. A
            // later lane attempt may reclaim it; checkpointed bytes remain in the
            // transaction and are excluded when the missing set is rebuilt.
            if (entry->committed && previous.completed) {
                previous.id = id;
                return entry;
            }
            if (previous.completed && entry->completed != 0) --entry->completed;
        }
        auto& slot = found->second;
        slot = Slot{};
        slot.id = id;
        slot.checksum = checksum::ChecksumComputer(id.checksumAlgorithm);
        for (const auto& missing:entry->transaction->missingRanges()) {
            const auto begin=std::max(missing.begin,id.rangeOffset);
            const auto end=std::min(missing.end,id.rangeOffset+id.rangeLength);
            // V3 range resumes reuse chunk-sized records from its own checkpoints.
            for (auto pos=begin;pos<end;) {
                auto next=std::min(end,(pos/id.chunkSize+1)*id.chunkSize);
                if (next<=pos) next=end;
                slot.missing.push_back({pos,next});pos=next;
            }
        }
        slot.position=slot.missing.empty() ? id.rangeOffset+id.rangeLength : slot.missing[0].begin;
        return entry;
    }
    static Status write(const std::shared_ptr<Entry>& entry, const PersistentFileIdentity& id,
                        std::uint64_t offset, const std::vector<std::uint8_t>& bytes, RangeObservation* observation) {
        ObservedEntryLock lock(entry->mutex, observation);
        if (!entry->checkpointFailure.isOk()) return entry->checkpointFailure;
        const auto slot = entry->slots.find(id.rangeId);
        if (entry->committed || slot == entry->slots.end() || !slot->second.active ||
            !samePersistentIdentity(slot->second.id,id) || offset < id.rangeOffset ||
            offset > id.rangeOffset+id.rangeLength ||
            bytes.size() > id.rangeOffset+id.rangeLength-offset)
            return Status::invalidArgument("range write without exclusive valid slot");
        auto& lease=slot->second;
        if (lease.next>=lease.missing.size() || offset!=lease.position ||
            bytes.size()>lease.missing[lease.next].end-lease.position)
            return Status::invalidArgument("range DATA touches already verified bytes");
        auto status=entry->transaction->writeRange(offset,bytes,observation);
        if (!status.isOk()) return status;
        checkpoint::OptionalManifestTimer checksumming(observation ? &observation->chunkChecksumSeconds : nullptr);
        lease.checksum.update(bytes.data(),bytes.size());
        checksumming.stop();
        lease.position+=bytes.size();
        if (lease.position==lease.missing[lease.next].end) {
            const auto piece=lease.missing[lease.next];
            status=entry->transaction->recordRange(id.rangeId,piece.begin,piece.end-piece.begin,lease.checksum.finalize(),observation,entry->checkpointBatch);
            if (!status.isOk()) { entry->checkpointFailure = status; return status; }
            scheduleCheckpoint(entry);
            ++lease.next;
            lease.position=lease.next==lease.missing.size() ? id.rangeOffset+id.rangeLength : lease.missing[lease.next].begin;
            lease.checksum=checksum::ChecksumComputer(id.checksumAlgorithm);
        }
        return Status::ok();
    }
    static std::pair<std::uint64_t, std::uint64_t> takeStats(
        const std::shared_ptr<Entry>& entry) {
        std::lock_guard lock(entry->mutex);
        if (entry->statsReported) return {0, 0};
        entry->statsReported = true;
        return {entry->loadedVerifiedChunks, entry->removedCorruptChunks};
    }
    static std::vector<chunk::CompletedRange> missingRanges(const std::shared_ptr<Entry>& entry,
                                                           const PersistentFileIdentity& id) {
        std::lock_guard lock(entry->mutex);
        return entry->slots.at(id.rangeId).missing;
    }
    static bool rangeComplete(const std::shared_ptr<Entry>& entry, const PersistentFileIdentity& id) {
        std::lock_guard lock(entry->mutex);
        return entry->transaction->rangeComplete(id.rangeOffset,id.rangeLength);
    }
    // Called on all receiver exits, including malformed input and clean disconnect.
    static Status flush(const std::shared_ptr<Entry>& entry, RangeObservation* observation) {
        if (!entry) return Status::ok();
        ObservedEntryLock lock(entry->mutex, observation);
        return flushLocked(entry, observation);
    }
 private:
    static void mergeBackground(Entry& entry, RangeObservation* observation) {
        if (!observation) return;
        auto& a = *observation; auto& b = entry.background;
        a.entryLockCalls += b.entryLockCalls;
        a.entryLockWaitSeconds += b.entryLockWaitSeconds;
        a.entryLockHoldSeconds += b.entryLockHoldSeconds;
        a.checkpointSeconds += b.checkpointSeconds;
        a.backgroundCheckpointCpuSeconds += b.backgroundCheckpointCpuSeconds;
        a.manifestSave.attempts += b.manifestSave.attempts;
        a.manifestSave.serializedBytes += b.manifestSave.serializedBytes;
        a.manifestSave.renameCalls += b.manifestSave.renameCalls;
        a.manifestSave.failures += b.manifestSave.failures;
        a.manifestSave.serializeSeconds += b.manifestSave.serializeSeconds;
        a.manifestSave.tempWriteSeconds += b.manifestSave.tempWriteSeconds;
        a.manifestSave.renameSeconds += b.manifestSave.renameSeconds;
        b = {};
    }
    static Status flushLocked(const std::shared_ptr<Entry>& entry, RangeObservation* observation) {
        ++entry->checkpointEpoch;
        if (auto queue = entry->checkpointQueue.lock()) queue->cancel(entry.get());
        mergeBackground(*entry, observation);
        if (!entry->checkpointFailure.isOk()) return entry->checkpointFailure;
        auto status = entry->transaction->flushRangeCheckpoint(observation);
        if (!status.isOk()) entry->checkpointFailure = status;
        return status;
    }
    static void scheduleCheckpoint(const std::shared_ptr<Entry>& entry) {
        auto queue = entry->checkpointQueue.lock();
        if (!queue) return; // batch=1 needs no thread
        const auto epoch = ++entry->checkpointEpoch;
        if (!entry->transaction->hasPendingRangeCheckpoint()) { queue->cancel(entry.get()); return; }
        std::weak_ptr<Entry> weakEntry = entry;
        queue->schedule(entry.get(), entry->transaction->rangeCheckpointDeadline(), [weakEntry, epoch] {
            auto current = weakEntry.lock();
            if (!current) return;
            auto* observation = current->observeBackground ? &current->background : nullptr;
            ObservedEntryLock lock(current->mutex, observation);
            if (epoch != current->checkpointEpoch || !current->checkpointFailure.isOk()) return;
            const auto cpu = threadCpuSeconds(observation != nullptr);
            const auto status = current->transaction->flushRangeCheckpoint(observation);
            if (!status.isOk()) current->checkpointFailure = status;
            if (observation) observation->backgroundCheckpointCpuSeconds += threadCpuSeconds(true)-cpu;
        });
    }
 public:
    static Status complete(const std::shared_ptr<Entry>& entry, const PersistentFileIdentity& id,
                           checksum::ChecksumValue checksum, bool skipped, RangeObservation* observation) {
        ObservedEntryLock lock(entry->mutex, observation);
        auto flushed = flushLocked(entry, observation);
        if (!flushed.isOk()) return flushed;
        const auto slot = entry->slots.find(id.rangeId);
        if (slot == entry->slots.end() || !samePersistentIdentity(slot->second.id,id))
            return Status::invalidArgument("invalid or duplicate range completion");
        if (slot->second.completed) {
            // A retry can replay FILE_END after the receiver committed the
            // checkpoint before the data socket reset. The empty resume mask
            // makes this completion idempotent.
            if (entry->committed && skipped) return Status::ok();
            return Status::invalidArgument("invalid or duplicate range completion");
        }
        if (!slot->second.active || (entry->committed && !skipped))
            return Status::invalidArgument("invalid or duplicate range completion");
        if (skipped && !entry->committed &&
            !entry->transaction->rangeComplete(id.rangeOffset,id.rangeLength))
            return Status::invalidArgument("skipped range is not verified");
        if (slot->second.next!=slot->second.missing.size())
            return Status::invalidArgument("range completion has missing bytes");
        auto status = Status::ok();
        slot->second.completed = true;
        slot->second.active = false;
        ++entry->completed;
        const bool drained = std::all_of(entry->slots.begin(), entry->slots.end(),
            [](const auto& item) { return !item.second.active; });
        const bool verified = entry->transaction->rangeComplete(0, entry->base.totalSize);
        // Prior checkpointed ranges need not be dispatched again in this epoch.
        // Full verified coverage plus no active writer is sufficient to publish.
        if (!entry->committed &&
            (entry->completed == entry->rangeCount || (drained && verified))) {
            status = entry->transaction->commit(observation);
            if (!status.isOk()) return status;
            entry->committed = true;
        }
        return Status::ok();
    }
};

Status notify(const PersistentFileCallback& callback, const PersistentFileIdentity& id,
              const Status& status, bool complete) {
    return callback ? callback(id, status, complete) : Status::ok();
}
bool sourceMatches(const struct stat& st, const PersistentFileIdentity& id) {
    return S_ISREG(st.st_mode) && st.st_size >= 0 &&
        static_cast<std::uint64_t>(st.st_size) == id.totalSize && st.st_mtim.tv_sec == id.mtimeUnixSeconds;
}
}  // namespace

common::Status verifyCommittedPersistentOutputs(
    const std::shared_ptr<PersistentTransferContext>& context) {
    return verifyCommittedFilesInternal(context);
}

common::Result<std::vector<PersistentFileIdentity>> expandPersistentFileRanges(
    const std::vector<PersistentFileIdentity>& files, std::uint32_t channelCount) {
    if (channelCount == 0 || channelCount > kUnifiedFileMaxChannels)
        return Status::invalidArgument("invalid range planner channel count");
    std::vector<PersistentFileIdentity> expanded;
    for (const auto& file : files) {
        chunk::AdaptiveRangeInputs inputs;
        inputs.fileSize = file.totalSize;
        inputs.chunkSize = file.chunkSize;
        inputs.channels = channelCount;
        auto planned = chunk::planAdaptiveRanges(inputs);
        if (!planned.isOk()) return planned.status();
        if (!planned.value().striped) {
            expanded.push_back(file);
            continue;
        }
        for (const auto& range : planned.value().ranges) {
            auto ranged = file;
            ranged.rangeId = range.chunkId;
            ranged.rangeOffset = range.offset;
            ranged.rangeLength = range.length;
            ranged.rangeCount = planned.value().ranges.size();
            expanded.push_back(std::move(ranged));
        }
    }
    return expanded;
}

common::Result<std::vector<tree::TreeFileInfo>> scanPersistentTree(const std::string& root) {
    auto scanned = tree::scanLocalTree(root, true);
    if (!scanned.isOk()) return scanned.status();
    std::unordered_set<std::string> artifacts;
    for (const auto& file : scanned.value()) {
        const auto full = fs::absolute(file.fullPath).lexically_normal().string();
        auto owned = [&](const auto& manifest, const std::string& output, const std::string& expectedTemp) {
            if (full == fs::absolute(output).lexically_normal().string() && manifest.tempPath == expectedTemp) {
                artifacts.insert(full);
                artifacts.insert(fs::absolute(manifest.tempPath).lexically_normal().string());
            }
        };
        if (file.relativePath.ends_with(".cpnetflux.manifest")) {
            auto loaded = checkpoint::ManifestStore::load(file.fullPath);
            if (loaded.isOk()) {
                const auto& m = loaded.value();
                owned(m, checkpoint::manifestPathForOutput(m.outputPath),
                      checkpoint::tempPathForOutput(m.outputPath, m.transferId));
            }
        } else if (file.relativePath.ends_with(".cpnetflux.download.manifest")) {
            auto loaded = checkpoint::loadDownloadManifest(file.fullPath);
            if (loaded.isOk()) {
                const auto& m = loaded.value();
                owned(m, checkpoint::downloadManifestPathForOutput(m.targetPath),
                      checkpoint::downloadTempPathForOutput(m.targetPath, m.transferId));
            }
        }
    }
    auto& files = scanned.value();
    files.erase(std::remove_if(files.begin(), files.end(), [&](const auto& file) {
        return artifacts.contains(fs::absolute(file.fullPath).lexically_normal().string());
    }), files.end());
    return std::move(files);
}

common::Status sendPersistentTree(FramedDataSocket* socket, const std::string& root,
    const std::vector<PersistentFileIdentity>& files, PersistentTreeStats* stats,
    const PersistentFileCallback& callback, std::uint32_t pendingWindow,
    std::uint32_t channelIndex, std::uint32_t channelCount, DynamicFileQueue* ready,
    const config::FileTransferOptions* transferOptions, bool continueAfterFileFailure,
    std::uint32_t maxRetries) {
    if (!stats) return Status::invalidArgument("missing persistent stats");
    if (!socket || !socket->valid()) return Status::invalidArgument("invalid persistent data socket");
    if (pendingWindow == 0 || pendingWindow > 16)
        return Status::invalidArgument("persistent pending window must be in range 1..16");
    if (channelCount == 0 || channelCount > kUnifiedFileMaxChannels ||
        channelIndex >= channelCount)
        return Status::invalidArgument("persistent channel index/count is invalid");
    const auto pipelineDepth = diagnosticPipelineDepth();
    if (!pipelineDepth.isOk()) return pipelineDepth.status();
    stats->pendingWindow = pendingWindow;
    const std::uint64_t filesAtConnectionStart = stats->files;
    auto status = PersistentDataSession::setTimeout(socket, 30);
    if (!status.isOk()) return status;
    std::optional<WorkExecutor> executor;
    if (ready) {
        auto context = ready->worker(channelIndex);
        if (!context.isOk()) return context.status();
        executor.emplace(*ready, context.value());
    }
    struct PendingFile {
        PersistentFileIdentity id;
        PersistentFileTiming timing;
        Clock::time_point started, resultStarted;
        std::optional<WorkLease> lease;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<PendingFile> pending;
    std::mutex callbackMutex;
    Status readerStatus = Status::ok();
    Status overall = Status::ok();
    std::uint64_t writerWireBytes = 0;
    std::uint64_t readerWireBytes = 0;
    bool writerDone = false;
    bool awaitingResume = false;
    bool abort = false;
    auto requestAbort = [&](const Status& failure) {
        // A transport fault is reconciled after both IO threads have stopped.
        // Cancelling the shared queue here would destroy healthy lanes.
        std::lock_guard<std::mutex> lock(mutex);
        if (readerStatus.isOk()) readerStatus = failure;
        abort = true;
        changed.notify_all();
        if (socket->valid()) (void)::shutdown(socket->fd(), SHUT_RDWR);
    };
    auto notifySafe = [&](const PersistentFileIdentity& id, const Status& fileStatus, bool complete) {
        if (!callback) return Status::ok();
        std::lock_guard<std::mutex> lock(callbackMutex);
        return callback(id, fileStatus, complete);
    };
    auto addWire = [&](std::uint64_t bytes) { writerWireBytes += bytes; };
    std::thread resultReader([&]() {
        for (;;) {
            PersistentFileIdentity expected;
            std::optional<WorkLease> lease;
            PersistentFileTiming timing;
            Clock::time_point fileStarted, resultStarted;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return abort || writerDone || (!pending.empty() && !awaitingResume); });
                if (abort) return;
                if (pending.empty() && writerDone) return;
                expected = pending.front().id;
                lease = pending.front().lease;
                timing = pending.front().timing;
                fileStarted = pending.front().started;
                resultStarted = pending.front().resultStarted;
            }
            const auto started = std::chrono::steady_clock::now();
            auto result = PersistentDataSession::readResult(socket, expected);
            const double resultSeconds = secondsSince(resultStarted);
            const double waitSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            if (!result.isOk()) {
                requestAbort(result.status());
                return;
            }
            const auto fileStatus = receiverFileStatus(result.value(), expected.relativePath);
            if (ready) {
                if (!lease) { requestAbort(Status::runtimeError("missing work lease")); return; }
                auto completed = !fileStatus.isOk() &&
                    (continueAfterFileFailure || result.value() == FrameStatusCode::OutputExists)
                    ? executor->failTerminal(*lease, fileStatus)
                    : executor->finalize(*lease, fileStatus);
                if (!completed.isOk()) { requestAbort(completed); return; }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (pending.empty() || pending.front().id.fileId != expected.fileId ||
                    pending.front().id.generation != expected.generation ||
                    pending.front().id.totalSize != expected.totalSize) {
                    if (readerStatus.isOk()) readerStatus = Status::invalidArgument(
                        "persistent FILE_RESULT queue identity mismatch");
                    abort = true;
                    changed.notify_all();
                    return;
                }
                pending.pop_front();
                stats->completeWaitSeconds += waitSeconds;
                readerWireBytes += 2 * protocol::kFrameHeaderSize;
                if (expected.rangeId == 0 || expected.rangeId == expected.rangeCount) {
                    ++stats->files;
                    if (fileStatus.isOk()) stats->bytes += expected.totalSize;
                }
                if (fileStatus.isOk() && expected.rangeId != 0) ++stats->rangeCompletions;
                if (stats->pendingHighWatermark > pending.size()) {
                    // Keep the peak unchanged; this branch documents that the queue is bounded.
                }
                if (!fileStatus.isOk() && overall.isOk()) overall = fileStatus;
            }
            changed.notify_all();
            const auto manifestStarted = Clock::now();
            const auto callbackStatus = notifySafe(expected, fileStatus, true);
            timing.manifestSeconds += secondsSince(manifestStarted);
            timing.fileResultSeconds = resultSeconds;
            timing.wallSeconds = secondsSince(fileStarted);
            if (stats->phaseTiming) stats->fileTimings.push_back(std::move(timing));
            if (!callbackStatus.isOk()) {
                requestAbort(callbackStatus);
                return;
            }
            if (ready && !fileStatus.isOk() && !continueAfterFileFailure) {
                requestAbort(fileStatus);
                return;
            }
        }
    });
    auto failWriter = [&](const Status& failure) {
        requestAbort(failure);
        if (socket->valid()) (void)::shutdown(socket->fd(), SHUT_RDWR);
    };
    std::size_t nextFile = 0;
    for (;;) {
        PersistentFileIdentity id;
        std::optional<WorkLease> lease;
        PersistentFileTiming timing;
        if (!ready && nextFile >= files.size()) break;
        if (!ready) id = files[nextFile++];
        if (!ready && !persistentFileAssignedToChannel(id.fileId, channelIndex, channelCount)) {
            failWriter(Status::invalidArgument("persistent file assigned to wrong channel"));
            break;
        }
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&] { return abort || pending.size() < pendingWindow; });
            if (abort) break;
        }
        if (ready) {
            auto next = executor->claim();
            if (!next.isOk()) { failWriter(next.status()); break; }
            if (!next.value()) break;
            lease = *next.value();
            id = lease->file;
            timing.queueWaitSeconds = next.value()->queueWaitSeconds;
            stats->queueWaitSeconds += next.value()->queueWaitSeconds;
        }
        if (transferOptions && transferOptions->reconnectAttempt != UINT32_MAX)
            id.attempt = transferOptions->reconnectAttempt;
        timing.file = id; timing.channel = channelIndex; timing.sender = true;
        const auto fileStarted = Clock::now();
        status = tree::validateTreeRelativePath(id.relativePath);
        if (!status.isOk()) { failWriter(status); break; }
        const auto sourcePath = transferOptions && !transferOptions->path.empty()
            ? transferOptions->path : (fs::path(root)/id.relativePath).string();
        UniqueFd fd(::open(sourcePath.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (!fd.isValid()) { failWriter(Status::runtimeError("open persistent source failed")); break; }
        struct stat before{};
        if (::fstat(fd.get(), &before) != 0 || !sourceMatches(before, id)) {
            failWriter(Status::runtimeError("persistent source changed before transfer")); break;
        }
        storage::PosixFile file(std::move(fd));
        const auto manifestStarted = Clock::now();
        status = notifySafe(id, Status::ok(), false);
        timing.manifestSeconds += secondsSince(manifestStarted);
        if (!status.isOk()) { failWriter(status); break; }
        const bool needsHandshake = id.rangeId != 0 || (transferOptions && transferOptions->resume);
        if (needsHandshake) {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&] { return abort || (!awaitingResume && pending.empty()); });
            if (abort) break;
            awaitingResume = true;
            changed.notify_all();
        }
        if (ready) {
            status = executor->begin(*lease);
            if (!status.isOk()) { failWriter(status); break; }
        }
        status = PersistentDataSession::writeBegin(socket, id,transferOptions && transferOptions->resume);
        if (!status.isOk()) { failWriter(status); break; }
        bool skipRange = false;
        std::vector<chunk::CompletedRange> missing{{id.rangeId ? id.rangeOffset : 0,
            id.rangeId ? id.rangeOffset+id.rangeLength : id.totalSize}};
        if (id.totalSize == 0) missing.clear();
        if (needsHandshake) {
            auto resume = PersistentDataSession::readResumeResponse(socket, id);
            if (!resume.isOk()) { failWriter(resume.status()); break; }
            if (resume.value().statusCode != FrameStatusCode::Ok) {
                failWriter(resume.value().statusCode == FrameStatusCode::OutputExists
                    ? receiverFileStatus(resume.value().statusCode, id.relativePath)
                    : Status::runtimeError("persistent range resume rejected"));
                break;
            }
            missing = resume.value().missingRanges;
            skipRange = missing.empty();
            {
                std::lock_guard<std::mutex> lock(mutex);
                awaitingResume = false;
                changed.notify_all();
            }
        }
        const auto metadata = protocol::encodeFileBeginMetadata({protocol::SessionMode::New,
            id.transferId, id.totalSize, id.chunkSize, id.checksumAlgorithm, id.relativePath,
            id.rangeId, id.rangeOffset, id.rangeLength, id.rangeCount, id.attempt});
        if (!metadata.isOk()) { failWriter(metadata.status()); break; }
        addWire(protocol::kFrameHeaderSize + metadata.value().size() + 8);
        bool writeFailed = false;
        const auto options = transferOptions ? *transferOptions : config::FileTransferOptions{};
        storage::FileIoContext ioContext(options.fileIo);
        auto available = ioContext.validateAvailable();
        if (!available.isOk()) { failWriter(available); break; }
        auto advice = storage::applyFileIoAdvice(file,options.fileIo.advice,0,id.totalSize);
        if (!advice.isOk()) { failWriter(advice); break; }
        checksum::ChecksumComputer checksumComputer(id.checksumAlgorithm,options.checksumBackend);
        const auto pipelineChunkBytes = std::min<std::size_t>(
            std::max<std::size_t>(options.bufferSize, 1), PersistentDataSession::kMaxPayload - 4);
        const auto kPipelineChunks = pipelineDepth.value();
        timing.rangeObservationEnabled = observeRanges(*stats);
        timing.payloadPipelineDepth = kPipelineChunks;
        const auto laneCpuStarted = threadCpuSeconds(timing.rangeObservationEnabled);
        const auto pipelineBudget = pipelineChunkBytes * kPipelineChunks;
        BoundedPayloadPipeline pipeline(pipelineBudget, kPipelineChunks, timing.rangeObservationEnabled);
        stats->payloadPipelineBudgetBytes = std::max<std::uint64_t>(
            stats->payloadPipelineBudgetBytes, pipelineBudget);
        std::uint64_t payloadSent = 0;
        std::uint64_t producedBytes = 0;
        double producerReadSeconds = 0;
        double producerChecksumSeconds = 0;
        Status producerStatus = Status::ok();
        std::thread producer([&]() {
            const auto cpuStarted = threadCpuSeconds(timing.rangeObservationEnabled);
            struct CpuSample { PersistentFileTiming& timing; double started;
                ~CpuSample() { timing.producerThreadCpuSeconds += threadCpuSeconds(timing.rangeObservationEnabled)-started; }
            } cpuSample{timing,cpuStarted};
            std::vector<std::uint8_t> producerBuffer;
            for (const auto& piece : missing) {
                for (std::uint64_t offset = piece.begin; offset < piece.end;) {
                    const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(
                        pipelineChunkBytes,
                        std::min<std::uint64_t>(piece.end - offset,
                            id.chunkSize - offset % id.chunkSize)));
                    if (options.maxChunks && producedBytes >= options.maxChunks * id.chunkSize) {
                        producerStatus = Status::runtimeError("transfer interrupted by max-chunks");
                        pipeline.cancel(producerStatus);
                        return;
                    }
                    producerBuffer.resize(length);
                    const auto reading = Clock::now();
                    producerStatus = storage::readAtAll(file, offset, producerBuffer.data(), length,
                        ioContext, &stats->fileIoStats);
                    producerReadSeconds += secondsSince(reading);
                    if (!producerStatus.isOk()) {
                        pipeline.cancel(producerStatus);
                        return;
                    }
                    const auto checksumStarted = Clock::now();
                    checksumComputer.update(producerBuffer.data(), length);
                    checksum::ChecksumComputer frameChecksum(id.checksumAlgorithm,
                        options.checksumBackend);
                    frameChecksum.update(producerBuffer.data(), length);
                    const auto frameDigest = frameChecksum.finalize().value;
                    producerChecksumSeconds += secondsSince(checksumStarted);
                    const auto chunkIndex = offset / id.chunkSize;
                    if (options.hasCorruptChunk && chunkIndex == options.corruptChunk)
                        producerBuffer[0] ^= 0xffU;
                    PreparedPayload prepared;
                    prepared.offset = offset;
                    prepared.chunkIndex = chunkIndex;
                    prepared.frameChecksum = frameDigest;
                    prepared.bytes = std::move(producerBuffer);
                    producerStatus = pipeline.push(std::move(prepared));
                    if (!producerStatus.isOk()) return;
                    producedBytes += length;
                    offset += length;
                }
                if (!producerStatus.isOk()) return;
            }
            pipeline.close();
        });
        for (;;) {
            auto next = pipeline.pop();
            if (!next.isOk()) {
                status = next.status();
                writeFailed = true;
                break;
            }
            if (!next.value()) break;
            auto prepared = std::move(*next.value());
            const auto offset = prepared.offset;
            const auto length = prepared.bytes.size();
            const auto frameDigest = prepared.frameChecksum;
            const auto chunkIndex = prepared.chunkIndex;
            checkpoint::OptionalManifestTimer preparing(timing.rangeObservationEnabled ? &timing.framePrepareSeconds : nullptr);
            std::vector<std::uint8_t> compressed;
            auto* bytes = prepared.bytes.data();
            std::size_t wireLength = length;
            std::uint16_t flags = 0;
            auto& runtime = stats->runtime;
            if (options.hotPathCompression.enabled && options.hotPathCompression.candidate &&
                !options.hotPathCompression.forceRaw) {
                ++runtime.compressionAttempts;
                ++timing.compressionAttempts;
                auto encoded = options.hotPathCompression.testForceCompressFailure
                    ? common::Result<std::vector<std::uint8_t>>(Status::runtimeError("compression_failure"))
                    : protocol::encodeCompressedDataPayload(bytes, length,
                        options.hotPathCompression.maxPayloadBytes
                            ? std::min(options.bufferSize, options.hotPathCompression.maxPayloadBytes)
                            : options.bufferSize);
                if (encoded.isOk() && encoded.value().size() < length) {
                    compressed = std::move(encoded.value());
                    bytes = compressed.data();
                    wireLength = compressed.size();
                    flags = protocol::kDataCompressed;
                    ++runtime.compressedFrames;
                    ++timing.compressedFrames;
                    runtime.compressedLogicalBytes += length;
                    runtime.compressedWireBytes += wireLength;
                    if (options.hotPathCompression.testCorruptCompressedPayload &&
                        wireLength > protocol::kCompressedDataPrefixSize)
                        compressed[protocol::kCompressedDataPrefixSize] ^= 0xffU;
                } else {
                    ++runtime.rawFallbackFrames;
                    ++timing.rawFallbackFrames;
                    runtime.compressionFallbackReason = encoded.isOk() ? "poor_ratio" : "compression_failure";
                    if (!encoded.isOk()) {
                        ++runtime.compressionFailures;
                        ++timing.compressionFailures;
                    }
                }
            }
            std::vector<std::uint8_t> protectedPayload;
            if (id.checksumAlgorithm != checksum::ChecksumAlgorithm::None) {
                protectedPayload = {static_cast<std::uint8_t>(frameDigest >> 24),
                    static_cast<std::uint8_t>(frameDigest >> 16),
                    static_cast<std::uint8_t>(frameDigest >> 8),
                    static_cast<std::uint8_t>(frameDigest)};
                protectedPayload.insert(protectedPayload.end(), bytes, bytes + wireLength);
                bytes = protectedPayload.data();
                wireLength = protectedPayload.size();
                flags |= protocol::kDataChecksum;
            }
            protocol::FrameHeader header;
            header.type = FrameType::Data;
            header.streamId = id.fileId;
            header.chunkId = id.generation;
            header.rangeId = id.rangeId;
            header.attempt = id.attempt;
            header.offset = offset;
            header.totalSize = id.totalSize;
            header.payloadSize = wireLength;
            header.flags = flags;
            auto encoded = protocol::encodeFrameHeader(header);
            preparing.stop();
            const auto payloadStarted = Clock::now();
            auto writePayload = [&](const std::uint8_t* payload, std::size_t size) {
                const auto frameBytes = encoded.size() + size;
                if (ready) {
                    auto reserved = executor->startIo(*lease, frameBytes);
                    if (!reserved.isOk()) return reserved;
                }
                checkpoint::OptionalManifestTimer socketWriting(timing.rangeObservationEnabled ? &timing.socketWriteSeconds : nullptr);
                if (timing.rangeObservationEnabled) ++timing.socketWriteCalls;
                auto written = socket->writeAll(encoded.data(), encoded.size());
                if (written.isOk()) written = socket->writeAll(payload, size);
                socketWriting.stop();
                if (written.isOk() && ready) written = executor->finishIo(*lease, frameBytes);
                return written;
            };
            status = writePayload(bytes, wireLength);
            if (status.isOk() && options.hasDuplicateCorruptChunk &&
                chunkIndex == options.duplicateCorruptChunk) {
                auto duplicate = prepared.bytes;
                duplicate[0] ^= 0xffU;
                status = writePayload(duplicate.data(), length);
            }
            timing.payloadIoSeconds += secondsSince(payloadStarted);
            if (status.isOk() && !timing.firstPayloadSeconds)
                timing.firstPayloadSeconds = secondsSince(fileStarted);
            if (!status.isOk()) {
                writeFailed = true;
                break;
            }
            payloadSent += length;
            stats->payloadBytes += length;
            addWire(protocol::kFrameHeaderSize + wireLength);
        }
        if (writeFailed)
            pipeline.cancel(status.isOk() ? Status::runtimeError("persistent data write failed") : status);
        producer.join();
        timing.laneThreadCpuSeconds = threadCpuSeconds(timing.rangeObservationEnabled)-laneCpuStarted;
        timing.pipelineBudgetBytes = pipelineBudget;
        timing.pipelineHighWatermarkBytes = pipeline.highWatermarkBytes();
        timing.pipelineProducerWaits = pipeline.producerWaitCount();
        timing.pipelineConsumerWaits = pipeline.consumerWaitCount();
        timing.pipelineProducerWaitSeconds = pipeline.producerWaitSeconds();
        timing.pipelineConsumerWaitSeconds = pipeline.consumerWaitSeconds();
        timing.readSeconds += producerReadSeconds;
        timing.checksumSeconds += producerChecksumSeconds;
        stats->payloadPipelineHighWatermarkBytes = std::max<std::uint64_t>(
            stats->payloadPipelineHighWatermarkBytes, pipeline.highWatermarkBytes());
        stats->payloadPipelineProducerWaits += pipeline.producerWaitCount();
        stats->payloadPipelineConsumerWaits += pipeline.consumerWaitCount();
        if (!writeFailed && !producerStatus.isOk()) {
            status = producerStatus;
            writeFailed = true;
        }
        const auto expectedBytes=id.rangeId ? id.rangeLength : id.totalSize;
        std::uint64_t requestedBytes = 0;
        for (const auto& piece : missing) requestedBytes += piece.end - piece.begin;
        // Bytes not sent because a write failed are not verified/skipped bytes.
        stats->skippedBytes += expectedBytes - requestedBytes;
        struct stat after{};
        if (!writeFailed && (::fstat(file.fd(), &after) != 0 || !sourceMatches(after, id) ||
            before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
            before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)) {
            status = Status::runtimeError("persistent source changed during transfer");
            writeFailed = true;
        }
        if (!writeFailed) status = PersistentDataSession::writeEnd(socket, id,
            skipRange ? checksum::ChecksumValue{} : checksumComputer.finalize(), skipRange);
        if (writeFailed || !status.isOk()) { failWriter(status.isOk() ? Status::runtimeError("persistent data write failed") : status); break; }
        if (ready) {
            status = executor->awaitResult(*lease);
            if (!status.isOk()) { failWriter(status); break; }
        }
        if (needsHandshake || socket->tlsEnabled()) {
            const auto resultStarted = Clock::now();
            auto result = PersistentDataSession::readResult(socket, id);
            if (!result.isOk()) { failWriter(result.status()); break; }
            const double resultSeconds = secondsSince(resultStarted);
            const auto rangeStatus = receiverFileStatus(result.value(), id.relativePath);
            if (ready) {
                auto completed = result.value() == FrameStatusCode::OutputExists
                    ? executor->failTerminal(*lease, rangeStatus)
                    : executor->finalize(*lease, rangeStatus);
                if (!completed.isOk()) { failWriter(completed); break; }
            }
            stats->completeWaitSeconds += resultSeconds;
            {
                std::lock_guard<std::mutex> lock(mutex);
                readerWireBytes += 2 * protocol::kFrameHeaderSize;
            }
            const bool logicalComplete = id.rangeId == 0 || id.rangeId == id.rangeCount;
            if (logicalComplete) {
                ++stats->files;
                if (rangeStatus.isOk()) stats->bytes += id.totalSize;
            }
            if (rangeStatus.isOk() && id.rangeId != 0) ++stats->rangeCompletions;
            if (!rangeStatus.isOk() && overall.isOk()) overall = rangeStatus;
            const auto callbackStarted = Clock::now();
            const auto callbackStatus = notifySafe(id, rangeStatus, true);
            timing.manifestSeconds += secondsSince(callbackStarted);
            timing.fileResultSeconds = resultSeconds;
            timing.wallSeconds = secondsSince(fileStarted);
            if (stats->phaseTiming) stats->fileTimings.push_back(std::move(timing));
            if (!callbackStatus.isOk()) { failWriter(callbackStatus); break; }
            if (!rangeStatus.isOk()) { failWriter(rangeStatus); break; }
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending.push_back({id, std::move(timing), fileStarted, Clock::now(), lease});
            stats->pendingHighWatermark = std::max<std::uint32_t>(
                stats->pendingHighWatermark, static_cast<std::uint32_t>(pending.size()));
        }
        changed.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        writerDone = true;
        changed.notify_all();
    }
    resultReader.join();
    Status finalStatus;
    {
        std::lock_guard<std::mutex> lock(mutex);
        finalStatus = readerStatus;
        if (finalStatus.isOk() && !pending.empty())
            finalStatus = Status::runtimeError("persistent pending result queue not drained");
    }
    if (socket->valid() && finalStatus.isOk()) {
        status = PersistentDataSession::writeDirectoryEnd(
            socket, stats->files - filesAtConnectionStart);
        addWire(protocol::kFrameHeaderSize);
        if (!status.isOk()) finalStatus = status;
    }
    stats->wireBytes += writerWireBytes + readerWireBytes;
    if (!finalStatus.isOk() && ready) {
        const auto owner = executor->owner();
        auto leases = ready->active(owner);
        if (!leases.isOk()) return leases.status();
        if (finalStatus.code() == common::StatusCode::TransportError) {
            auto disconnected = ready->disconnect(owner);
            if (!disconnected.isOk()) return disconnected;
            std::vector<WorkRecoveryDecision> decisions;
            decisions.reserve(leases.value().size());
            for (const auto& lease : leases.value())
                decisions.push_back({lease, WorkRecoveryDisposition::Missing});
            auto recovered = executor->recover(decisions, maxRetries);
            if (!recovered.isOk()) return recovered.status();
        } else {
            ready->cancel(finalStatus);
        }
    }
    if (!finalStatus.isOk()) return finalStatus;
    return overall;
}

common::Status receivePersistentTree(FramedDataSocket* socket, const std::string& root,
    bool download, const std::string& remoteRoot, PersistentTreeStats* stats,
    const PersistentFileCallback& callback, std::uint32_t pendingWindow,
    std::uint32_t channelIndex, std::uint32_t channelCount,
    std::uint32_t expectedFileCount, bool dynamic, bool resume,
    std::shared_ptr<PersistentTransferContext> context, DynamicFileQueue* ready,
    std::uint32_t maxRetries) {
    if (!stats) return Status::invalidArgument("missing persistent stats");
    if (!socket || !socket->valid()) return Status::invalidArgument("invalid persistent data socket");
    if (pendingWindow == 0 || pendingWindow > 16)
        return Status::invalidArgument("persistent pending window must be in range 1..16");
    if (channelCount == 0 || channelCount > kUnifiedFileMaxChannels ||
        channelIndex >= channelCount)
        return Status::invalidArgument("persistent channel index/count is invalid");
    stats->pendingWindow = pendingWindow; // Receiver stays serial; report negotiated sender credit.
    const std::uint64_t filesAtConnectionStart = stats->files;
    auto status = PersistentDataSession::setTimeout(socket, 30);
    if (!status.isOk()) return status;
    if (!context) context = makePersistentTransferContext();
    PersistentDataSession state(dynamic);
    std::optional<WorkExecutor> executor;
    if (ready) {
        auto owner = ready->worker(channelIndex);
        if (!owner.isOk()) return owner.status();
        executor.emplace(*ready, owner.value());
    }
    Status overall = Status::ok();
    bool transportFailure = false;
    bool transportReconciled = false;
    auto finishTransport = [&](Status result) -> Status {
        if (result.code() != common::StatusCode::TransportError) return result;
        const auto integrity = verifyCommittedPersistentOutputs(context);
        if (!integrity.isOk()) return integrity;
        if (!ready || !executor || transportReconciled) return result;
        transportFailure = true;
        transportReconciled = true;
        const auto owner = executor->owner();
        auto leases = ready->active(owner);
        if (!leases.isOk()) return leases.status();
        auto disconnected = ready->disconnect(owner);
        if (!disconnected.isOk()) return disconnected;
        std::vector<WorkRecoveryDecision> decisions;
        decisions.reserve(leases.value().size());
        for (const auto& lease : leases.value())
            decisions.push_back({lease, WorkRecoveryDisposition::Missing});
        auto recovered = executor->recover(decisions, maxRetries);
        if (!recovered.isOk()) return recovered.status();
        return result;
    };
    for (;;) {
        auto frame = PersistentDataSession::readNext(socket, PersistentDataSession::kMaxPayload);
        if (!frame.isOk()) return finishTransport(frame.status());
        stats->wireBytes += protocol::kFrameHeaderSize + frame.value().payload.size();
        if (frame.value().header.type == FrameType::DirectoryEnd) {
            const auto filesOnConnection = stats->files - filesAtConnectionStart;
            if (frame.value().header.totalSize != filesOnConnection)
                return Status::invalidArgument("persistent directory file count mismatch");
            if (expectedFileCount != kUnknownPersistentFileCount &&
                expectedFileCount != filesOnConnection)
                return Status::invalidArgument("persistent channel file count mismatch");
            return finishTransport(overall);
        }
        auto decoded = PersistentDataSession::decodeBegin(frame.value());
        if (!decoded.isOk()) return decoded.status();
        const auto id = decoded.value();
        std::optional<WorkLease> lease;
        struct IncomingLeaseGuard {
            WorkExecutor* executor = nullptr;
            std::optional<WorkLease>* lease = nullptr;
            bool finished = false;
            bool* transportFailure = nullptr;
            ~IncomingLeaseGuard() {
                if (executor && lease && lease->has_value() && !finished &&
                    !(transportFailure && *transportFailure))
                    (void)executor->reject(**lease);
            }
        } leaseGuard;
        if (ready) {
            auto claimed = executor->claimIncoming(WorkItem{id, id.transferId});
            if (!claimed.isOk()) return claimed.status();
            lease = claimed.value();
            leaseGuard.executor = &*executor;
            leaseGuard.lease = &lease;
            leaseGuard.transportFailure = &transportFailure;
            status = executor->begin(*lease);
            if (!status.isOk()) return finishTransport(status);
        }
        const auto assignedChannel = id.rangeId != 0
            ? static_cast<std::uint32_t>((id.rangeId - 1) % channelCount)
            : (id.fileId - 1) % channelCount;
        if (!dynamic && assignedChannel != channelIndex)
            return Status::invalidArgument("persistent file arrived on wrong channel");
        status = state.begin(frame.value().header, id);
        if (!status.isOk()) return finishTransport(status);
        PersistentFileTiming timing;
        timing.file = id; timing.channel = channelIndex;
        timing.rangeObservationEnabled = observeRanges(*stats);
        auto* observation = timing.rangeObservationEnabled ? &timing.rangeObservation : nullptr;
        const auto laneCpuStarted = threadCpuSeconds(timing.rangeObservationEnabled);
        const auto fileStarted = Clock::now();
        const auto manifestStarted = Clock::now();
        status = notify(callback, id, Status::ok(), false);
        timing.manifestSeconds += secondsSince(manifestStarted);
        if (!status.isOk()) return finishTransport(status);
        std::shared_ptr<RangeFileCoordinator::Entry> rangeEntry;
        struct RangeExitFlush {
            std::shared_ptr<RangeFileCoordinator::Entry>& entry;
            RangeObservation* observation;
            bool armed = true;
            ~RangeExitFlush() { if (armed) (void)RangeFileCoordinator::flush(entry, observation); }
        } rangeExitFlush{rangeEntry, observation};
        std::shared_ptr<FileTransaction> transaction;
        Status fileStatus = Status::ok();
        if (id.rangeId != 0) {
            auto started = RangeFileCoordinator::begin(*context, root, id, download, remoteRoot, resume, observation);
            if (!started.isOk()) {
                // Write the semantic rejection before callbacks can cancel sibling sockets.
                auto rejected = PersistentDataSession::writeResumeResponse(socket,id,{},fileFailureCode(started.status()));
                (void)notify(callback,id,started.status(),true);
                return finishTransport(rejected.isOk() ? started.status() : rejected);
            } else {
                rangeEntry = std::move(started.value());
                const auto rangeStats = RangeFileCoordinator::takeStats(rangeEntry);
                stats->loadedVerifiedChunks += rangeStats.first;
                stats->removedCorruptChunks += rangeStats.second;
                const auto missing=RangeFileCoordinator::missingRanges(rangeEntry,id);
                std::uint64_t missingBytes = 0;
                for (const auto& range : missing) missingBytes += range.end - range.begin;
                stats->skippedBytes += id.rangeLength - missingBytes;
                status=state.setMissingRanges(missing);
                if (!status.isOk()) return finishTransport(status);
                status = PersistentDataSession::writeResumeResponse(socket, id, missing);
                if (!status.isOk()) fileStatus = status;
            }
        } else {
            transaction = std::make_shared<FileTransaction>(stats->phaseTiming ? &timing : nullptr,context->policy,&stats->fileIoStats);
            {
                std::lock_guard lock(context->mutex);
                context->integrityChecks.emplace_back([transaction] {
                    return transaction->verifyPublished();
                });
            }
            const bool requestedResume = resume || (frame.value().header.flags & protocol::kFileResume);
            fileStatus = transaction->begin(root, id, download, remoteRoot, requestedResume);
            if (fileStatus.isOk()) {
                stats->loadedVerifiedChunks += transaction->loadedVerifiedChunks();
                stats->removedCorruptChunks += transaction->removedCorruptChunks();
            }
            if (frame.value().header.flags & protocol::kFileResume) {
                if (!fileStatus.isOk()) {
                    (void)PersistentDataSession::writeResumeResponse(socket,id,{},fileFailureCode(fileStatus));
                    return fileStatus;
                }
                auto missing = transaction->missingRanges();
                std::uint64_t missingBytes = 0;
                for (const auto& range : missing) missingBytes += range.end - range.begin;
                stats->skippedBytes += id.totalSize - missingBytes;
                status = state.setMissingRanges(missing);
                if (!status.isOk()) return finishTransport(status);
                status = PersistentDataSession::writeResumeResponse(socket,id,missing);
                if (!status.isOk()) return finishTransport(status);
            }
        }
        for (;;) {
            const auto payloadStarted = Clock::now();
            auto data = PersistentDataSession::readNext(socket, PersistentDataSession::kMaxPayload);
            if (!data.isOk()) {
                (void)notify(callback, id, data.status(), true);
                return finishTransport(data.status());
            }
            stats->wireBytes += protocol::kFrameHeaderSize + data.value().payload.size();
            if (data.value().header.type == FrameType::FileEnd) {
                auto ended = state.end(data.value().header, data.value().payload.data(), data.value().payload.size());
                if (!ended.isOk()) return ended.status();
                if (fileStatus.isOk() && rangeEntry) {
                    Timer finalized{&timing.finalizeSeconds};
                    fileStatus = RangeFileCoordinator::complete(rangeEntry, ended.value(), ended.value().checksum,
                        (data.value().header.flags & protocol::kRangeSkipped) != 0, observation);
                }
                break;
            }
            timing.payloadIoSeconds += secondsSince(payloadStarted);
            if (!timing.firstPayloadSeconds) timing.firstPayloadSeconds = secondsSince(fileStarted);
            const auto checksumStarted = Clock::now();
            std::vector<std::uint8_t> logical = data.value().payload;
            std::optional<std::uint32_t> expectedDigest;
            if (data.value().header.flags & protocol::kDataChecksum) {
                if (id.checksumAlgorithm==checksum::ChecksumAlgorithm::None || logical.size()<=4)
                    return Status::invalidArgument("unexpected DATA checksum");
                expectedDigest=(static_cast<std::uint32_t>(logical[0])<<24)|
                    (static_cast<std::uint32_t>(logical[1])<<16)|
                    (static_cast<std::uint32_t>(logical[2])<<8)|logical[3];
                logical.erase(logical.begin(),logical.begin()+4);
            }
            if (data.value().header.flags & protocol::kDataCompressed) {
                auto size = protocol::compressedDataPayloadLogicalLength(logical.data(),logical.size());
                if (!size.isOk()) return size.status();
                auto decoded = protocol::decodeCompressedDataPayload(logical.data(),logical.size(),
                    size.value(),PersistentDataSession::kMaxPayload);
                if (!decoded.isOk()) { ++stats->runtime.decompressionFailures; return decoded.status(); }
                ++timing.compressedFrames;
                logical = std::move(decoded.value());
                ++stats->runtime.compressedFrames;
                stats->runtime.compressedLogicalBytes += logical.size();
                stats->runtime.compressedWireBytes += data.value().payload.size();
            }
            if (id.checksumAlgorithm!=checksum::ChecksumAlgorithm::None && !expectedDigest)
                return Status::invalidArgument("DATA checksum is required before checkpointing");
            if (expectedDigest) {
                checksum::ChecksumComputer checksum(id.checksumAlgorithm);
                checksum.update(logical.data(),logical.size());
                if (checksum.finalize().value!=*expectedDigest)
                    return Status::invalidArgument("DATA checksum mismatch before write");
            }
            auto logicalHeader=data.value().header;
            logicalHeader.flags=0;logicalHeader.payloadSize=logical.size();
            status = state.data(logicalHeader, logical.size(), logical.data());
            timing.checksumSeconds += secondsSince(checksumStarted);
            if (!status.isOk()) return finishTransport(status);
            if (fileStatus.isOk()) {
                Timer writing{rangeEntry ? &timing.writeSeconds : nullptr};
                fileStatus = rangeEntry ? RangeFileCoordinator::write(rangeEntry, id, data.value().header.offset,
                                                                       logical, observation)
                                         : transaction->write(data.value().header.offset, logical);
                stats->payloadBytes += logical.size();
                if (context->policy.options.maxChunks &&
                    stats->payloadBytes >= context->policy.options.maxChunks*id.chunkSize)
                    return Status::runtimeError("transfer interrupted by max-chunks");
            }
        }
        if (rangeEntry) {
            const auto checkpoint = RangeFileCoordinator::flush(rangeEntry, observation);
            if (!checkpoint.isOk()) fileStatus = checkpoint;
            rangeExitFlush.armed = false; // do not flush another lane after our result
        }
        if (fileStatus.isOk() && transaction) {
            Timer timer{&timing.finalizeSeconds};
            fileStatus = transaction->commit();
        }
        const bool logicalFileComplete = id.rangeId == 0 || id.rangeId == id.rangeCount;
        if (logicalFileComplete) {
            ++stats->files;
            if (fileStatus.isOk()) stats->bytes += id.totalSize;
        }
        if (!fileStatus.isOk() && overall.isOk()) overall = fileStatus;
        const auto completedStarted = Clock::now();
        const bool terminalConflict = dynamic &&
            fileFailureCode(fileStatus) == FrameStatusCode::OutputExists;
        if (terminalConflict) {
            // Dynamic batches stop on semantic failure. Publish the rejection
            // before callbacks cancel sockets; static receivers keep draining.
            const auto rejected = PersistentDataSession::writeResult(
                socket, id, FrameStatusCode::OutputExists);
            (void)notify(callback, id, fileStatus, true);
            return rejected.isOk() ? fileStatus : rejected;
        }
        status = notify(callback, id, fileStatus, true);
        if (fileStatus.isOk() && id.rangeId != 0) ++stats->rangeCompletions;
        timing.manifestSeconds += secondsSince(completedStarted);
        if (!status.isOk()) return finishTransport(status);
        const auto resultStarted = Clock::now();
        status = PersistentDataSession::writeResult(socket, id, fileFailureCode(fileStatus));
        if (!status.isOk()) return finishTransport(status);
        if (ready) {
            status = !fileStatus.isOk() && !dynamic
                ? executor->failTerminal(*lease, fileStatus)
                : executor->finalize(*lease, fileStatus);
            if (!status.isOk()) return finishTransport(status);
            leaseGuard.finished = true;
        }
        stats->wireBytes += protocol::kFrameHeaderSize;
        timing.laneThreadCpuSeconds = threadCpuSeconds(timing.rangeObservationEnabled)-laneCpuStarted;
        timing.fileResultSeconds = secondsSince(resultStarted);
        timing.wallSeconds = secondsSince(fileStarted);
        if (stats->phaseTiming) stats->fileTimings.push_back(std::move(timing));
    }
}
}  // namespace cpnetflux::core::io

namespace cpnetflux::core::io {
namespace {
void quoted(std::ostream& out, const std::string& value) {
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) {
            constexpr char digits[] = "0123456789abcdef";
            out << "\\u00" << digits[c>>4] << digits[c&15];
        } else out << c;
    }
    out << '"';
}
void optionalSeconds(std::ostream& out, const std::optional<double>& value) {
    if (value) out << *value; else out << "null";
}
}
void appendPersistentFileTimings(std::ostream& out, const std::vector<PersistentFileTiming>& files) {
    out << std::setprecision(17) << '[';
    bool first = true;
    for (const auto& t : files) {
        if (!first) out << ',';
        first = false;
        out << "{\"file_id\":" << t.file.fileId << ",\"generation\":" << t.file.generation
            << ",\"chunk_size\":" << t.file.chunkSize << ",\"size\":" << t.file.totalSize << ",\"channel_index\":" << t.channel << ",\"relative_path\":";
        quoted(out,t.file.relativePath);
        out << ",\"transfer_id\":"; quoted(out,t.file.transferId);
        out << ",\"range_id\":" << t.file.rangeId << ",\"attempt\":" << t.file.attempt
            << ",\"offset\":" << t.file.rangeOffset << ",\"length\":" << t.file.rangeLength;
        out << ",\"measurement_scope\":\"" << (t.sender ? "local_sender" : "local_receiver")
            << "\",\"queue_wait_seconds\":"; optionalSeconds(out,t.queueWaitSeconds);
        out << ",\"first_payload_seconds\":"; optionalSeconds(out,t.firstPayloadSeconds);
        out << ",\"read_seconds\":";
        if (t.sender) out << t.readSeconds; else out << "null";
        out << ",\"write_seconds\":";
        if (!t.sender) out << t.writeSeconds; else out << "null";
        out << ",\"first_payload_scope\":\"first_complete_application_payload_frame_relative_to_file_start\""
            << ",\"file_result_scope\":\"" << (t.sender ? "file_end_to_valid_result_received" : "result_frame_write")
            << "\",\"manifest_scope\":\"checkpoint_and_file_state_callback\""
            << ",\"compression_attempts\":" << t.compressionAttempts
            << ",\"compressed_frames\":" << t.compressedFrames
            << ",\"raw_fallback_frames\":" << t.rawFallbackFrames
            << ",\"compression_failures\":" << t.compressionFailures
            << ",\"finalize_scope\":" << (t.sender ? "null" : "\"receiver_commit_includes_manifest_flush\"")
            << ",\"payload_io_seconds\":" << t.payloadIoSeconds
            << ",\"file_result_seconds\":" << t.fileResultSeconds
            << ",\"manifest_seconds\":" << t.manifestSeconds << ",\"checksum_seconds\":"
            << t.checksumSeconds << ",\"finalize_seconds\":";
        if (!t.sender) out << t.finalizeSeconds; else out << "null";
        out << ",\"range_observation_enabled\":" << (t.rangeObservationEnabled ? "true" : "false")
            << ",\"range_observation_scope\":\"local_range_begin_write_complete_subintervals_overlap_not_wall\""
            << ",\"lane_thread_cpu_seconds\":" << t.laneThreadCpuSeconds
            << ",\"lane_thread_cpu_scope\":\"" << (t.sender ? "payload_prepare_send_and_join" : "file_begin_through_result") << "\""
            << ",\"producer_thread_cpu_seconds\":" << t.producerThreadCpuSeconds
            << ",\"frame_prepare_seconds\":" << t.framePrepareSeconds
            << ",\"socket_write_seconds\":" << t.socketWriteSeconds
            << ",\"socket_write_frame_calls\":" << t.socketWriteCalls
            << ",\"socket_write_scope\":\"DATA_header_and_payload_writeAll_includes_TLS_when_enabled\""
            << ",\"pipeline_wait_scope\":\"condition_wait_through_mutex_reacquire_subintervals_not_wall\""
            << ",\"write_seconds_scope\":\"" << (t.file.rangeId ? "range_coordinator_includes_entry_wait_crc_checkpoint" : "writer_write_only") << "\""
            << ",\"payload_pipeline_depth\":" << t.payloadPipelineDepth
            << ",\"pipeline_budget_bytes\":" << t.pipelineBudgetBytes
            << ",\"pipeline_high_watermark_bytes\":" << t.pipelineHighWatermarkBytes
            << ",\"pipeline_producer_waits\":" << t.pipelineProducerWaits
            << ",\"pipeline_consumer_waits\":" << t.pipelineConsumerWaits
            << ",\"pipeline_producer_wait_seconds\":" << t.pipelineProducerWaitSeconds
            << ",\"pipeline_consumer_wait_seconds\":" << t.pipelineConsumerWaitSeconds;
        const auto& r = t.rangeObservation;
        const auto& m = r.manifestSave;
        out << ",\"range_observation\":{\"entry_lock_calls\":" << r.entryLockCalls
            << ",\"entry_lock_wait_seconds\":" << r.entryLockWaitSeconds
            << ",\"entry_lock_hold_seconds\":" << r.entryLockHoldSeconds
            << ",\"offset_write_calls\":" << r.offsetWriteCalls
            << ",\"offset_write_seconds\":" << r.offsetWriteSeconds
            << ",\"chunk_flush_calls\":" << r.chunkFlushCalls
            << ",\"chunk_flush_seconds\":" << r.chunkFlushSeconds
            << ",\"chunk_checksum_seconds\":" << r.chunkChecksumSeconds
            << ",\"checkpoint_seconds\":" << r.checkpointSeconds
            << ",\"background_checkpoint_cpu_seconds\":" << r.backgroundCheckpointCpuSeconds
            << ",\"manifest_save\":{\"attempts\":" << m.attempts
            << ",\"serialized_bytes\":" << m.serializedBytes
            << ",\"rename_calls\":" << m.renameCalls
            << ",\"failures\":" << m.failures
            << ",\"serialize_seconds\":" << m.serializeSeconds
            << ",\"temp_write_seconds\":" << m.tempWriteSeconds
            << ",\"rename_seconds\":" << m.renameSeconds << "}}";
        out << ",\"wall_seconds\":" << t.wallSeconds << '}';
    }
    out << ']';
}
}  // namespace cpnetflux::core::io
