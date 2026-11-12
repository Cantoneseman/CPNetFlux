#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/core/io/payload_pipeline.h"
#include <cstdlib>
#include <ctime>
#include <charconv>
#include <string_view>
#include <span>
#include "cpnetflux/core/io/work_executor.h"
#include "cpnetflux/core/io/detail/persistent_transfer_lifetime.h"
#include "cpnetflux/core/io/detail/sender_pacing.h"
#include "cpnetflux/core/io/detail/diagnostic_output.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <variant>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <semaphore>
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
    std::vector<PersistentTreeFileMetadata> publishedFiles;
    PersistentTransferPolicy policy;
    std::shared_ptr<TransferStages> stages;
    std::shared_ptr<RangeCheckpointQueue> checkpoints; // destroyed/joined before files
};
std::shared_ptr<PersistentTransferContext> makePersistentTransferContext(const PersistentTransferPolicy& policy) {
    auto context = std::make_shared<PersistentTransferContext>();
    context->policy = policy;
    return context;
}
std::vector<PersistentTreeFileMetadata> persistentTreePublishedFiles(
    const std::shared_ptr<PersistentTransferContext>& context) {
    if (!context) return {};
    std::lock_guard lock(context->mutex);
    return context->publishedFiles;
}
namespace {
template<class... Args>
void emitStderrObservation(const char* format, Args... args) noexcept {
    std::array<char, 1024> buffer{};
    const int size = std::snprintf(buffer.data(), buffer.size(), format, args...);
    if (size > 0 && static_cast<std::size_t>(size) < buffer.size())
        detail::emitDiagnosticRecord(stderr, std::string_view(buffer.data(), size));
}

using common::Status;
using protocol::FrameStatusCode;
using protocol::FrameType;
namespace fs = std::filesystem;
using Session = std::variant<session::TransferSession, session::DownloadSession>;
using Clock = std::chrono::steady_clock;

Status setPersistentReceiveDeadline(FramedDataSocket* socket, Clock::time_point deadline) {
    if (socket->transport() == DataTransport::Rdma) return Status::ok();
    const auto remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero())
        return Status::runtimeError("V3 result/commit deadline exceeded");
    const auto micros = std::max<std::int64_t>(1,
        std::chrono::duration_cast<std::chrono::microseconds>(remaining).count());
    const timeval timeout{micros / 1000000, micros % 1000000};
    if (::setsockopt(socket->fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
        return Status::runtimeError("persistent result receive timeout configuration failed");
    return Status::ok();
}

void tracePersistentProtocol(const char* role, const char* event,
    std::uint32_t channelIndex, const PersistentFileIdentity* id = nullptr,
    std::uint64_t value = 0, std::uint64_t detail = 0, int resultCode = -1) noexcept {
    static const bool enabled = [] {
        const auto* setting = std::getenv("CPNETFLUX_V3_PROTOCOL_TRACE");
        return setting && std::string_view(setting) == "1";
    }();
    if (!enabled) return;
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
    emitStderrObservation(
        "CPNETFLUX_V3_PROTOCOL_TRACE {\"role\":\"%s\",\"event\":\"%s\","
        "\"monotonic_ns\":%lld,\"channel\":%u,\"file_id\":%u,"
        "\"generation\":%llu,\"range_id\":%llu,\"range_count\":%llu,"
        "\"attempt\":%llu,\"range_offset\":%llu,\"range_length\":%llu,"
        "\"value\":%llu,\"detail\":%llu,\"result_code\":%d}\n",
        role, event, static_cast<long long>(stamp), channelIndex,
        id ? id->fileId : 0, static_cast<unsigned long long>(id ? id->generation : 0),
        static_cast<unsigned long long>(id ? id->rangeId : 0),
        static_cast<unsigned long long>(id ? id->rangeCount : 0),
        static_cast<unsigned long long>(id ? id->attempt : 0),
        static_cast<unsigned long long>(id ? id->rangeOffset : 0),
        static_cast<unsigned long long>(id ? id->rangeLength : 0),
        static_cast<unsigned long long>(value), static_cast<unsigned long long>(detail),
        resultCode);
}

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
common::Result<ProducerWakePolicy> diagnosticReceiverWakePolicy() {
    const auto* setting = std::getenv("CPNETFLUX_RECEIVER_PIPELINE_WAKE");
    // Empty-queue wake batching is diagnostic only: the fixed-source crossover
    // reduced switches but regressed writer CPU and throughput.
    if (!setting || std::string_view(setting) == "available") return ProducerWakePolicy::AvailableCapacity;
    if (std::string_view(setting) == "empty") return ProducerWakePolicy::EmptyQueue;
    return Status::invalidArgument("CPNETFLUX_RECEIVER_PIPELINE_WAKE must be available or empty");
}
common::Result<std::size_t> diagnosticReceiverWriteBatch() {
    const auto* setting = std::getenv("CPNETFLUX_RECEIVER_WRITE_BATCH");
    if (!setting || std::string_view(setting) == "1") return std::size_t{1};
    if (std::string_view(setting) == "2") return std::size_t{2};
    return Status::invalidArgument("CPNETFLUX_RECEIVER_WRITE_BATCH must be 1 or 2");
}
common::Result<bool> diagnosticReceiverInlinePayload() {
    const auto* setting = std::getenv("CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION");
    if (!setting || std::string_view(setting) == "queued") return false;
    if (std::string_view(setting) == "inline") return true;
    return Status::invalidArgument("CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION must be queued or inline");
}
common::Result<bool> diagnosticSenderInlinePayload() {
    const auto* setting = std::getenv("CPNETFLUX_SENDER_PAYLOAD_EXECUTION");
    if (!setting || std::string_view(setting) == "queued") return false;
    if (std::string_view(setting) == "inline") return true;
    return Status::invalidArgument("CPNETFLUX_SENDER_PAYLOAD_EXECUTION must be queued or inline");
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
common::Result<std::uint64_t> rangeWriteBatchBytes(const PersistentTransferPolicy& policy) {
    constexpr std::uint64_t kMin = 64ULL * 1024ULL;
    constexpr std::uint64_t kMax = 16ULL * 1024ULL * 1024ULL;
    const auto validate = [&](std::uint64_t value) -> common::Result<std::uint64_t> {
        if (value == 0 || (value >= kMin && value <= kMax)) return value;
        return Status::invalidArgument("range write batch bytes must be off or in range 65536..16777216");
    };
    if (policy.rangeWriteBatchBytes) return validate(policy.rangeWriteBatchBytes);
    const auto* setting = std::getenv("CPNETFLUX_RANGE_WRITE_BATCH_BYTES");
    if (!setting || std::string_view(setting) == "off") return std::uint64_t{0};
    std::uint64_t value = 0;
    const auto text = std::string_view(setting);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return Status::invalidArgument("CPNETFLUX_RANGE_WRITE_BATCH_BYTES must be off or an integer byte count");
    return validate(value);
}
double threadCpuSeconds(bool enabled) {
    if (!enabled) return 0;
    timespec now{};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) return 0;
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec)*1e-9;
}
class OptionalThreadCpuTimer {
 public:
    explicit OptionalThreadCpuTimer(double* value)
        : value_(value), started_(threadCpuSeconds(value != nullptr)) {}
    ~OptionalThreadCpuTimer() { stop(); }
    void stop() {
        if (!value_) return;
        *value_ += threadCpuSeconds(true) - started_;
        value_ = nullptr;
    }
 private:
    double* value_;
    double started_;
};
class ObservedEntryLock {
 public:
    ObservedEntryLock(std::mutex& mutex, RangeObservation* observation)
        : observation_(observation), lock_(mutex, std::defer_lock) { lock(); }
    ~ObservedEntryLock() { accountHold(); }
    void unlock() { accountHold(); lock_.unlock(); }
    void lock() {
        const auto started = observation_ ? Clock::now() : Clock::time_point{};
        lock_.lock();
        if (observation_) {
            acquired_ = Clock::now();
            ++observation_->entryLockCalls;
            observation_->entryLockWaitSeconds += std::chrono::duration<double>(acquired_-started).count();
        }
    }
    template<class Predicate>
    void waitForReclaim(std::condition_variable& drained, Predicate predicate) {
        if (predicate()) return;
        accountHold();
        const auto started = observation_ ? Clock::now() : Clock::time_point{};
        drained.wait(lock_, predicate);
        if (observation_) {
            acquired_ = Clock::now();
            ++observation_->entryLockCalls;
            ++observation_->reclaimWaitCalls;
            observation_->reclaimWaitSeconds += std::chrono::duration<double>(acquired_-started).count();
        }
    }
 private:
    void accountHold() {
        if (observation_ && lock_.owns_lock())
            observation_->entryLockHoldSeconds += secondsSince(acquired_);
    }
    RangeObservation* observation_;
    std::unique_lock<std::mutex> lock_;
    Clock::time_point acquired_{};
};
// The shared transaction must never retain a lane's stack observation pointer.
class ScopedManifestObservation {
 public:
    ScopedManifestObservation(Session& session, RangeObservation* observation,
                              WholeFileObservation* whole = nullptr) : session_(session) {
        std::visit([&](auto& s) {
            s.setManifestSaveStats(observation ? &observation->manifestSave : whole ? &whole->manifestSave : nullptr);
            s.setRecordStats(whole ? &whole->records : nullptr);
        }, session_);
    }
    ~ScopedManifestObservation() {
        std::visit([](auto& s) { s.setManifestSaveStats(nullptr); s.setRecordStats(nullptr); }, session_);
    }
 private:
    Session& session_;
};
class FileTransaction {
 public:
    explicit FileTransaction(PersistentFileTiming* timing, const PersistentTransferPolicy& policy = {},
                             storage::FileIoStats* ioStats = nullptr,
                             PersistentTransferContext* context = nullptr)
        : timing_(timing), policy_(policy), ioContext_(policy.options.fileIo), ioStats_(ioStats),
          context_(context) {}
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
            const auto checkpointPath = download ? checkpoint::downloadManifestPathForOutput(output_)
                                                 : checkpoint::manifestPathForOutput(output_);
            const auto checkpointType = fs::symlink_status(checkpointPath, error);
            if (error || !fs::is_regular_file(checkpointType) || fs::is_symlink(checkpointType))
                return Status::invalidArgument("persistent checkpoint is not a safe regular file");
            std::optional<checkpoint::TransferManifest> uploadManifest;
            auto loaded = [&]() -> common::Result<checkpoint::DownloadManifest> {
                if (download) return checkpoint::loadDownloadManifest(checkpointPath);
                auto uploaded = checkpoint::ManifestStore::load(checkpointPath);
                if (!uploaded.isOk()) return uploaded.status();
                const auto& upload = uploaded.value();
                if (upload.transferId != id.transferId || upload.chunkSize != id.chunkSize)
                    return Status::invalidArgument("committed upload identity mismatch");
                checkpoint::DownloadManifest converted;
                converted.state = upload.state; converted.totalSize = upload.totalSize;
                converted.transferId = upload.transferId; converted.chunkSize = upload.chunkSize;
                converted.targetPath = upload.outputPath; converted.tempPath = upload.tempPath;
                converted.checksumAlgorithm = upload.checksumAlgorithm;
                converted.sourcePath = (fs::path(remoteRoot)/relative).lexically_normal().generic_string();
                converted.verifiedChunks = upload.verifiedChunks;
                uploadManifest = std::move(uploaded.value());
                return converted;
            }();
            if (!loaded.isOk()) return loaded.status();
            const auto& m = loaded.value();
            const bool reconcile = m.state == checkpoint::ManifestState::Transferring &&
                id.checksumAlgorithm == checksum::ChecksumAlgorithm::Crc32c;
            const auto expectedTemp = download ? checkpoint::downloadTempPathForOutput(output_, m.transferId)
                                               : checkpoint::tempPathForOutput(output_, m.transferId);
            if ((!reconcile && m.state != checkpoint::ManifestState::Committed) || m.totalSize != id.totalSize ||
                m.chunkSize != id.chunkSize || m.targetPath != output_ || m.tempPath != expectedTemp ||
                m.checksumAlgorithm != id.checksumAlgorithm || m.sourcePath !=
                    (fs::path(remoteRoot)/relative).lexically_normal().generic_string())
                return Status::invalidArgument("committed download identity mismatch");
            storage::PosixFile existingFile(UniqueFd(::open(output_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)));
            if (!existingFile.isValid()) return Status::runtimeError("open published output failed");
            struct stat existingMetadata{};
            if (::fstat(existingFile.fd(), &existingMetadata) != 0 || !S_ISREG(existingMetadata.st_mode) ||
                existingMetadata.st_size < 0 || static_cast<std::uint64_t>(existingMetadata.st_size) != id.totalSize ||
                existingMetadata.st_mtim.tv_sec != id.mtimeUnixSeconds)
                return Status::invalidArgument("committed output mtime mismatch");
            struct stat tempMetadata{};
            const bool aliasPresent = ::lstat(expectedTemp.c_str(), &tempMetadata) == 0;
            if ((!aliasPresent && errno != ENOENT) || (reconcile && !aliasPresent))
                return Status::invalidArgument("published checkpoint has no provable temp alias");
            const auto unchanged = [&](const struct stat& value) {
                return S_ISREG(value.st_mode) && value.st_dev == existingMetadata.st_dev &&
                    value.st_ino == existingMetadata.st_ino && value.st_size == existingMetadata.st_size &&
                    value.st_mtim.tv_sec == existingMetadata.st_mtim.tv_sec &&
                    value.st_mtim.tv_nsec == existingMetadata.st_mtim.tv_nsec &&
                    value.st_ctim.tv_sec == existingMetadata.st_ctim.tv_sec &&
                    value.st_ctim.tv_nsec == existingMetadata.st_ctim.tv_nsec;
            };
            // The saved state alone cannot prove that an existing target is our publication.
            const auto proveAlias = [&] {
                struct stat descriptor{}, output{}, temporary{};
                return ::fstat(existingFile.fd(), &descriptor) == 0 && unchanged(descriptor) &&
                    ::lstat(output_.c_str(), &output) == 0 && unchanged(output) &&
                    ::lstat(expectedTemp.c_str(), &temporary) == 0 && unchanged(temporary);
            };
            if (aliasPresent && !proveAlias())
                return Status::invalidArgument("published checkpoint temp/output identity mismatch");
            chunk::RangeList coverage;
            std::vector<std::uint8_t> checkBuffer(65536);
            for (const auto& record : m.verifiedChunks) {
                checksum::ChecksumComputer computer(id.checksumAlgorithm,policy_.options.checksumBackend);
                for (std::uint64_t pos=0;pos<record.length;) {
                    auto count = static_cast<std::size_t>(std::min<std::uint64_t>(checkBuffer.size(),record.length-pos));
                    auto read = existingFile.readAtAll(record.offset+pos,checkBuffer.data(),count);
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
            if (aliasPresent) {
                if (!proveAlias()) return Status::invalidArgument("published output changed during verification");
                if (policy_.options.commitSyncPolicy != session::CommitSyncPolicy::None) {
                    auto synced = existingFile.sync();
                    if (!synced.isOk()) return synced;
                }
                if (policy_.options.commitSyncPolicy == session::CommitSyncPolicy::FsyncFileAndDir) {
                    auto synced = storage::PosixFile::fsyncParentDirectory(output_);
                    if (!synced.isOk()) return synced;
                }
                if (!proveAlias()) return Status::invalidArgument("published output changed before reconciliation");
                if (reconcile) {
                    auto* whole = wholeObservation();
                    auto* saves = observation ? &observation->manifestSave : whole ? &whole->manifestSave : nullptr;
                    Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
                    Status saved;
                    if (download) {
                        auto committed = m;
                        committed.state = checkpoint::ManifestState::Committed;
                        committed.updatedAtUnixNanos = checkpoint::nowUnixNanos();
                        saved = checkpoint::saveDownloadManifestAtomic(checkpointPath, committed, saves);
                    } else {
                        uploadManifest->state = checkpoint::ManifestState::Committed;
                        uploadManifest->updatedAtUnixNanos = checkpoint::nowUnixNanos();
                        saved = checkpoint::ManifestStore::saveAtomic(checkpointPath, *uploadManifest, saves);
                    }
                    if (!saved.isOk()) return saved;
                }
                // Also handles a retry after committed save succeeded but unlink failed.
                if (!proveAlias()) return Status::invalidArgument("published output changed before temp retirement");
                if (::unlink(expectedTemp.c_str()) != 0)
                    return Status::runtimeError("published temp retirement failed");
            }
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
        ScopedManifestObservation observing(session_, observation, wholeObservation());
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
            const auto started = Clock::now();
            const auto cpu = threadCpuSeconds(timing_ != nullptr);
            auto allocated = file_.preallocate(id.totalSize);
            if (timing_) {
                ++timing_->preallocateCalls;
                timing_->preallocateRequestedBytes += id.totalSize;
                timing_->preallocateCpuSeconds += threadCpuSeconds(true) - cpu;
                timing_->preallocateSeconds += secondsSince(started);
            }
            if (!allocated.isOk()) return allocated;
        }
        writer_ = std::make_unique<storage::BufferedFileWriter>(file_,ioContext_,ioStats_);
        auto advice = storage::applyFileIoAdvice(file_,policy_.options.fileIo.advice,0,id.totalSize);
        if (!advice.isOk()) return advice;
        Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
        return resume ? common::Status::ok() : std::visit([](auto& s) { return s.save(); }, session_);
    }
    Status write(std::uint64_t offset, std::span<const std::uint8_t> bytes,
                 checksum::ChecksumValue verifiedChecksum) {
        ScopedManifestObservation observing(session_, nullptr, wholeObservation());
        if (skipped_) return Status::ok();
        const auto writing = Clock::now();
        auto status = writer_->write(offset, bytes.data(), bytes.size(), receiverWriteObservation());
        if (timing_) timing_->writeSeconds += secondsSince(writing);
        if (!status.isOk()) return status;
        return recordWritten(offset, bytes, verifiedChecksum);
    }
    Status writeGathered(std::span<const PreparedPayload> items) {
        if (items.size() != 2) return Status::invalidArgument("gathered receiver write needs two frames");
        if (skipped_) return Status::ok();
        std::array<storage::FileWriteSlice, 2> slices;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            if (item.logicalOffset > item.bytes.size())
                return Status::invalidArgument("gathered logical offset exceeds buffer");
            const auto size = item.logicalBytes ? item.logicalBytes : item.bytes.size() - item.logicalOffset;
            if (size > item.bytes.size() - item.logicalOffset || item.offset > identity_.totalSize ||
                size > identity_.totalSize - item.offset)
                return Status::invalidArgument("gathered write exceeds buffer or file");
            slices[i] = {item.bytes.data() + item.logicalOffset, size};
        }
        if (items[1].offset != items[0].offset + slices[0].size)
            return Status::invalidArgument("gathered receiver frames must be contiguous");
        const auto writing = Clock::now();
        auto status = storage::writeGatheredAtAll(file_, items[0].offset, slices, ioStats_, receiverWriteObservation());
        if (timing_) {
            timing_->writeSeconds += secondsSince(writing);
            ++timing_->gatheredWriteCalls;
            timing_->gatheredWriteFrames += items.size();
            timing_->gatheredWriteBytes += slices[0].size + slices[1].size;
        }
        // Even a partial kernel write must not certify any frame in this batch.
        if (!status.isOk()) return status;
        for (std::size_t i = 0; i < items.size(); ++i) {
            status = recordWritten(items[i].offset, {slices[i].data, slices[i].size},
                {identity_.checksumAlgorithm, items[i].frameChecksum});
            if (!status.isOk()) return status;
        }
        return Status::ok();
    }
   private:
    Status recordWritten(std::uint64_t offset, std::span<const std::uint8_t> bytes,
                         checksum::ChecksumValue verifiedChecksum) {
        ScopedManifestObservation observing(session_, nullptr, wholeObservation());
        Status status = Status::ok();
        std::size_t consumed = 0;
        while (consumed < bytes.size()) {
            const auto chunkOffset = (offset + consumed) / identity_.chunkSize * identity_.chunkSize;
            if (chunkBytes_ == 0) recorded_ = chunkOffset;
            if (chunkOffset != recorded_) return Status::invalidArgument("persistent chunk order mismatch");
            const auto length = std::min<std::uint64_t>(identity_.chunkSize, identity_.totalSize - recorded_);
            const auto part = std::min<std::size_t>(length - chunkBytes_, bytes.size() - consumed);
            if (part == bytes.size()) {
                if (!checksumComputer_.combine(verifiedChecksum, part))
                    return Status::invalidArgument("persistent chunk checksum algorithm mismatch");
            } else {
                // A frame spanning chunks needs a separate CRC for each chunk piece.
                checksumComputer_.update(bytes.data() + consumed, part);
            }
            chunkBytes_ += part;
            consumed += part;
            if (chunkBytes_ != length) continue;
            auto flushed = writer_->flush(receiverWriteObservation());
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
   public:
    Status commit(RangeObservation* observation = nullptr) {
        ScopedManifestObservation observing(session_, observation, wholeObservation());
        std::lock_guard publicationLock(publicationMutex_);
        if (skipped_) { active_ = false; return Status::ok(); }
        auto flushed = writer_->flush(receiverWriteObservation());
        if (!flushed.isOk()) return flushed;
        if (!std::visit([](const auto& s) { return s.missingRanges().empty(); }, session_))
            return Status::invalidArgument("persistent commit requires complete verified coverage");
        const auto actualSize = file_.fileSize();
        if (!actualSize.isOk()) return actualSize.status();
        if (actualSize.value() != identity_.totalSize)
            return Status::invalidArgument("persistent commit file size mismatch");
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
        struct stat publishedMetadata{};
        if (context_ && ::fstat(file_.fd(), &publishedMetadata) == 0 &&
            S_ISREG(publishedMetadata.st_mode) && publishedMetadata.st_size >= 0) {
            const auto toNanoseconds = [](std::int64_t seconds, long nanoseconds) {
                return seconds * 1'000'000'000LL + nanoseconds;
            };
            PersistentTreeFileMetadata snapshot;
            snapshot.relativePath = identity_.relativePath;
            snapshot.size = static_cast<std::uint64_t>(publishedMetadata.st_size);
            snapshot.device = static_cast<std::uint64_t>(publishedMetadata.st_dev);
            snapshot.inode = static_cast<std::uint64_t>(publishedMetadata.st_ino);
            snapshot.mtimeNanoseconds = toNanoseconds(publishedMetadata.st_mtim.tv_sec,
                                                       publishedMetadata.st_mtim.tv_nsec);
            snapshot.ctimeNanoseconds = toNanoseconds(publishedMetadata.st_ctim.tv_sec,
                                                       publishedMetadata.st_ctim.tv_nsec);
            std::lock_guard lock(context_->mutex);
            context_->publishedFiles.push_back(std::move(snapshot));
        }
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
    bool permitsParallelRangeWrites() const {
        return ioContext_.backend() == storage::FileIoBackendKind::Posix &&
            storage::effectivePosixWriteStrategy(ioContext_.config()) == storage::PosixWriteStrategy::Direct;
    }
    std::unique_ptr<storage::MappedFileWindow> mappedWindow(bool profile) const {
        return std::make_unique<storage::MappedFileWindow>(file_.fd(), identity_.totalSize, profile);
    }
    Status writeRange(std::uint64_t offset, std::span<const std::uint8_t> bytes,
                      RangeObservation* observation, bool independent = false,
                      storage::MappedFileWindow* mapped = nullptr) {
        if (skipped_) return Status::ok();
        if (!active_ || offset > identity_.totalSize || bytes.size() > identity_.totalSize - offset)
            return Status::invalidArgument("persistent range write exceeds file boundary");
        if (observation) {
            ++observation->offsetWriteCalls;
            ++observation->writerCallCount;
        }
        const auto started = Clock::now();
        // Independent ranges bypass the mutable buffered writer. The coordinator
        // pins this transaction and its fd until IO and certification finish.
        auto status = mapped ? mapped->write(offset, bytes) : independent
            ? storage::writeAtAll(file_, offset, bytes.data(), bytes.size(), ioContext_, ioStats_,
                                  observation ? &observation->fileWrite : nullptr)
            : writer_->write(offset, bytes.data(), bytes.size(),
                             observation ? &observation->fileWrite : nullptr);
        const auto elapsed = secondsSince(started);
        if (observation) {
            observation->offsetWriteSeconds += elapsed;
            observation->writerCallSeconds += elapsed;
        }
        if (timing_) timing_->writeSeconds += elapsed;
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
        if (observation) {
            ++observation->chunkFlushCalls;
            ++observation->dataFlushCalls;
        }
        const auto flushStarted = observation ? Clock::now() : Clock::time_point{};
        auto flushed = writer_->flush(observation ? &observation->fileWrite : nullptr);
        if (observation) {
            const auto elapsed = secondsSince(flushStarted);
            observation->chunkFlushSeconds += elapsed;
            observation->dataFlushSeconds += elapsed;
        }
        checkpoint::OptionalManifestTimer checkpointing(observation ? &observation->checkpointSeconds : nullptr);
        if (!flushed.isOk()) return flushed;
        const auto recordId = std::visit([](const auto& s) {
            return s.nextVerifiedChunkId();
        }, session_);
        if (recordId==UINT64_MAX) return Status::invalidArgument("checkpoint record ID exhausted");
        const double savedSecondsBefore = observation ? observation->manifestSave.saveSeconds : 0.0;
        const auto recordStarted = observation ? Clock::now() : Clock::time_point{};
        if (observation) ++observation->recordUpdateCalls;
        auto status = std::visit([&](auto& s) { return s.recordVerifiedChunk(recordId, offset, length, checksum, batch > 1); }, session_);
        if (observation) {
            const double elapsed = secondsSince(recordStarted);
            const double nestedSave = observation->manifestSave.saveSeconds - savedSecondsBefore;
            observation->recordUpdateSeconds += std::max(0.0, elapsed - nestedSave);
        }
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
        if (observation) ++observation->checkpointManifestFlushCalls;
        const auto started = observation ? Clock::now() : Clock::time_point{};
        auto status = std::visit([](auto& s) { return s.flushManifest(); }, session_);
        if (observation)
            observation->checkpointManifestFlushSeconds += secondsSince(started);
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
    PersistentTransferContext* context_;
    std::unique_ptr<storage::BufferedFileWriter> writer_;
    WholeFileObservation* wholeObservation() const {
        return timing_ && timing_->wholeFileObservationEnabled ? &timing_->wholeFileObservation : nullptr;
    }
    storage::FileWriteObservation* receiverWriteObservation() const {
        return timing_ && timing_->receiverPayloadObservationEnabled
            ? &timing_->receiverPayloadObservation.wholeFileWrite : nullptr;
    }
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
        std::unique_ptr<storage::MappedFileWindow> mapped;
        bool active = true, completed = false, writing = false;
        std::vector<chunk::CompletedRange> missing;
        std::size_t next = 0;
        std::uint64_t position = 0;
        checksum::ChecksumComputer checksum{checksum::ChecksumAlgorithm::None};
    };
    struct Entry {
        std::mutex mutex;
        std::condition_variable writersDrained;
        bool parallelDirectWrites = false;
        // Never hold this gate while acquiring Entry::mutex. A waiting syscall
        // retains its slot pin, but must not prevent metadata/checkpoint access.
        std::mutex directWriteMutex;
        bool serializedDirectWrites = false;
        std::counting_semaphore<2> directWritePermits{2};
        bool boundedDirectWrites = false;
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
        std::uint64_t rangeWriteBatchBytes = 0;
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
        const bool directWrites = context.policy.options.fileIo.posixWriteStrategy ==
            storage::PosixWriteStrategy::Direct;
        auto writeBatch = directWrites
            ? common::Result<std::uint64_t>(std::uint64_t{0})
            : rangeWriteBatchBytes(context.policy);
        if (!writeBatch.isOk()) return writeBatch.status();
        const auto* directSetting = std::getenv("CPNETFLUX_RANGE_DIRECT_WRITE");
        if (directSetting && std::string_view(directSetting) != "serial" &&
            std::string_view(directSetting) != "parallel" &&
            std::string_view(directSetting) != "file_serial" &&
            std::string_view(directSetting) != "file_dual")
            return Status::invalidArgument("CPNETFLUX_RANGE_DIRECT_WRITE must be serial, parallel, file_serial or file_dual");
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
                auto rangePolicy = context.policy;
                if (writeBatch.value() > 0 && rangePolicy.options.fileIo.bufferSize == 0 &&
                    rangePolicy.options.fileIo.posixWriteStrategy == storage::PosixWriteStrategy::Auto) {
                    rangePolicy.options.fileIo.bufferSize = writeBatch.value();
                    rangePolicy.options.fileIo.posixWriteStrategy = storage::PosixWriteStrategy::Coalesced;
                }
                entry->rangeWriteBatchBytes =
                    storage::effectivePosixWriteStrategy(rangePolicy.options.fileIo) ==
                            storage::PosixWriteStrategy::Coalesced
                        ? rangePolicy.options.fileIo.bufferSize
                        : 0;
                if (entry->checkpointBatch > 1) {
                    if (!context.checkpoints) context.checkpoints = std::make_shared<RangeCheckpointQueue>();
                    entry->checkpointQueue = context.checkpoints;
                }
                // The transaction outlives any channel's stack timing object.
                entry->transaction = std::make_unique<FileTransaction>(nullptr,rangePolicy,nullptr,&context);
                auto status = entry->transaction->begin(root,entry->base,download,remoteRoot,resume,observation);
                if (!status.isOk()) return status;
                entry->parallelDirectWrites = entry->transaction->permitsParallelRangeWrites() &&
                    (!directSetting || std::string_view(directSetting) != "serial");
                entry->serializedDirectWrites = entry->parallelDirectWrites && directSetting &&
                    std::string_view(directSetting) == "file_serial";
                entry->boundedDirectWrites = entry->parallelDirectWrites && directSetting &&
                    std::string_view(directSetting) == "file_dual";
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
        if (observation) {
            observation->rangeWriteBatchBytes = entry->rangeWriteBatchBytes;
            observation->directWriteConcurrency = entry->serializedDirectWrites ? 1 :
                entry->boundedDirectWrites ? 2 : 0;
        }
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
            if (found->second.id.rangeOffset != id.rangeOffset ||
                found->second.id.rangeLength != id.rangeLength)
                return Status::invalidArgument("range retry changed its partition");
            // Never reset a slot while an older attempt can still modify the fd.
            // Wait releases Entry so independent ranges and write completion proceed.
            if (id.attempt <= found->second.id.attempt)
                return Status::invalidArgument("range slot already claimed");
            lock.waitForReclaim(entry->writersDrained, [&] {
                return !entry->slots.at(id.rangeId).writing;
            });
            // Another waiter may have reclaimed the slot, or a checkpoint failed.
            found = entry->slots.find(id.rangeId);
            if (!entry->checkpointFailure.isOk()) return entry->checkpointFailure;
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
        if (slot.mapped) {
            const auto retired = slot.mapped->close();
            if (!retired.isOk()) return retired;
        }
        slot = Slot{};
        slot.id = id;
        if (context.policy.experimentalMappedDownload) {
            const auto* requested = std::getenv("CPNETFLUX_MAPPED_WRITE_PROFILE");
            const bool profile = observation && requested && std::string_view(requested) == "1";
            slot.mapped = entry->transaction->mappedWindow(profile);
        }
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
                        std::uint64_t offset, std::span<const std::uint8_t> bytes,
                        checksum::ChecksumValue verifiedChecksum, RangeObservation* observation,
                        ReceiverCpuObservation* cpu = nullptr) {
        OptionalThreadCpuTimer rangeCpu(cpu ? &cpu->rangeCpuSeconds : nullptr);
        if (cpu) ++cpu->rangeCalls;
        auto physicalWrite = [&](bool independent = false, storage::MappedFileWindow* mapped = nullptr) {
            OptionalThreadCpuTimer writing(cpu ? &cpu->physicalWriteCpuSeconds : nullptr);
            if (cpu) { ++cpu->physicalWriteCalls; cpu->physicalWriteBytes += bytes.size(); }
            return entry->transaction->writeRange(offset, bytes, observation, independent, mapped);
        };
        ObservedEntryLock lock(entry->mutex, observation);
        if (!entry->checkpointFailure.isOk()) return entry->checkpointFailure;
        const auto slot = entry->slots.find(id.rangeId);
        if (entry->committed || slot == entry->slots.end() || !slot->second.active || slot->second.writing ||
            !samePersistentIdentity(slot->second.id,id) || offset < id.rangeOffset ||
            offset > id.rangeOffset+id.rangeLength ||
            bytes.size() > id.rangeOffset+id.rangeLength-offset)
            return Status::invalidArgument("range write without exclusive valid slot");
        auto& lease=slot->second;
        if (lease.next>=lease.missing.size() || offset!=lease.position ||
            bytes.size()>lease.missing[lease.next].end-lease.position)
            return Status::invalidArgument("range DATA touches already verified bytes");
        auto status = Status::ok();
        if (entry->parallelDirectWrites) {
            // Slot identity/position remain pinned until both IO and the following
            // certification finish under Entry. Other slots are disjoint by begin().
            lease.writing = true;
            if (observation) ++observation->parallelDirectWriteCalls;
            lock.unlock();
            try {
                if (entry->boundedDirectWrites) {
                    checkpoint::OptionalManifestTimer waiting(
                        observation ? &observation->directWriteWaitSeconds : nullptr);
                    entry->directWritePermits.acquire();
                    struct PermitGuard {
                        std::counting_semaphore<2>& permits;
                        ~PermitGuard() { permits.release(); }
                    } permit{entry->directWritePermits};
                    waiting.stop();
                    if (observation) ++observation->boundedDirectWriteCalls;
                    status = physicalWrite(true);
                } else if (entry->serializedDirectWrites) {
                    std::unique_lock ioLock(entry->directWriteMutex, std::defer_lock);
                    checkpoint::OptionalManifestTimer waiting(
                        observation ? &observation->directWriteWaitSeconds : nullptr);
                    ioLock.lock();
                    waiting.stop();
                    if (observation) ++observation->serializedDirectWriteCalls;
                    status = physicalWrite(true);
                } else {
                    status = physicalWrite(true, lease.mapped.get());
                }
                // The physical IO gate is released before Entry is reacquired below.
            } catch (...) {
                lock.lock(); lease.writing = false; entry->writersDrained.notify_all();
                throw;
            }
            lock.lock();
            lease.writing = false;
            entry->writersDrained.notify_all();
        } else {
            status = physicalWrite();
        }
        if (!status.isOk()) return status;
        if (!entry->checkpointFailure.isOk()) return entry->checkpointFailure;
        checkpoint::OptionalManifestTimer checksumming(observation ? &observation->chunkChecksumSeconds : nullptr);
        if (!lease.checksum.combine(verifiedChecksum, bytes.size()))
            return Status::invalidArgument("range chunk checksum algorithm mismatch");
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
    static Status flush(const std::shared_ptr<Entry>& entry, const PersistentFileIdentity& id,
                        RangeObservation* observation, storage::MappedWriteStats* mappedStats) {
        if (!entry) return Status::ok();
        ObservedEntryLock lock(entry->mutex, observation);
        const auto slot = entry->slots.find(id.rangeId);
        // A late old-attempt exit must neither unmap nor flush its replacement.
        if (slot == entry->slots.end() || !samePersistentIdentity(slot->second.id, id))
            return Status::ok();
        if (slot->second.writing) return Status::runtimeError("range exit still has a writer");
        if (slot->second.mapped) {
            const auto retired = slot->second.mapped->close();
            if (mappedStats) *mappedStats = slot->second.mapped->stats();
            if (!retired.isOk()) return retired;
        }
        return flushLocked(entry, observation);
    }
 private:
    static void mergeBackground(Entry& entry, RangeObservation* observation) {
        if (!observation) return;
        auto& a = *observation; auto& b = entry.background;
        a.entryLockCalls += b.entryLockCalls;
        a.parallelDirectWriteCalls += b.parallelDirectWriteCalls;
        a.serializedDirectWriteCalls += b.serializedDirectWriteCalls;
        a.boundedDirectWriteCalls += b.boundedDirectWriteCalls;
        a.directWriteWaitSeconds += b.directWriteWaitSeconds;
        a.reclaimWaitCalls += b.reclaimWaitCalls;
        a.reclaimWaitSeconds += b.reclaimWaitSeconds;
        a.entryLockWaitSeconds += b.entryLockWaitSeconds;
        a.entryLockHoldSeconds += b.entryLockHoldSeconds;
        a.dataFrameCount += b.dataFrameCount;
        a.effectiveDataBytes += b.effectiveDataBytes;
        a.writerCallCount += b.writerCallCount;
        a.writerCallSeconds += b.writerCallSeconds;
        a.fileWrite.writeAtAllCalls += b.fileWrite.writeAtAllCalls;
        a.fileWrite.writeAtAllBytes += b.fileWrite.writeAtAllBytes;
        a.fileWrite.writeAtAllFailures += b.fileWrite.writeAtAllFailures;
        a.fileWrite.writeAtAllSeconds += b.fileWrite.writeAtAllSeconds;
        a.dataFlushCalls += b.dataFlushCalls;
        a.dataFlushSeconds += b.dataFlushSeconds;
        a.recordUpdateCalls += b.recordUpdateCalls;
        a.recordUpdateSeconds += b.recordUpdateSeconds;
        a.checkpointManifestFlushCalls += b.checkpointManifestFlushCalls;
        a.checkpointManifestFlushSeconds += b.checkpointManifestFlushSeconds;
        a.checkpointSeconds += b.checkpointSeconds;
        a.backgroundCheckpointCpuSeconds += b.backgroundCheckpointCpuSeconds;
        a.manifestSave.attempts += b.manifestSave.attempts;
        a.manifestSave.serializeCalls += b.manifestSave.serializeCalls;
        a.manifestSave.serializedBytes += b.manifestSave.serializedBytes;
        a.manifestSave.tempWriteCalls += b.manifestSave.tempWriteCalls;
        a.manifestSave.renameCalls += b.manifestSave.renameCalls;
        a.manifestSave.publishCalls += b.manifestSave.publishCalls;
        a.manifestSave.publishSuccesses += b.manifestSave.publishSuccesses;
        a.manifestSave.failures += b.manifestSave.failures;
        a.manifestSave.saveSeconds += b.manifestSave.saveSeconds;
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
        if (!slot->second.active || slot->second.writing || (entry->committed && !skipped))
            return Status::invalidArgument("invalid or duplicate range completion");
        if (skipped && !entry->committed &&
            !entry->transaction->rangeComplete(id.rangeOffset,id.rangeLength))
            return Status::invalidArgument("skipped range is not verified");
        if (slot->second.next!=slot->second.missing.size())
            return Status::invalidArgument("range completion has missing bytes");
        auto status = Status::ok();
        if (slot->second.mapped) {
            status = slot->second.mapped->close();
            if (!status.isOk()) return status;
        }
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
    std::uint32_t maxRetries, const PersistentTransferLimits& limits) {
    if (!stats) return Status::invalidArgument("missing persistent stats");
    if (!socket || !socket->valid()) return Status::invalidArgument("invalid persistent data socket");
    if (pendingWindow == 0 || pendingWindow > 16)
        return Status::invalidArgument("persistent pending window must be in range 1..16");
    if (channelCount == 0 || channelCount > kUnifiedFileMaxChannels ||
        channelIndex >= channelCount)
        return Status::invalidArgument("persistent channel index/count is invalid");
    const char* pacingValue = std::getenv("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND");
    const std::string_view pacing = pacingValue ? pacingValue : "off";
    if (pacing != "off" && pacing != "1500000000")
        return Status::invalidArgument("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND must be off or 1500000000");
    const bool paced = pacing != "off";
    if (paced && (socket->tlsEnabled() || socket->transport() != DataTransport::Tcp ||
        channelCount > 4 || (transferOptions && transferOptions->hotPathCompression.enabled)))
        return Status::invalidArgument("sender pacing requires plain uncompressed V3 TCP and at most four lanes");
    const char* kernelValue = std::getenv("CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND");
    const std::string_view kernelPacing = kernelValue ? kernelValue : "off";
    if (kernelPacing != "off" && kernelPacing != "2000000000")
        return Status::invalidArgument("CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND must be off or 2000000000");
    const bool kernelPaced = kernelPacing != "off";
    if (kernelPaced && (paced || channelCount > 4 || socket->tlsEnabled() ||
        socket->transport() != DataTransport::Tcp ||
        (transferOptions && transferOptions->hotPathCompression.enabled)))
        return Status::invalidArgument("kernel pacing requires plain uncompressed TCP, at most four lanes and application pacing off");
    auto kernelStatus = socket->configureTcpKernelPacing(kernelPaced ? 2000000000ULL : 0);
    if (!kernelStatus.isOk()) return kernelStatus;
    struct KernelPacingReceipt {
        bool enabled; std::uint32_t lane; FramedDataSocket* socket;
        ~KernelPacingReceipt() {
            if (!enabled) return;
            const auto s = socket->tcpKernelPacingStats();
            emitStderrObservation(
                "CPNETFLUX_TCP_KERNEL_PACING {\"lane\":%u,\"requested_bytes_per_second\":%llu,"
                "\"effective_ceiling_bytes_per_second\":%llu,\"enabled\":%s,"
                "\"scope\":\"TCP_socket_ceiling_not_achieved_rate\",\"application_pacer_enabled\":false}\n",
                lane, (unsigned long long)s.requestedBytesPerSecond,
                (unsigned long long)s.effectiveBytesPerSecond, s.enabled ? "true" : "false");
        }
    } kernelReceipt{kernelPaced, channelIndex, socket};
    detail::SenderPacer pacer(paced ? detail::SenderPacer::kRate : 0);
    struct PacingReceipt {
        bool enabled; std::uint32_t lane; detail::SenderPacer& p;
        ~PacingReceipt() {
            if (!enabled) return;
            emitStderrObservation(
                "CPNETFLUX_SENDER_PACING {\"lane\":%u,\"rate_bytes_per_second\":1500000000,"
                "\"frames\":%llu,\"wire_bytes\":%llu,\"waits\":%llu,\"sleep_calls\":%llu,"
                "\"wait_ns\":%llu,\"required_spacing_ns\":%llu,\"spacing_violations\":%llu,"
                "\"first_admission_ns\":%lld,\"last_admission_ns\":%lld,"
                "\"wait_backend\":\"timerfd\",\"timer_fd_creates\":%llu,"
                "\"timer_arm_calls\":%llu,\"timer_read_calls\":%llu}\n",
                lane, (unsigned long long)p.frames, (unsigned long long)p.wireBytes,
                (unsigned long long)p.waits, (unsigned long long)p.sleepCalls,
                (unsigned long long)p.waitNanoseconds, (unsigned long long)p.requiredSpacingNanoseconds,
                (unsigned long long)p.spacingViolations,
                (long long)p.firstAdmissionNanoseconds, (long long)p.lastAdmissionNanoseconds,
                (unsigned long long)p.timerFdCreates, (unsigned long long)p.timerArmCalls,
                (unsigned long long)p.timerReadCalls);
        }
    } pacingReceipt{paced, channelIndex, pacer};
    const auto pipelineDepth = diagnosticPipelineDepth();
    if (!pipelineDepth.isOk()) return pipelineDepth.status();
    const auto senderInline = diagnosticSenderInlinePayload();
    if (!senderInline.isOk()) return senderInline.status();
    if (socket->dataPayloadLimit() > PersistentDataSession::kMaxPayload &&
        transferOptions && transferOptions->hotPathCompression.enabled)
        return Status::invalidArgument("large DATA rejects compression");
    auto stageResult = ready ? ready->transferStages(channelCount,pipelineDepth.value(),socket->dataPayloadLimit())
                             : TransferStages::create(channelCount,pipelineDepth.value(),false,socket->dataPayloadLimit());
    if (!stageResult.isOk()) return stageResult.status();
    auto stages = stageResult.value();
    stats->pendingWindow = pendingWindow;
    const std::uint64_t filesAtConnectionStart = stats->files;
    auto& senderWork = stats->senderWorkProfile;
    SenderWorkProfile resultWork;
    resultWork.enabled = senderWork.enabled;
    auto status = PersistentDataSession::setTimeout(socket, 30);
    if (!status.isOk()) return status;
    std::optional<WorkExecutor> executor;
    if (ready) {
        auto context = ready->worker(channelIndex);
        if (!context.isOk()) return context.status();
        executor.emplace(*ready, context.value());
    }
    detail::PersistentTransferLifetime lifetime(socket, limits);
    if (auto bounded = lifetime.status(); !bounded.isOk()) return bounded;
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
        if (socket->valid()) socket->shutdown();
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
            tracePersistentProtocol("sender", "file_result_read_begin", channelIndex, &expected);
            const auto observedRead = resultWork.enabled && expected.rangeId == 0 ? memberWorkNow() : 0;
            auto result = PersistentDataSession::readResult(socket, expected);
            if (observedRead) resultWork.record(SenderWorkStage::ResultRead, observedRead, memberWorkNow());
            const double resultSeconds = secondsSince(resultStarted);
            const double waitSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            if (!result.isOk()) {
                if (observedRead) ++resultWork.resultFailures;
                tracePersistentProtocol("sender", "file_result_read_error", channelIndex,
                    &expected, 0, 0, static_cast<int>(result.status().code()));
                requestAbort(result.status());
                return;
            }
            tracePersistentProtocol("sender", "file_result_read_end", channelIndex, &expected,
                0, 0, static_cast<int>(result.value()));
            const auto fileStatus = receiverFileStatus(result.value(), expected.relativePath);
            if (observedRead && !fileStatus.isOk()) ++resultWork.resultFailures;
            if (ready) {
                if (!lease) { requestAbort(Status::runtimeError("missing work lease")); return; }
                auto completed = !fileStatus.isOk() &&
                    (continueAfterFileFailure || result.value() == FrameStatusCode::OutputExists)
                    ? executor->failTerminal(*lease, fileStatus)
                    : executor->finalize(*lease, fileStatus);
                if (!completed.isOk()) { requestAbort(completed); return; }
            }
            lifetime.finishResult(expected);
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
            if (observedRead && fileStatus.isOk() && callbackStatus.isOk()) ++resultWork.completedMembers;
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
        if (socket->valid()) socket->shutdown();
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
        double creditStarted = 0, creditEnded = 0;
        bool creditAborted = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (senderWork.enabled && !abort && pending.size() >= pendingWindow) creditStarted = memberWorkNow();
            changed.wait(lock, [&] { return abort || pending.size() < pendingWindow; });
            if (creditStarted) creditEnded = memberWorkNow();
            creditAborted = abort;
        }
        if (creditStarted) {
            auto kind = SenderWorkStage::CreditTerminal;
            if (!creditAborted) {
                const auto snapshot = ready ? ready->observeReadyIgnoringCredit(executor->owner())
                    : DynamicFileQueue::ReadyObservation{true, 1};
                kind = !snapshot.monotone ? SenderWorkStage::CreditUnproven : snapshot.remaining
                    ? SenderWorkStage::CreditReady : SenderWorkStage::CreditEmptyAtResume;
            }
            senderWork.record(kind, creditStarted, creditEnded);
        }
        if (creditAborted) break;
        if (ready) {
            auto next = executor->claim();
            if (!next.isOk()) { failWriter(next.status()); break; }
            if (!next.value()) break;
            lease = *next.value();
            id = lease->file;
            timing.queueWaitSeconds = next.value()->queueWaitSeconds;
            stats->queueWaitSeconds += next.value()->queueWaitSeconds;
        }
        // A dynamic work lease keeps its own retry generation when a healthy
        // lane picks it up; the lane's reconnect counter is not the work attempt.
        if (!ready && transferOptions && transferOptions->reconnectAttempt != UINT32_MAX)
            id.attempt = transferOptions->reconnectAttempt;
        timing.file = id; timing.channel = channelIndex; timing.sender = true;
        const bool observeSender = senderWork.enabled && id.rangeId == 0;
        if (senderWork.enabled) {
            if (id.rangeId) ++senderWork.excludedRanges;
            else ++senderWork.startedMembers;
        }
        timing.stageWritePool=bool(stages->writerPool);
        timing.stageReadConcurrency=stages->readConcurrency;
        timing.stageWriteConcurrency=stages->writeConcurrency;
        timing.stageNetworkLanes=stages->networkLanes;
        timing.stageBufferBudgetBytes=stages->bufferBudgetBytes;
        timing.stageOwnershipBoundBytes=stages->ownershipBoundBytes;

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
        const bool fileResume = id.attempt != 0 || (transferOptions && transferOptions->resume);
        const bool needsHandshake = id.rangeId != 0 || fileResume;
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
        tracePersistentProtocol("sender", "file_begin_write_begin", channelIndex, &id);
        status = PersistentDataSession::writeBegin(socket, id, fileResume);
        tracePersistentProtocol("sender", "file_begin_write_end", channelIndex, &id,
            0, 0, static_cast<int>(status.code()));
        if (!status.isOk()) { failWriter(status); break; }
        bool skipRange = false;
        std::vector<chunk::CompletedRange> missing{{id.rangeId ? id.rangeOffset : 0,
            id.rangeId ? id.rangeOffset+id.rangeLength : id.totalSize}};
        if (id.totalSize == 0) missing.clear();
        if (needsHandshake) {
            status = setPersistentReceiveDeadline(socket, Clock::now() + std::chrono::seconds(30));
            if (!status.isOk()) { failWriter(status); break; }
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
        const auto pipelineChunkBytes = socket->dataPayloadLimit() > PersistentDataSession::kMaxPayload
            ? std::size_t(socket->dataPayloadLimit() - 4)
            : std::min<std::size_t>(std::max<std::size_t>(options.bufferSize, 1), PersistentDataSession::kMaxPayload - 4);
        const auto kPipelineChunks = pipelineDepth.value();
        timing.rangeObservationEnabled = observeRanges(*stats);
        auto& senderCpu = timing.senderCpuObservation;
        const auto* senderProfile = std::getenv("CPNETFLUX_SENDER_PROFILE");
        senderCpu.enabled = timing.rangeObservationEnabled && senderProfile && std::string_view(senderProfile) == "1";
        timing.senderInlinePayload = senderInline.value();
        timing.payloadPipelineDepth = timing.senderInlinePayload ? 0 : kPipelineChunks;
        const auto laneCpuStarted = threadCpuSeconds(timing.rangeObservationEnabled);
        const auto pipelineBudget = timing.senderInlinePayload ? 0 : pipelineChunkBytes * kPipelineChunks;
        std::optional<BoundedPayloadPipeline> pipeline;
        if (!timing.senderInlinePayload)
            pipeline.emplace(pipelineBudget, kPipelineChunks, timing.rangeObservationEnabled);
        stats->payloadPipelineBudgetBytes = std::max<std::uint64_t>(
            stats->payloadPipelineBudgetBytes, pipelineBudget);
        std::uint64_t payloadSent = 0;
        std::uint64_t producedBytes = 0;
        double producerReadSeconds = 0;
        double producerChecksumSeconds = 0;
        std::uint64_t bufferAllocations = 0, bufferReuses = 0, allocatedCapacityBytes = 0;
        Status producerStatus = Status::ok();
        std::size_t pieceIndex = 0;
        std::uint64_t nextOffset = missing.empty() ? 0 : missing.front().begin;
        auto preparePayload = [&](std::vector<std::uint8_t> producerBuffer)
            -> common::Result<std::optional<PreparedPayload>> {
            while (pieceIndex < missing.size() && nextOffset >= missing[pieceIndex].end) {
                ++pieceIndex;
                if (pieceIndex < missing.size()) nextOffset = missing[pieceIndex].begin;
            }
            if (pieceIndex == missing.size()) return std::optional<PreparedPayload>{};
            const auto offset = nextOffset;
            const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(
                pipelineChunkBytes, std::min<std::uint64_t>(missing[pieceIndex].end - offset,
                    id.chunkSize - offset % id.chunkSize)));
            if (options.maxChunks && producedBytes >= options.maxChunks * id.chunkSize)
                return Status::runtimeError("transfer interrupted by max-chunks");
            if (producerBuffer.capacity() < pipelineChunkBytes) {
                producerBuffer.reserve(pipelineChunkBytes);
                ++bufferAllocations;
                allocatedCapacityBytes += producerBuffer.capacity();
            } else {
                ++bufferReuses;
            }
            producerBuffer.resize(length);
            auto readPermit = stages->reads.acquire(&timing.stageReadWaitSeconds, limits.taskDeadline);
            if (!readPermit.isOk()) return readPermit.status();
            ++timing.stageReadCalls;
            const auto reading = Clock::now();
            OptionalThreadCpuTimer readCpu(senderCpu.enabled ? &senderCpu.readCpuSeconds : nullptr);
            auto read = storage::readAtAll(file, offset, producerBuffer.data(), length,
                ioContext, &stats->fileIoStats);
            readCpu.stop();
            if (senderCpu.enabled) { ++senderCpu.readCalls; senderCpu.readBytes += length; }
            producerReadSeconds += secondsSince(reading);
            if (!read.isOk()) return read;
            const auto checksumStarted = Clock::now();
            OptionalThreadCpuTimer checksumCpu(senderCpu.enabled ? &senderCpu.checksumCpuSeconds : nullptr);
            checksum::ChecksumComputer frameChecksum(id.checksumAlgorithm, options.checksumBackend);
            frameChecksum.update(producerBuffer.data(), length);
            const auto verifiedChecksum = frameChecksum.finalize();
            if (!checksumComputer.combine(verifiedChecksum, length))
                return Status::invalidArgument("persistent DATA checksum algorithm mismatch");
            checksumCpu.stop();
            if (senderCpu.enabled) { ++senderCpu.checksumCalls; senderCpu.checksumBytes += length; }
            producerChecksumSeconds += secondsSince(checksumStarted);
            const auto chunkIndex = offset / id.chunkSize;
            if (options.hasCorruptChunk && chunkIndex == options.corruptChunk)
                producerBuffer[0] ^= 0xffU;
            readPermit = StageGate::Permit{};
            PreparedPayload prepared;
            prepared.offset = offset;
            prepared.chunkIndex = chunkIndex;
            prepared.frameChecksum = verifiedChecksum.value;
            prepared.bytes = std::move(producerBuffer);
            producedBytes += length;
            nextOffset += length;
            return std::optional<PreparedPayload>(std::move(prepared));
        };
        std::thread producer;
        double producerEntered = 0, producerLoopExit = 0;
        const auto producerCreateStarted = observeSender && pipeline ? memberWorkNow() : 0;
        if (pipeline) producer = std::thread([&]() {
            if (observeSender) producerEntered = memberWorkNow();
            const auto cpuStarted = threadCpuSeconds(timing.rangeObservationEnabled);
            struct CpuSample { PersistentFileTiming& timing; double started;
                ~CpuSample() { timing.producerThreadCpuSeconds += threadCpuSeconds(timing.rangeObservationEnabled)-started; }
            } cpuSample{timing,cpuStarted};
            std::vector<std::uint8_t> producerBuffer;
            struct ProducerExit {
                bool enabled; double& ended;
                ~ProducerExit() { if (enabled) ended = memberWorkNow(); }
            } producerExit{observeSender, producerLoopExit};
            for (;;) {
                auto next = preparePayload(std::move(producerBuffer));
                if (!next.isOk()) {
                    producerStatus = next.status();
                    pipeline->cancel(producerStatus);
                    return;
                }
                if (!next.value()) break;
                auto prepared = std::move(*next.value());
                const auto length = prepared.bytes.size();
                OptionalThreadCpuTimer pushCpu(senderCpu.enabled ? &senderCpu.pushCpuSeconds : nullptr);
                producerStatus = pipeline->push(std::move(prepared), &producerBuffer);
                pushCpu.stop();
                if (senderCpu.enabled) { ++senderCpu.pushCalls; senderCpu.pushBytes += length; }
                if (!producerStatus.isOk()) return;
            }
            pipeline->close();
        });
        if (producerCreateStarted) senderWork.record(SenderWorkStage::ProducerCreate, producerCreateStarted, memberWorkNow());
        std::vector<std::uint8_t> reusablePayloadBuffer;
        for (;;) {
            OptionalThreadCpuTimer popCpu(senderCpu.enabled && pipeline ? &senderCpu.popCpuSeconds : nullptr);
            auto next = pipeline ? pipeline->pop(std::move(reusablePayloadBuffer))
                : preparePayload(std::move(reusablePayloadBuffer));
            popCpu.stop();
            if (senderCpu.enabled && pipeline) ++senderCpu.popCalls;
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
            OptionalThreadCpuTimer prepareCpu(senderCpu.enabled ? &senderCpu.prepareCpuSeconds : nullptr);
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
            std::array<std::uint8_t, 4> checksumPrefix{};
            std::size_t checksumPrefixSize = 0;
            if (id.checksumAlgorithm != checksum::ChecksumAlgorithm::None) {
                checksumPrefix = {static_cast<std::uint8_t>(frameDigest >> 24),
                    static_cast<std::uint8_t>(frameDigest >> 16),
                    static_cast<std::uint8_t>(frameDigest >> 8),
                    static_cast<std::uint8_t>(frameDigest)};
                checksumPrefixSize = checksumPrefix.size();
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
            header.payloadSize = wireLength + checksumPrefixSize;
            header.flags = flags;
            auto encoded = protocol::encodeFrameHeader(header);
            prepareCpu.stop();
            preparing.stop();
            const auto payloadStarted = Clock::now();
            auto writePayload = [&](const std::uint8_t* payload, std::size_t size) {
                const auto frameBytes = encoded.size() + checksumPrefixSize + size;
                if (paced) {
                    auto admission = pacer.beforeSend(frameBytes, [&] {
                        const auto bounded = lifetime.status();
                        if (!bounded.isOk()) return bounded;
                        std::lock_guard<std::mutex> lock(mutex);
                        return abort ? (readerStatus.isOk()
                            ? Status::runtimeError("sender pacing cancelled") : readerStatus) : Status::ok();
                    });
                    if (!admission.isOk()) return admission;
                }
                if (ready) {
                    OptionalThreadCpuTimer leaseCpu(senderCpu.enabled ? &senderCpu.leaseCpuSeconds : nullptr);
                    if (senderCpu.enabled) ++senderCpu.leaseStartCalls;
                    auto reserved = executor->startIo(*lease, frameBytes);
                    if (!reserved.isOk()) return reserved;
                }
                checkpoint::OptionalManifestTimer socketWriting(timing.rangeObservationEnabled ? &timing.socketWriteSeconds : nullptr);
                if (timing.rangeObservationEnabled) ++timing.socketWriteCalls;
                OptionalThreadCpuTimer socketCpu(senderCpu.enabled ? &senderCpu.socketCpuSeconds : nullptr);
                if (senderCpu.enabled) { ++senderCpu.socketCalls; senderCpu.socketLogicalBytes += length; }
                auto written = socket->writeFrameBuffered(
                    encoded.data(), encoded.size(), checksumPrefix.data(), checksumPrefixSize,
                    payload, size);
                socketCpu.stop();
                socketWriting.stop();
                if (written.isOk() && ready) {
                    OptionalThreadCpuTimer leaseCpu(senderCpu.enabled ? &senderCpu.leaseCpuSeconds : nullptr);
                    if (senderCpu.enabled) ++senderCpu.leaseFinishCalls;
                    written = executor->finishIo(*lease, frameBytes);
                }
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
            if (id.checksumAlgorithm == checksum::ChecksumAlgorithm::Crc32c) {
                stats->verifiedChunkCrcs.push_back(PersistentTreeChunkCrc{
                    id.relativePath, id.totalSize, offset, static_cast<std::uint64_t>(length),
                    frameDigest});
            }
            payloadSent += length;
            stats->payloadBytes += length;
            addWire(protocol::kFrameHeaderSize + checksumPrefixSize + wireLength);
            reusablePayloadBuffer = std::move(prepared.bytes);
        }
        if (writeFailed && pipeline)
            pipeline->cancel(status.isOk() ? Status::runtimeError("persistent data write failed") : status);
        if (producer.joinable()) {
            const auto joinStarted = observeSender ? memberWorkNow() : 0;
            producer.join();
            if (observeSender) {
                senderWork.join(joinStarted, producerLoopExit, memberWorkNow());
                senderWork.record(SenderWorkStage::ProducerLoop, producerEntered, producerLoopExit);
                senderWork.workerEntrySeconds += producerEntered - producerCreateStarted;
            }
        }
        if (observeSender) {
            senderWork.readSeconds += producerReadSeconds;
            senderWork.checksumSeconds += producerChecksumSeconds;
        }
        timing.payloadBufferAllocations = bufferAllocations;
        timing.payloadBufferReuses = bufferReuses;
        timing.payloadBufferAllocatedCapacityBytes = allocatedCapacityBytes;
        timing.payloadBufferOwnershipBudgetBytes = pipelineChunkBytes * (timing.senderInlinePayload ? 1 : kPipelineChunks + 2);
        timing.laneThreadCpuSeconds = threadCpuSeconds(timing.rangeObservationEnabled)-laneCpuStarted;
        timing.pipelineBudgetBytes = pipelineBudget;
        timing.pipelineHighWatermarkBytes = pipeline ? pipeline->highWatermarkBytes() : 0;
        timing.pipelineProducerWaits = pipeline ? pipeline->producerWaitCount() : 0;
        timing.pipelineConsumerWaits = pipeline ? pipeline->consumerWaitCount() : 0;
        timing.pipelineProducerWaitSeconds = pipeline ? pipeline->producerWaitSeconds() : 0;
        timing.pipelineConsumerWaitSeconds = pipeline ? pipeline->consumerWaitSeconds() : 0;
        timing.readSeconds += producerReadSeconds;
        timing.checksumSeconds += producerChecksumSeconds;
        stats->payloadPipelineHighWatermarkBytes = std::max<std::uint64_t>(
            stats->payloadPipelineHighWatermarkBytes, timing.pipelineHighWatermarkBytes);
        stats->payloadPipelineProducerWaits += timing.pipelineProducerWaits;
        stats->payloadPipelineConsumerWaits += timing.pipelineConsumerWaits;
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
        if (!writeFailed && stats) {
            const auto toNanoseconds = [](std::int64_t seconds, long nanoseconds) {
                return seconds * 1'000'000'000LL + nanoseconds;
            };
            stats->verifiedFileMetadata.push_back(PersistentTreeFileMetadata{
                id.relativePath, static_cast<std::uint64_t>(after.st_size),
                static_cast<std::uint64_t>(after.st_dev), static_cast<std::uint64_t>(after.st_ino),
                toNanoseconds(after.st_mtim.tv_sec, after.st_mtim.tv_nsec),
                toNanoseconds(after.st_ctim.tv_sec, after.st_ctim.tv_nsec)});
        }
        if (!writeFailed) {
            tracePersistentProtocol("sender", "file_end_write_begin", channelIndex, &id);
            status = PersistentDataSession::writeEnd(socket, id,
                skipRange ? checksum::ChecksumValue{} : checksumComputer.finalize(), skipRange);
            tracePersistentProtocol("sender", "file_end_write_end", channelIndex, &id,
                0, 0, static_cast<int>(status.code()));
        }
        if (writeFailed || !status.isOk()) { failWriter(status.isOk() ? Status::runtimeError("persistent data write failed") : status); break; }
        if (observeSender) ++senderWork.sentMembers;
        const auto resultDeadline = lifetime.beginResult(id);
        status = setPersistentReceiveDeadline(socket, resultDeadline);
        if (!status.isOk()) { failWriter(status); break; }
        if (ready) {
            status = executor->awaitResult(*lease, WorkClock::now(), resultDeadline);
            if (!status.isOk()) { failWriter(status); break; }
        }
        if (needsHandshake || socket->tlsEnabled()) {
            const auto resultStarted = Clock::now();
            tracePersistentProtocol("sender", "file_result_read_begin", channelIndex, &id);
            const auto observedRead = observeSender ? memberWorkNow() : 0;
            auto result = PersistentDataSession::readResult(socket, id);
            if (observedRead) senderWork.record(SenderWorkStage::ResultRead, observedRead, memberWorkNow());
            if (!result.isOk()) {
                if (observeSender) ++senderWork.resultFailures;
                tracePersistentProtocol("sender", "file_result_read_error", channelIndex,
                    &id, 0, 0, static_cast<int>(result.status().code()));
                failWriter(result.status()); break;
            }
            tracePersistentProtocol("sender", "file_result_read_end", channelIndex, &id,
                0, 0, static_cast<int>(result.value()));
            const double resultSeconds = secondsSince(resultStarted);
            const auto rangeStatus = receiverFileStatus(result.value(), id.relativePath);
            if (observeSender && !rangeStatus.isOk()) ++senderWork.resultFailures;
            if (ready) {
                tracePersistentProtocol("sender", "lease_finalize_begin", channelIndex, &id);
                auto completed = result.value() == FrameStatusCode::OutputExists
                    ? executor->failTerminal(*lease, rangeStatus)
                    : executor->finalize(*lease, rangeStatus);
                tracePersistentProtocol("sender", "lease_finalize_end", channelIndex, &id,
                    0, 0, static_cast<int>(completed.code()));
                if (!completed.isOk()) { failWriter(completed); break; }
            }
            lifetime.finishResult(id);
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
            tracePersistentProtocol("sender", "callback_begin", channelIndex, &id);
            const auto callbackStatus = notifySafe(id, rangeStatus, true);
            if (observeSender && rangeStatus.isOk() && callbackStatus.isOk()) ++senderWork.completedMembers;
            tracePersistentProtocol("sender", "callback_end", channelIndex, &id,
                0, 0, static_cast<int>(callbackStatus.code()));
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
    const auto drainStarted = senderWork.enabled ? memberWorkNow() : 0;
    resultReader.join();
    if (senderWork.enabled) {
        senderWork.record(SenderWorkStage::ResultDrain, drainStarted, memberWorkNow());
        senderWork.merge(std::move(resultWork));
    }
    Status finalStatus;
    {
        std::lock_guard<std::mutex> lock(mutex);
        finalStatus = readerStatus;
        if (finalStatus.isOk() && !pending.empty())
            finalStatus = Status::runtimeError("persistent pending result queue not drained");
    }
    if (const auto bounded = lifetime.status(); !bounded.isOk()) finalStatus = bounded;
    if (socket->valid() && finalStatus.isOk()) {
        const auto fileCount = stats->files - filesAtConnectionStart;
        tracePersistentProtocol("sender", "directory_end_write_begin",
            channelIndex, nullptr, fileCount);
        status = PersistentDataSession::writeDirectoryEnd(socket, fileCount);
        tracePersistentProtocol("sender", "directory_end_write_end",
            channelIndex, nullptr, fileCount, 0, static_cast<int>(status.code()));
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
    const auto receiverWake = diagnosticReceiverWakePolicy();
    if (!receiverWake.isOk()) return receiverWake.status();
    const auto receiverWriteBatch = diagnosticReceiverWriteBatch();
    if (!receiverWriteBatch.isOk()) return receiverWriteBatch.status();
    const auto receiverInline = diagnosticReceiverInlinePayload();
    if (!receiverInline.isOk()) return receiverInline.status();
    const char* receiveBuffer = std::getenv("CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES");
    const std::string_view receiveBufferMode = receiveBuffer ? receiveBuffer : "off";
    if (receiveBufferMode != "off" && receiveBufferMode != "1048576")
        return Status::invalidArgument("CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES must be off or 1048576");
    if (receiveBufferMode != "off" && channelCount > 4)
        return Status::invalidArgument("bounded TCP receive buffer supports at most four lanes");
    const auto buffered = socket->configureTcpReceiveBuffer(receiveBufferMode == "off" ? 0 : 1048576);
    if (!buffered.isOk()) return buffered;
    const auto* exactRead = std::getenv("CPNETFLUX_TCP_EXACT_READ");
    const std::string_view exactReadMode = exactRead ? exactRead : "off";
    if (exactReadMode != "off" && exactReadMode != "on")
        return Status::invalidArgument("CPNETFLUX_TCP_EXACT_READ must be off or on");
    if (exactReadMode == "on" && channelCount > 4)
        return Status::invalidArgument("bounded TCP exact read supports at most four lanes");
    const auto exact = socket->configureTcpExactRead(exactReadMode == "on");
    if (!exact.isOk()) return exact;
    stats->pendingWindow = pendingWindow; // Receiver stays serial; report negotiated sender credit.
    const std::uint64_t filesAtConnectionStart = stats->files;
    auto status = PersistentDataSession::setTimeout(socket, 30);
    if (!status.isOk()) return status;
    if (!context) context = makePersistentTransferContext();
    auto stageDepth = diagnosticPipelineDepth();
    if (!stageDepth.isOk()) return stageDepth.status();
    std::shared_ptr<TransferStages> stages;
    {
        std::lock_guard lock(context->mutex);
        if (!context->stages) {
            auto created=TransferStages::create(channelCount,stageDepth.value(),true,socket->dataPayloadLimit());
            if (!created.isOk()) return created.status();
            context->stages=created.value();
        }
        stages=context->stages;
        if (stages->networkLanes!=channelCount || stages->depth!=stageDepth.value() ||
            stages->frameCapacityBytes!=socket->dataPayloadLimit())
            return Status::invalidArgument("V3 receiver stage configuration changed");
    }

    if (context->policy.experimentalMappedDownload) {
        const auto& options = context->policy.options;
        const auto* direct = std::getenv("CPNETFLUX_RANGE_DIRECT_WRITE");
        if (!download || channelCount != 4 || socket->transport() != DataTransport::Tcp || socket->tlsEnabled() ||
            !receiverInline.value() || receiverWriteBatch.value() != 1 ||
            stages->readConcurrency || stages->writeConcurrency || stages->writerPool ||
            options.fileIo.backend != storage::FileIoBackendKind::Posix ||
            options.fileIo.posixWriteStrategy != storage::PosixWriteStrategy::Direct ||
            options.fileIo.bufferSize || options.preallocateMode != storage::PreallocateMode::Off ||
            options.commitSyncPolicy != session::CommitSyncPolicy::None ||
            (direct && std::string_view(direct) != "parallel"))
            return Status::invalidArgument("experimental download write requires four inline TCP POSIX direct ranges without workers, buffers, preallocation or sync");
    }
    detail::PersistentTransferLifetime lifetime(socket, context->policy.limits);
    if (auto bounded = lifetime.status(); !bounded.isOk()) return bounded;
    PersistentDataSession state(dynamic,socket->dataPayloadLimit());
    PersistentFrame incoming; // One owner per connection; all writes below are synchronous.
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
        if (const auto bounded = lifetime.status(); !bounded.isOk()) return bounded;
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
        tracePersistentProtocol("receiver", "next_frame_read_begin", channelIndex,
            nullptr, stats->files - filesAtConnectionStart);
        status = PersistentDataSession::readNext(socket, socket->dataPayloadLimit(), &incoming);
        const auto& frame = incoming;
        if (!status.isOk()) {
            tracePersistentProtocol("receiver", "next_frame_read_error", channelIndex,
                nullptr, stats->files - filesAtConnectionStart, 0,
                static_cast<int>(status.code()));
            return finishTransport(status);
        }
        stats->wireBytes += protocol::kFrameHeaderSize + frame.payload.size();
        if (frame.header.type == FrameType::DirectoryEnd) {
            const auto filesOnConnection = stats->files - filesAtConnectionStart;
            tracePersistentProtocol("receiver", "directory_end_read", channelIndex,
                nullptr, filesOnConnection, frame.header.totalSize);
            if (frame.header.totalSize != filesOnConnection)
                return Status::invalidArgument("persistent directory file count mismatch");
            if (expectedFileCount != kUnknownPersistentFileCount &&
                expectedFileCount != filesOnConnection)
                return Status::invalidArgument("persistent channel file count mismatch");
            return finishTransport(overall);
        }
        auto decoded = PersistentDataSession::decodeBegin(frame);
        if (!decoded.isOk()) return decoded.status();
        const auto id = decoded.value();
        if (socket->dataPayloadLimit() > PersistentDataSession::kMaxPayload &&
            (id.chunkSize != 1048576 || id.checksumAlgorithm != checksum::ChecksumAlgorithm::Crc32c))
            return Status::invalidArgument("large DATA requires 1 MiB chunks and CRC32C");
        tracePersistentProtocol("receiver", "file_begin_read", channelIndex, &id);
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
        status = state.begin(frame.header, id);
        if (!status.isOk()) return finishTransport(status);
        PersistentFileTiming timing;
        timing.file = id; timing.channel = channelIndex;
        auto& memberProfile = stats->memberWorkProfile;
        const bool observeMember = memberProfile.enabled && id.rangeId == 0;
        if (memberProfile.enabled) {
            if (id.rangeId) ++memberProfile.excludedRanges;
            else ++memberProfile.startedMembers;
        }
        MemberWorkSample memberSample;
        double workerEntered = 0, workerLoopExit = 0;
        timing.stageWritePool=bool(stages->writerPool);
        timing.stageReadConcurrency=stages->readConcurrency;
        timing.stageWriteConcurrency=stages->writeConcurrency;
        timing.stageNetworkLanes=stages->networkLanes;
        timing.stageBufferBudgetBytes=stages->bufferBudgetBytes;
        timing.stageOwnershipBoundBytes=stages->ownershipBoundBytes;
        timing.wholeFileObservationEnabled = (stats->phaseTiming || observeMember) && id.rangeId == 0;
        timing.rangeObservationEnabled = observeRanges(*stats);
        const auto* receiverProfile = std::getenv("CPNETFLUX_RECEIVER_PROFILE");
        auto& receiverCpu = timing.receiverCpuObservation;
        const auto* receiverCpuProfile = std::getenv("CPNETFLUX_RECEIVER_CPU_PROFILE");
        receiverCpu.enabled = timing.rangeObservationEnabled && receiverCpuProfile &&
            std::string_view(receiverCpuProfile) == "1";
        receiverCpu.localWorkExecutor = ready != nullptr;
        timing.receiverPayloadObservationEnabled = stats->phaseTiming && receiverProfile &&
            std::string_view(receiverProfile) == "1";
        auto* payloadObservation = timing.receiverPayloadObservationEnabled
            ? &timing.receiverPayloadObservation : nullptr;
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
            const PersistentFileIdentity& id;
            RangeObservation* observation;
            storage::MappedWriteStats& mappedStats;
            bool armed = true;
            ~RangeExitFlush() { if (armed) (void)RangeFileCoordinator::flush(entry, id, observation, &mappedStats); }
        } rangeExitFlush{rangeEntry, id, observation, timing.mappedWrite};
        std::shared_ptr<FileTransaction> transaction;
        Status fileStatus = Status::ok();
        if (context->policy.experimentalMappedDownload &&
            (!id.rangeId || id.rangeCount < 4 || id.rangeCount > 8 || id.totalSize > 8ULL * 1024 * 1024 * 1024 ||
             id.checksumAlgorithm != checksum::ChecksumAlgorithm::Crc32c))
            return Status::invalidArgument("experimental download write requires 4..8 CRC32C ranges in a file <=8 GiB");
        const bool requestedResume = resume || (frame.header.flags & protocol::kFileResume);
        if (id.rangeId != 0) {
            auto started = RangeFileCoordinator::begin(*context, root, id, download, remoteRoot, requestedResume, observation);
            if (!started.isOk()) {
                // Write the semantic rejection before callbacks can cancel sibling sockets.
                auto rejected = PersistentDataSession::writeResumeResponse(socket,id,{},fileFailureCode(started.status()));
                (void)notify(callback,id,started.status(),true);
                return finishTransport(rejected.isOk() ? started.status() : rejected);
            } else {
                rangeEntry = std::move(started.value());
                timing.receiverMappedPayload = context->policy.experimentalMappedDownload;
                stats->receiverRangeCheckpointBatchChunks = rangeEntry->checkpointBatch;
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
                if (!status.isOk()) {
                    (void)notify(callback, id, status, true);
                    return finishTransport(status);
                }
            }
        } else {
            transaction = std::make_shared<FileTransaction>(stats->phaseTiming || observeMember ? &timing : nullptr,
                context->policy, &stats->fileIoStats, context.get());
            {
                std::lock_guard lock(context->mutex);
                context->integrityChecks.emplace_back([transaction] {
                    return transaction->verifyPublished();
                });
            }
            const auto beginStarted = observeMember ? memberWorkNow() : 0;
            fileStatus = transaction->begin(root, id, download, remoteRoot, requestedResume);
            if (observeMember) memberSample.set(MemberWorkStage::TransactionBegin, beginStarted, memberWorkNow());
            if (fileStatus.isOk()) {
                stats->loadedVerifiedChunks += transaction->loadedVerifiedChunks();
                stats->removedCorruptChunks += transaction->removedCorruptChunks();
            }
            if (frame.header.flags & protocol::kFileResume) {
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
        auto receiverDepth = diagnosticPipelineDepth();
        if (!receiverDepth.isOk()) return finishTransport(receiverDepth.status());
        // Diagnostic simplification preserves explicitly selected batch/pool execution.
        timing.receiverInlinePayload = receiverInline.value() &&
            receiverWriteBatch.value() == 1 && !stages->writerPool;
        timing.receiverEmptyQueueWake = receiverWake.value() == ProducerWakePolicy::EmptyQueue;
        const auto& receiverIo = context->policy.options.fileIo;
        timing.receiverWriteBatchLimit = !rangeEntry && !context->policy.options.maxChunks &&
            receiverIo.backend == storage::FileIoBackendKind::Posix &&
            storage::effectivePosixWriteStrategy(receiverIo) == storage::PosixWriteStrategy::Direct
            ? static_cast<std::uint32_t>(receiverWriteBatch.value()) : 1;
        const std::size_t receiverPipelineChunkBytes = socket->dataPayloadLimit();
        BoundedPayloadPipeline receiverPipeline(receiverPipelineChunkBytes * receiverDepth.value(),
            receiverDepth.value(), timing.rangeObservationEnabled, receiverWake.value());
        Status writerTerminal = Status::ok();
        std::thread receiverWriter;
        struct ReceiverWriterGuard {
            BoundedPayloadPipeline* pipeline = nullptr;
            std::thread* writer = nullptr;
            ~ReceiverWriterGuard() {
                if (pipeline && writer && writer->joinable()) {
                    pipeline->cancel(Status::runtimeError("receiver writer interrupted"));
                    writer->join();
                }
            }
        } writerGuard{&receiverPipeline, &receiverWriter};
        auto writePayloads = [&](std::span<const PreparedPayload> slots) {
            const auto count = slots.size();
            bool stop = false;
            std::array<std::span<const std::uint8_t>, 2> logical;
            for (std::size_t i = 0; i < count; ++i) {
                const auto& item = slots[i];
                if (item.logicalOffset > item.bytes.size() ||
                    item.logicalBytes > item.bytes.size() - item.logicalOffset) {
                    writerTerminal = Status::invalidArgument("receiver pipeline logical payload is invalid");
                    stop = true;
                    break;
                }
                const auto size = item.logicalBytes ? item.logicalBytes : item.bytes.size() - item.logicalOffset;
                logical[i] = {item.bytes.data() + item.logicalOffset, size};
            }
            for (std::size_t first = 0; first < count && !stop;) {
                const std::size_t group = count == 2 && first == 0 &&
                    slots[0].offset <= UINT64_MAX - logical[0].size() &&
                    slots[1].offset == slots[0].offset + logical[0].size() ? 2 : 1;
                std::size_t issued = 0;
                if (ready) {
                    for (; issued < group; ++issued) {
                        OptionalThreadCpuTimer leaseCpu(receiverCpu.enabled ? &receiverCpu.leaseCpuSeconds : nullptr);
                        if (receiverCpu.enabled) ++receiverCpu.leaseStartCalls;
                        auto status = executor->startIo(*lease, logical[first + issued].size());
                        if (!status.isOk()) { writerTerminal = status; stop = true; break; }
                    }
                }
                if (!stop && fileStatus.isOk()) {
                    auto performWrite = [&]() -> Status {
                        const bool measurePool=timing.stageWritePool && timing.rangeObservationEnabled;
                        const auto started=threadCpuSeconds(measurePool);
                        struct PoolCpu {
                            PersistentFileTiming& timing; bool measure; double started;
                            ~PoolCpu() { if (measure) timing.stageWritePoolCpuSeconds += threadCpuSeconds(true)-started; }
                        } poolCpu{timing,measurePool,started};
                        OptionalThreadCpuTimer writeCpu(receiverCpu.enabled ? &receiverCpu.writeCpuSeconds : nullptr);
                        if (receiverCpu.enabled) {
                            ++receiverCpu.writeCalls;
                            for (std::size_t n = 0; n < group; ++n)
                                receiverCpu.writeLogicalBytes += logical[first + n].size();
                        }
                        ++timing.stageWriteCalls;
                        Timer writing{rangeEntry ? &timing.writeSeconds : nullptr};
                        const auto& item = slots[first];
                        const checksum::ChecksumValue verifiedChecksum{id.checksumAlgorithm, item.frameChecksum};
                        fileStatus = group == 2 ? transaction->writeGathered({slots.data(), group}) : rangeEntry
                            ? RangeFileCoordinator::write(rangeEntry, id, item.offset, logical[first], verifiedChecksum, observation,
                                receiverCpu.enabled ? &receiverCpu : nullptr)
                            : transaction->write(item.offset, logical[first], verifiedChecksum);
                        if (fileStatus.isOk()) {
                            for (std::size_t n = 0; n < group; ++n) {
                                const auto i = first + n;
                                stats->payloadBytes += logical[i].size();
                                if (observation && id.rangeId != 0) {
                                    ++observation->dataFrameCount;
                                    observation->effectiveDataBytes += logical[i].size();
                                }
                                if (slots[i].checksumPresent && id.checksumAlgorithm == checksum::ChecksumAlgorithm::Crc32c)
                                    stats->verifiedChunkCrcs.push_back(PersistentTreeChunkCrc{
                                        id.relativePath, id.totalSize, slots[i].offset,
                                        static_cast<std::uint64_t>(logical[i].size()), slots[i].frameChecksum});
                            }
                            if (context->policy.options.maxChunks &&
                                stats->payloadBytes >= context->policy.options.maxChunks * id.chunkSize) {
                                writerTerminal = Status::runtimeError("transfer interrupted by max-chunks");
                                stop = true;
                            }
                        }
                        return Status::ok();
                    };
                    if (stages->writerPool) {
                        auto executed=stages->writerPool->run(performWrite,&timing.stageWriteWaitSeconds,context->policy.limits.taskDeadline);
                        if (!executed.isOk()) { writerTerminal=executed; stop=true; }
                    } else {
                        auto permit=stages->writes.acquire(&timing.stageWriteWaitSeconds,context->policy.limits.taskDeadline);
                        if (!permit.isOk()) { writerTerminal=permit.status(); stop=true; }
                        if (!stop) (void)performWrite();
                    }
                }
                // Balance every successful issue, including an issue or write failure.
                for (std::size_t n = 0; n < issued; ++n) {
                    OptionalThreadCpuTimer leaseCpu(receiverCpu.enabled ? &receiverCpu.leaseCpuSeconds : nullptr);
                    if (receiverCpu.enabled) ++receiverCpu.leaseFinishCalls;
                    auto status = executor->finishIo(*lease, logical[first + n].size());
                    if (!status.isOk()) { writerTerminal = status; stop = true; }
                }
                first += group;
            }
            return writerTerminal;
        };
        const auto createStarted = observeMember && !timing.receiverInlinePayload ? memberWorkNow() : 0;
        if (!timing.receiverInlinePayload) receiverWriter = std::thread([&] {
            if (observeMember) workerEntered = memberWorkNow();
            const auto writerCpuStarted = threadCpuSeconds(timing.rangeObservationEnabled);
            std::array<PreparedPayload, 2> slots;
            for (;;) {
                std::size_t count = 0;
                OptionalThreadCpuTimer popCpu(receiverCpu.enabled ? &receiverCpu.popCpuSeconds : nullptr);
                if (receiverCpu.enabled) ++receiverCpu.popCalls;
                if (timing.receiverWriteBatchLimit == 2) {
                    auto next = receiverPipeline.popBatch(slots);
                    if (!next.isOk()) { writerTerminal = next.status(); break; }
                    count = next.value();
                } else {
                    auto next = receiverPipeline.pop(std::move(slots[0].bytes));
                    if (!next.isOk()) { writerTerminal = next.status(); break; }
                    if (next.value()) { slots[0] = std::move(*next.value()); count = 1; }
                }
                popCpu.stop();
                if (count == 0) break;
                if (!writePayloads({slots.data(), count}).isOk()) {
                    receiverPipeline.cancel(writerTerminal);
                    break;
                }
            }
            timing.receiverWriterThreadCpuSeconds +=
                threadCpuSeconds(timing.rangeObservationEnabled) - writerCpuStarted;
            if (observeMember) workerLoopExit = memberWorkNow();
        });
        if (createStarted) memberSample.set(MemberWorkStage::ThreadCreate, createStarted, memberWorkNow());
        auto drainReceiverWriter = [&] {
            if (receiverWriter.joinable()) {
                receiverPipeline.close();
                receiverWriter.join();
            }
        };
        for (;;) {
            const auto payloadStarted = Clock::now();
            const auto previousCapacity = incoming.payload.capacity();
            OptionalThreadCpuTimer socketCpu(receiverCpu.enabled ? &receiverCpu.socketReadCpuSeconds : nullptr);
            if (receiverCpu.enabled) ++receiverCpu.socketReadCalls;
            status = PersistentDataSession::readNext(socket, socket->dataPayloadLimit(), &incoming);
            socketCpu.stop();
            const auto& data = incoming;
            if (!status.isOk()) {
                drainReceiverWriter();
                (void)notify(callback, id, status, true);
                return finishTransport(status);
            }
            stats->wireBytes += protocol::kFrameHeaderSize + data.payload.size();
            if (data.header.type == FrameType::FileEnd) {
                tracePersistentProtocol("receiver", "file_end_read", channelIndex, &id);
                receiverPipeline.close();
                if (receiverWriter.joinable()) {
                    const auto joinStarted = observeMember ? memberWorkNow() : 0;
                    receiverWriter.join();
                    if (observeMember) {
                        memberSample.splitJoin(joinStarted, workerLoopExit, memberWorkNow());
                        memberSample.workerEntrySeconds = workerEntered - createStarted;
                    }
                }
                timing.payloadPipelineDepth = timing.receiverInlinePayload ? 0 : static_cast<std::uint32_t>(receiverDepth.value());
                timing.pipelineBudgetBytes = timing.receiverInlinePayload ? 0 : receiverPipelineChunkBytes * receiverDepth.value();
                timing.pipelineHighWatermarkBytes = receiverPipeline.highWatermarkBytes();
                timing.pipelineProducerWaits = receiverPipeline.producerWaitCount();
                timing.pipelineConsumerWaits = receiverPipeline.consumerWaitCount();
                timing.pipelineProducerWaitSeconds = receiverPipeline.producerWaitSeconds();
                timing.pipelineConsumerWaitSeconds = receiverPipeline.consumerWaitSeconds();
                if (!writerTerminal.isOk()) return finishTransport(writerTerminal);
                auto ended = state.end(data.header, data.payload.data(), data.payload.size());
                if (!ended.isOk()) return ended.status();
                const auto resultDeadline = lifetime.beginResult(id);
                if (ready) {
                    status = executor->awaitResult(*lease, WorkClock::now(), resultDeadline);
                    if (!status.isOk()) return finishTransport(status);
                }
                if (fileStatus.isOk() && rangeEntry) {
                    Timer finalized{&timing.finalizeSeconds};
                    fileStatus = RangeFileCoordinator::complete(rangeEntry, ended.value(), ended.value().checksum,
                        (data.header.flags & protocol::kRangeSkipped) != 0, observation);
                }
                break;
            }
            timing.payloadIoSeconds += secondsSince(payloadStarted);
            if (!timing.firstPayloadSeconds) timing.firstPayloadSeconds = secondsSince(fileStarted);
            const auto checksumStarted = Clock::now();
            // Validate before either writer path consumes or recycles this frame storage.
            std::span<const std::uint8_t> logical(data.payload);
            std::vector<std::uint8_t> decodedPayload;
            if (payloadObservation) {
                auto& p = *payloadObservation;
                ++p.dataFrameCount;
                const bool allocated = data.payload.capacity() > previousCapacity;
                p.framePayloadAllocations += allocated;
                p.framePayloadReuses += !allocated;
                if (allocated) p.framePayloadAllocatedCapacityBytes += data.payload.capacity();
                p.framePayloadPeakCapacityBytes = std::max<std::uint64_t>(
                    p.framePayloadPeakCapacityBytes, data.payload.capacity());
                p.framePayloadBytes += data.payload.size();
                p.framePayloadCapacityBytes += data.payload.capacity();
            }
            std::optional<std::uint32_t> expectedDigest;
            if (data.header.flags & protocol::kDataChecksum) {
                if (id.checksumAlgorithm==checksum::ChecksumAlgorithm::None || logical.size()<=4) {
                    drainReceiverWriter();
                    return Status::invalidArgument("unexpected DATA checksum");
                }
                expectedDigest=(static_cast<std::uint32_t>(logical[0])<<24)|
                    (static_cast<std::uint32_t>(logical[1])<<16)|
                    (static_cast<std::uint32_t>(logical[2])<<8)|logical[3];
                logical = logical.subspan(4);
            }
            if (data.header.flags & protocol::kDataCompressed) {
                if (context->policy.experimentalMappedDownload)
                    return Status::invalidArgument("experimental download write rejects compressed DATA");
                const auto decoding = payloadObservation ? Clock::now() : Clock::time_point{};
                auto size = protocol::compressedDataPayloadLogicalLength(logical.data(),logical.size());
                if (!size.isOk()) { drainReceiverWriter(); return size.status(); }
                auto decoded = protocol::decodeCompressedDataPayload(logical.data(),logical.size(),
                    size.value(),PersistentDataSession::kMaxPayload);
                if (!decoded.isOk()) { ++stats->runtime.decompressionFailures; drainReceiverWriter(); return decoded.status(); }
                ++timing.compressedFrames;
                decodedPayload = std::move(decoded.value());
                logical = decodedPayload;
                if (payloadObservation) {
                    ++payloadObservation->decompressionCalls;
                    payloadObservation->decompressionSeconds += secondsSince(decoding);
                }
                ++stats->runtime.compressedFrames;
                stats->runtime.compressedLogicalBytes += logical.size();
                stats->runtime.compressedWireBytes += data.payload.size();
            }
            if (id.checksumAlgorithm!=checksum::ChecksumAlgorithm::None && !expectedDigest) {
                drainReceiverWriter();
                return Status::invalidArgument("DATA checksum is required before checkpointing");
            }
            if (expectedDigest) {
                const auto checking = payloadObservation ? Clock::now() : Clock::time_point{};
                OptionalThreadCpuTimer crcCpu(receiverCpu.enabled ? &receiverCpu.crcCpuSeconds : nullptr);
                if (receiverCpu.enabled) { ++receiverCpu.crcCalls; receiverCpu.crcBytes += logical.size(); }
                checksum::ChecksumComputer checksum(id.checksumAlgorithm);
                checksum.update(logical.data(),logical.size());
                const auto actualDigest = checksum.finalize().value;
                if (payloadObservation) {
                    payloadObservation->frameCrcBytes += logical.size();
                    payloadObservation->frameCrcSeconds += secondsSince(checking);
                }
                if (actualDigest!=*expectedDigest) {
                    drainReceiverWriter();
                    return Status::invalidArgument("DATA checksum mismatch before write");
                }
            }
            auto logicalHeader=data.header;
            logicalHeader.flags=0;logicalHeader.payloadSize=logical.size();
            OptionalThreadCpuTimer stateCpu(receiverCpu.enabled ? &receiverCpu.stateCpuSeconds : nullptr);
            if (receiverCpu.enabled) ++receiverCpu.stateCalls;
            status = expectedDigest
                ? state.dataWithChecksum(logicalHeader, logical.size(),
                    checksum::ChecksumValue{id.checksumAlgorithm, *expectedDigest})
                : state.data(logicalHeader, logical.size(), logical.data());
            stateCpu.stop();
            timing.checksumSeconds += secondsSince(checksumStarted);
            if (!status.isOk()) { drainReceiverWriter(); return finishTransport(status); }
            PreparedPayload prepared;
            prepared.offset = data.header.offset;
            prepared.frameChecksum = expectedDigest.value_or(0);
            prepared.checksumPresent = expectedDigest.has_value();
            if (!decodedPayload.empty()) {
                prepared.bytes = std::move(decodedPayload);
                prepared.logicalBytes = prepared.bytes.size();
            } else {
                prepared.bytes = std::move(incoming.payload);
                prepared.logicalOffset = expectedDigest ? 4 : 0;
                prepared.logicalBytes = logical.size();
            }
            if (timing.receiverInlinePayload) {
                status = writePayloads({&prepared, 1});
                incoming.payload = std::move(prepared.bytes);
            } else {
                OptionalThreadCpuTimer pushCpu(receiverCpu.enabled ? &receiverCpu.pushCpuSeconds : nullptr);
                if (receiverCpu.enabled) ++receiverCpu.pushCalls;
                status = receiverPipeline.push(std::move(prepared), &incoming.payload);
            }
            if (!status.isOk()) { drainReceiverWriter(); return finishTransport(status); }
        }
        tracePersistentProtocol("receiver", "commit_begin", channelIndex, &id);
        if (rangeEntry) {
            const auto checkpoint = RangeFileCoordinator::flush(rangeEntry, id, observation, &timing.mappedWrite);
            if (!checkpoint.isOk()) fileStatus = checkpoint;
            rangeExitFlush.armed = false; // do not flush another lane after our result
        }
        if (fileStatus.isOk() && transaction) {
            Timer timer{&timing.finalizeSeconds};
            const auto commitStarted = observeMember ? memberWorkNow() : 0;
            fileStatus = transaction->commit();
            if (observeMember) memberSample.set(MemberWorkStage::Commit, commitStarted, memberWorkNow());
        }
        tracePersistentProtocol("receiver", "commit_end", channelIndex, &id,
            0, 0, static_cast<int>(fileStatus.code()));
        if (const auto bounded = lifetime.status(); !bounded.isOk()) return bounded;
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
            tracePersistentProtocol("receiver", "file_result_write_begin", channelIndex, &id,
                0, 0, static_cast<int>(FrameStatusCode::OutputExists));
            const auto rejected = PersistentDataSession::writeResult(
                socket, id, FrameStatusCode::OutputExists);
            tracePersistentProtocol("receiver", "file_result_write_end", channelIndex, &id,
                0, 0, static_cast<int>(rejected.code()));
            timing.rdmaTransportStats = socket->rdmaStats();
            timing.rdmaReceiveObservationAvailable = socket->transport() == DataTransport::Rdma;
            (void)notify(callback, id, fileStatus, true);
            return rejected.isOk() ? fileStatus : rejected;
        }
        tracePersistentProtocol("receiver", "callback_begin", channelIndex, &id);
        status = notify(callback, id, fileStatus, true);
        tracePersistentProtocol("receiver", "callback_end", channelIndex, &id,
            0, 0, static_cast<int>(status.code()));
        if (fileStatus.isOk() && id.rangeId != 0) ++stats->rangeCompletions;
        timing.manifestSeconds += secondsSince(completedStarted);
        if (!status.isOk()) return finishTransport(status);
        const auto resultStarted = Clock::now();
        if (const auto bounded = lifetime.status(); !bounded.isOk()) return bounded;
        tracePersistentProtocol("receiver", "file_result_write_begin", channelIndex, &id,
            0, 0, static_cast<int>(fileFailureCode(fileStatus)));
        const auto resultWriteStarted = observeMember ? memberWorkNow() : 0;
        status = PersistentDataSession::writeResult(socket, id, fileFailureCode(fileStatus));
        if (observeMember) memberSample.set(MemberWorkStage::ResultWrite, resultWriteStarted, memberWorkNow());
        tracePersistentProtocol("receiver", "file_result_write_end", channelIndex, &id,
            0, 0, static_cast<int>(status.code()));
        if (!status.isOk()) return finishTransport(status);
        if (ready) {
            status = !fileStatus.isOk() && !dynamic
                ? executor->failTerminal(*lease, fileStatus)
                : executor->finalize(*lease, fileStatus);
            if (!status.isOk()) return finishTransport(status);
            leaseGuard.finished = true;
        }
        lifetime.finishResult(id);
        stats->wireBytes += protocol::kFrameHeaderSize;
        timing.rdmaTransportStats = socket->rdmaStats();
        timing.rdmaReceiveObservationAvailable = socket->transport() == DataTransport::Rdma;
        timing.laneThreadCpuSeconds = threadCpuSeconds(timing.rangeObservationEnabled)-laneCpuStarted;
        timing.fileResultSeconds = secondsSince(resultStarted);
        timing.wallSeconds = secondsSince(fileStarted);
        if (observeMember && fileStatus.isOk()) memberProfile.record(memberSample, timing.wholeFileObservation.manifestSave);
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
namespace {
void appendManifestSaveObservation(std::ostream& out, const checkpoint::ManifestSaveStats& m) {
    out << "{\"attempts\":" << m.attempts
        << ",\"serialize_calls\":" << m.serializeCalls
        << ",\"serialized_bytes\":" << m.serializedBytes
        << ",\"save_seconds\":" << m.saveSeconds
        << ",\"temp_write_calls\":" << m.tempWriteCalls
        << ",\"rename_calls\":" << m.renameCalls
        << ",\"publish_calls\":" << m.publishCalls
        << ",\"publish_successes\":" << m.publishSuccesses
        << ",\"failures\":" << m.failures
        << ",\"serialize_seconds\":" << m.serializeSeconds
        << ",\"temp_write_seconds\":" << m.tempWriteSeconds
        << ",\"publish_seconds\":" << m.renameSeconds
        << ",\"rename_seconds\":" << m.renameSeconds
        << ",\"record_preparation_calls\":" << m.recordPreparationCalls
        << ",\"record_sort_calls\":" << m.recordSortCalls
        << ",\"record_copy_items\":" << m.recordCopyItems
        << ",\"record_prepare_seconds\":" << m.recordPreparationSeconds
        << ",\"range_validation_seconds\":" << m.rangeValidationSeconds
        << ",\"record_format_seconds\":" << m.recordFormatSeconds
        << ",\"body_crc_seconds\":" << m.bodyCrcSeconds << '}';
}
}  // namespace
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
        const bool allocationObserved = !t.sender && t.file.rangeId == 0;
        out << ",\"preallocate_observation_scope\":";
        if (allocationObserved)
            out << "\"whole_file_begin_included_in_file_wall_and_lane_cpu; calls_are_wrapper_attempts\"";
        else out << "null";
        out << ",\"preallocate_calls\":";
        if (allocationObserved) out << t.preallocateCalls; else out << "null";
        out << ",\"preallocate_requested_bytes\":";
        if (allocationObserved) out << t.preallocateRequestedBytes; else out << "null";
        out << ",\"preallocate_seconds\":";
        if (allocationObserved) out << t.preallocateSeconds; else out << "null";
        out << ",\"preallocate_cpu_seconds\":";
        if (allocationObserved) out << t.preallocateCpuSeconds; else out << "null";
        out << ",\"receiver_write_batch_limit\":";
        if (allocationObserved) out << t.receiverWriteBatchLimit; else out << "null";
        out << ",\"gathered_write_scope\":\"two_contiguous_frames_wrapper_attempts_not_verified_progress\""
            << ",\"gathered_write_calls\":" << t.gatheredWriteCalls
            << ",\"gathered_write_frames\":" << t.gatheredWriteFrames
            << ",\"gathered_write_bytes\":" << t.gatheredWriteBytes;
        out << ",\"range_observation_enabled\":" << (t.rangeObservationEnabled ? "true" : "false")
            << ",\"range_observation_scope\":\"local_range_begin_write_complete_subintervals_overlap_not_wall\""
            << ",\"lane_thread_cpu_seconds\":" << t.laneThreadCpuSeconds
            << ",\"lane_thread_cpu_scope\":\"" << (t.sender ? "payload_prepare_send_and_join" : "file_begin_through_result") << "\""
            << ",\"receiver_payload_execution\":\"" << (t.sender ? "not_receiver" : t.receiverInlinePayload ? "inline" : "queued") << "\""
            << ",\"sender_payload_execution\":\"" << (!t.sender ? "not_sender" : t.senderInlinePayload ? "inline" : "queued") << "\""
            << ",\"receiver_write_backend\":\"" << (t.sender ? "not_receiver" : t.receiverMappedPayload ? "mmap_experimental" : "configured_file_io") << "\""
            << ",\"mapped_write\":{\"window_bound_bytes\":" << (t.receiverMappedPayload ? storage::MappedFileWindow::kWindowBytes : 0)
            << ",\"concurrent_window_bound_bytes\":" << (t.receiverMappedPayload ? 4 * storage::MappedFileWindow::kWindowBytes : 0)
            << ",\"map_calls\":" << t.mappedWrite.mapCalls << ",\"map_failures\":" << t.mappedWrite.mapFailures
            << ",\"unmap_calls\":" << t.mappedWrite.unmapCalls << ",\"write_calls\":" << t.mappedWrite.writeCalls
            << ",\"written_bytes\":" << t.mappedWrite.writtenBytes << ",\"active_mapped_bytes\":" << t.mappedWrite.activeMappedBytes
            << ",\"peak_mapped_bytes\":" << t.mappedWrite.peakMappedBytes << ",\"profile\":";
        const auto& mappedProfile = t.mappedWrite.profile;
        if (!mappedProfile.enabled) out << "null";
        else out << "{\"schema\":\"cpnetflux_mapped_write_profile_v1\""
            << ",\"cpu_clock\":\"CLOCK_THREAD_CPUTIME_ID\",\"fault_scope\":\"RUSAGE_THREAD_copy_brackets\""
            << ",\"scope\":\"map/unmap/copy disjoint wall and CPU wrappers; copy includes faults,not pure memcpy; getrusage excluded from copy CPU but charged to task; faults include bracket instrumentation; counts are attempts; map/copy within physical write; final unmap may be in flush\""
            << ",\"copy_calls\":" << mappedProfile.copyCalls << ",\"copy_bytes\":" << mappedProfile.copyBytes
            << ",\"errors\":" << mappedProfile.errors << ",\"copy_minor_faults\":" << mappedProfile.copyMinorFaults
            << ",\"copy_major_faults\":" << mappedProfile.copyMajorFaults
            << ",\"map_cpu_seconds\":" << mappedProfile.mapCpuSeconds << ",\"map_wall_seconds\":" << mappedProfile.mapWallSeconds
            << ",\"unmap_cpu_seconds\":" << mappedProfile.unmapCpuSeconds << ",\"unmap_wall_seconds\":" << mappedProfile.unmapWallSeconds
            << ",\"copy_cpu_seconds\":" << mappedProfile.copyCpuSeconds << ",\"copy_wall_seconds\":" << mappedProfile.copyWallSeconds << "}";
        out << "}"
            << ",\"receiver_writer_thread_cpu_seconds\":" << t.receiverWriterThreadCpuSeconds
            << ",\"producer_thread_cpu_seconds\":" << t.producerThreadCpuSeconds
            << ",\"frame_prepare_seconds\":" << t.framePrepareSeconds
            << ",\"socket_write_seconds\":" << t.socketWriteSeconds
            << ",\"socket_write_frame_calls\":" << t.socketWriteCalls
            << ",\"socket_write_scope\":\"DATA_header_and_payload_writeFrame_includes_TLS_when_enabled\""
            << ",\"pipeline_wait_scope\":\"condition_wait_through_mutex_reacquire_subintervals_not_wall\""
            << ",\"payload_pipeline_stage\":\"" << (t.sender ? "source_read_checksum_to_network" : "network_checksum_to_write") << "\""
            << ",\"write_seconds_scope\":\"" << (t.file.rangeId ? "range_coordinator_includes_entry_wait_crc_checkpoint" : "writer_write_only") << "\""
            << ",\"payload_pipeline_producer_wake_policy\":\""
            << (t.receiverEmptyQueueWake ? "empty_queue_when_blocked" : "available_capacity") << "\""
            << ",\"stage_execution\":{\"schema_version\":1,\"read_concurrency\":" << t.stageReadConcurrency
            << ",\"write_concurrency\":" << t.stageWriteConcurrency << ",\"network_lanes\":" << t.stageNetworkLanes
            << ",\"payload_budget_bytes\":" << t.stageBufferBudgetBytes << ",\"payload_ownership_bound_bytes\":" << t.stageOwnershipBoundBytes
            << ",\"read_admission_required\":" << (t.stageReadConcurrency && t.stageReadConcurrency < t.stageNetworkLanes ? "true" : "false")
            << ",\"write_admission_required\":" << (t.stageWriteConcurrency && t.stageWriteConcurrency < t.stageNetworkLanes ? "true" : "false")
            << ",\"admission_policy\":\"" << (t.stageWritePool ? "FIFO_ready_jobs" : "oldest_waiter_with_bounded_ready_bypass") << "\",\"max_ready_bypasses\":" << (t.stageWritePool ? 0 : StageGate::kReadyBypassLimit)
            << ",\"write_executor\":\"" << (t.sender ? "not_receiver" : t.stageWritePool ? "shared_ready_pool" : (t.stageWriteConcurrency && t.stageWriteConcurrency < t.stageNetworkLanes ? "shared_admission" : "per_lane")) << "\",\"write_pool_cpu_seconds\":" << t.stageWritePoolCpuSeconds
            << ",\"write_pool_workers\":" << (t.stageWritePool ? t.stageWriteConcurrency : 0)
            << ",\"read_calls\":" << t.stageReadCalls << ",\"write_calls\":" << t.stageWriteCalls
            << ",\"read_wait_seconds\":" << t.stageReadWaitSeconds << ",\"write_wait_seconds\":" << t.stageWriteWaitSeconds
            << ",\"scope\":\"batch_shared_ready_IO_admission_not_thread_counts;0=per_lane;payload_bound_excludes_transport_metadata_tmpfs\"}"
            << ",\"payload_pipeline_depth\":" << t.payloadPipelineDepth
            << ",\"payload_buffer_allocations\":" << t.payloadBufferAllocations
            << ",\"payload_buffer_reuses\":" << t.payloadBufferReuses
            << ",\"payload_buffer_allocated_capacity_bytes\":" << t.payloadBufferAllocatedCapacityBytes
            << ",\"payload_buffer_ownership_budget_bytes\":" << t.payloadBufferOwnershipBudgetBytes
            << ",\"pipeline_budget_bytes\":" << t.pipelineBudgetBytes
            << ",\"pipeline_high_watermark_bytes\":" << t.pipelineHighWatermarkBytes
            << ",\"pipeline_producer_waits\":" << t.pipelineProducerWaits
            << ",\"pipeline_consumer_waits\":" << t.pipelineConsumerWaits
            << ",\"pipeline_producer_wait_seconds\":" << t.pipelineProducerWaitSeconds
            << ",\"pipeline_consumer_wait_seconds\":" << t.pipelineConsumerWaitSeconds;
        if (t.rdmaReceiveObservationAvailable) {
            const auto& rdma = t.rdmaTransportStats;
            out << ",\"rdma_receive_observation\":{\"schema\":\"cpnetflux_rdma_receive_observation_v1\","
                << "\"scope\":\"receiver socket cumulative snapshot at file completion; wait time overlaps payload_io_seconds\","
                << "\"configured_receive_queue_depth\":" << kRdmaDataReceiveQueueDepth << ","
                << "\"negotiated_receive_queue_depth\":" << rdma.negotiatedReceiveQueueDepth << ","
                << "\"negotiated_path_mtu_bytes\":" << rdma.negotiatedPathMtuBytes << ","
                << "\"negotiated_min_rnr_timer\":" << rdma.negotiatedMinRnrTimer << ","
                << "\"negotiated_rnr_retry\":" << rdma.negotiatedRnrRetry << ","
                << "\"receive_slot_bytes\":" << kRdmaDataMessageBufferBytes << ","
                << "\"receive_bytes\":" << rdma.receiveBytes << ","
                << "\"receive_completions\":" << rdma.receiveCompletions << ","
                << "\"receive_post_calls\":" << rdma.receivePostCalls << ","
                << "\"receive_reposts\":" << rdma.receiveReposts << ","
                << "\"receive_ready_queue_high_watermark\":" << rdma.receiveReadyQueueHighWatermark << ","
                << "\"receive_completion_poll_calls\":" << rdma.receiveCompletionPollCalls << ","
                << "\"receive_completion_empty_poll_calls\":" << rdma.receiveCompletionEmptyPollCalls << ","
                << "\"receive_completion_event_waits\":" << rdma.receiveCompletionEventWaits << ","
                << "\"receive_completion_wait_count\":" << rdma.receiveCompletionWaitCount << ","
                << "\"receive_completion_wait_seconds\":"
                << static_cast<double>(rdma.receiveCompletionWaitNanoseconds) / 1e9 << '}';
        } else {
            out << ",\"rdma_receive_observation\":null";
        }
        out << ",\"sender_cpu_observation\":";
        if (t.sender && t.senderCpuObservation.enabled) {
            const auto& p = t.senderCpuObservation;
            out << "{\"schema\":\"cpnetflux_sender_cpu_observation_v1\","
                << "\"clock\":\"CLOCK_THREAD_CPUTIME_ID\","
                << "\"scope\":\"exact wrapper CPU includes timer cost; queued read/checksum/push on producer; inline read/checksum on lane; pop/prepare/lease/socket on lane; inline queue counters zero; nested in thread totals; waits exclude sleeping but include reacquire CPU; counts are attempts not certified progress\","
                << "\"read_checksum_cpu_owner\":\"" << (t.senderInlinePayload ? "lane" : "producer") << "\","
                << "\"read_calls\":" << p.readCalls << ",\"read_bytes\":" << p.readBytes
                << ",\"checksum_calls\":" << p.checksumCalls << ",\"checksum_bytes\":" << p.checksumBytes
                << ",\"push_calls\":" << p.pushCalls << ",\"push_bytes\":" << p.pushBytes
                << ",\"pop_calls\":" << p.popCalls << ",\"lease_start_calls\":" << p.leaseStartCalls
                << ",\"lease_finish_calls\":" << p.leaseFinishCalls << ",\"socket_calls\":" << p.socketCalls
                << ",\"socket_logical_bytes\":" << p.socketLogicalBytes
                << ",\"read_cpu_seconds\":" << p.readCpuSeconds << ",\"checksum_cpu_seconds\":" << p.checksumCpuSeconds
                << ",\"push_cpu_seconds\":" << p.pushCpuSeconds << ",\"pop_cpu_seconds\":" << p.popCpuSeconds
                << ",\"prepare_cpu_seconds\":" << p.prepareCpuSeconds << ",\"lease_cpu_seconds\":" << p.leaseCpuSeconds
                << ",\"socket_cpu_seconds\":" << p.socketCpuSeconds << '}';
        } else out << "null";
        out << ",\"receiver_cpu_observation\":";
        if (!t.sender && t.receiverCpuObservation.enabled) {
            const auto& p = t.receiverCpuObservation;
            out << "{\"schema\":\"cpnetflux_receiver_cpu_observation_v1\","
                << "\"clock\":\"CLOCK_THREAD_CPUTIME_ID\","
                << "\"scope\":\"exact wrapper CPU includes timer cost; read/CRC/state/push on lane; pop/lease on lane or writer; write on lane/writer/pool; physical nested in range nested in write; excludes begin/finalize/background; counts are attempts not certified progress; read includes FILE_END; sleeping excluded\","
                << "\"write_cpu_owner\":\"" << (t.stageWritePool ? "pool" : t.receiverInlinePayload ? "lane" : "writer") << "\","
                << "\"local_work_executor\":" << (p.localWorkExecutor ? "true" : "false") << ','
                << "\"socket_read_calls\":" << p.socketReadCalls << ",\"crc_calls\":" << p.crcCalls
                << ",\"crc_bytes\":" << p.crcBytes << ",\"state_calls\":" << p.stateCalls
                << ",\"lease_start_calls\":" << p.leaseStartCalls << ",\"lease_finish_calls\":" << p.leaseFinishCalls
                << ",\"write_calls\":" << p.writeCalls << ",\"write_logical_bytes\":" << p.writeLogicalBytes
                << ",\"range_calls\":" << p.rangeCalls << ",\"physical_write_calls\":" << p.physicalWriteCalls
                << ",\"physical_write_bytes\":" << p.physicalWriteBytes
                << ",\"push_calls\":" << p.pushCalls << ",\"pop_calls\":" << p.popCalls
                << ",\"socket_read_cpu_seconds\":" << p.socketReadCpuSeconds << ",\"crc_cpu_seconds\":" << p.crcCpuSeconds
                << ",\"state_cpu_seconds\":" << p.stateCpuSeconds << ",\"lease_cpu_seconds\":" << p.leaseCpuSeconds
                << ",\"write_cpu_seconds\":" << p.writeCpuSeconds << ",\"range_cpu_seconds\":" << p.rangeCpuSeconds
                << ",\"physical_write_cpu_seconds\":" << p.physicalWriteCpuSeconds
                << ",\"push_cpu_seconds\":" << p.pushCpuSeconds << ",\"pop_cpu_seconds\":" << p.popCpuSeconds << '}';
        } else out << "null";
        out << ",\"receiver_payload_observation\":";
        if (t.receiverPayloadObservationEnabled && !t.sender) {
            const auto& p = t.receiverPayloadObservation;
            out << "{\"schema\":\"cpnetflux_receiver_payload_observation_v2\","
                << "\"scope\":\"local DATA processing; allocations count DATA capacity growth, reuse retains a connection-owned frame; excludes control-frame growth; whole-file write counts exclude range writer\","
                << "\"clock\":\"steady_clock seconds; copy/shift/decode/CRC are nested in checksum_seconds; storage writes nested in write_seconds or flush; do not add aggregates\","
                << "\"capacity_scope\":\"capacity_bytes sums retained capacity per DATA; allocated_capacity_bytes sums new capacity on DATA growth; peak is retained DATA capacity, not RSS; excludes control growth, decoder and RDMA\","
                << "\"data_frame_count\":" << p.dataFrameCount
                << ",\"frame_payload_allocations\":" << p.framePayloadAllocations
                << ",\"frame_payload_reuses\":" << p.framePayloadReuses
                << ",\"frame_payload_allocated_capacity_bytes\":" << p.framePayloadAllocatedCapacityBytes
                << ",\"frame_payload_peak_capacity_bytes\":" << p.framePayloadPeakCapacityBytes
                << ",\"frame_payload_bytes\":" << p.framePayloadBytes
                << ",\"frame_payload_capacity_bytes\":" << p.framePayloadCapacityBytes
                << ",\"logical_buffer_allocations\":" << p.logicalBufferAllocations
                << ",\"logical_copy_bytes\":" << p.logicalCopyBytes
                << ",\"logical_copy_seconds\":" << p.logicalCopySeconds
                << ",\"prefix_shift_calls\":" << p.prefixShiftCalls
                << ",\"prefix_shift_bytes\":" << p.prefixShiftBytes
                << ",\"prefix_shift_seconds\":" << p.prefixShiftSeconds
                << ",\"decompression_calls\":" << p.decompressionCalls
                << ",\"decompression_seconds\":" << p.decompressionSeconds
                << ",\"frame_crc_bytes\":" << p.frameCrcBytes
                << ",\"frame_crc_seconds\":" << p.frameCrcSeconds
                << ",\"whole_file_write_at_all_calls\":" << p.wholeFileWrite.writeAtAllCalls
                << ",\"whole_file_write_at_all_bytes\":" << p.wholeFileWrite.writeAtAllBytes
                << ",\"whole_file_write_at_all_failures\":" << p.wholeFileWrite.writeAtAllFailures
                << ",\"whole_file_write_at_all_seconds\":" << p.wholeFileWrite.writeAtAllSeconds << '}';
        } else {
            out << "null";
        }
        out << ",\"whole_file_observation\":";
        if (t.wholeFileObservationEnabled) {
            const auto& whole = t.wholeFileObservation;
            out << "{\"schema\":\"cpnetflux_whole_file_observation_v1\","
                << "\"scope\":\"local whole-file session begin/write/commit; excludes preflight, callbacks and published-output revalidation\","
                << "\"clock\":\"steady_clock; lookup excludes nested save; serialize substeps are disjoint subsets of serialize_seconds; session sort precedes save; do not add aggregates\","
                << "\"record_lookup_calls\":" << whole.records.lookupCalls
                << ",\"record_candidates_examined\":" << whole.records.candidateRecordsExamined
                << ",\"record_lookup_seconds\":" << whole.records.lookupSeconds
                << ",\"session_sort_seconds\":" << whole.records.sortSeconds
                << ",\"manifest_save\":";
            appendManifestSaveObservation(out, whole.manifestSave);
            out << '}';
        } else {
            out << "null";
        }
        const auto& r = t.rangeObservation;
        const auto& m = r.manifestSave;
        out << ",\"range_observation\":{\"schema_version\":2,\"data_frame_count\":" << r.dataFrameCount
            << ",\"effective_data_bytes\":" << r.effectiveDataBytes
            << ",\"range_write_batch_bytes\":" << r.rangeWriteBatchBytes
            << ",\"parallel_direct_write_calls\":" << r.parallelDirectWriteCalls
            << ",\"serialized_direct_write_calls\":" << r.serializedDirectWriteCalls
            << ",\"bounded_direct_write_calls\":" << r.boundedDirectWriteCalls
            << ",\"direct_write_concurrency\":" << r.directWriteConcurrency
            << ",\"direct_write_wait_seconds\":" << r.directWriteWaitSeconds
            << ",\"reclaim_wait_calls\":" << r.reclaimWaitCalls
            << ",\"reclaim_wait_seconds\":" << r.reclaimWaitSeconds
            << ",\"entry_lock_calls\":" << r.entryLockCalls
            << ",\"entry_lock_wait_seconds\":" << r.entryLockWaitSeconds
            << ",\"entry_lock_hold_seconds\":" << r.entryLockHoldSeconds
            << ",\"offset_write_calls\":" << r.offsetWriteCalls
            << ",\"offset_write_seconds\":" << r.offsetWriteSeconds
            << ",\"writer_call_count\":" << r.writerCallCount
            << ",\"writer_call_seconds\":" << r.writerCallSeconds
            << ",\"file_write_at_all_calls\":" << r.fileWrite.writeAtAllCalls
            << ",\"file_write_at_all_bytes\":" << r.fileWrite.writeAtAllBytes
            << ",\"file_write_at_all_failures\":" << r.fileWrite.writeAtAllFailures
            << ",\"file_write_at_all_seconds\":" << r.fileWrite.writeAtAllSeconds
            << ",\"data_flush_calls\":" << r.dataFlushCalls
            << ",\"data_flush_seconds\":" << r.dataFlushSeconds
            << ",\"chunk_flush_calls\":" << r.chunkFlushCalls
            << ",\"chunk_flush_seconds\":" << r.chunkFlushSeconds
            << ",\"chunk_checksum_seconds\":" << r.chunkChecksumSeconds
            << ",\"checkpoint_seconds\":" << r.checkpointSeconds
            << ",\"checkpoint_manifest_flush_calls\":" << r.checkpointManifestFlushCalls
            << ",\"checkpoint_manifest_flush_seconds\":" << r.checkpointManifestFlushSeconds
            << ",\"background_checkpoint_thread_cpu_seconds\":" << r.backgroundCheckpointCpuSeconds
            << ",\"background_checkpoint_cpu_seconds\":" << r.backgroundCheckpointCpuSeconds
            << ",\"manifest_save\":";
        appendManifestSaveObservation(out, m);
        out << '}';
        out << ",\"wall_seconds\":" << t.wallSeconds << '}';
    }
    out << ']';
}
void appendRangeObservationContract(std::ostream& out) {
    out <<
        R"obs({"schema":"cpnetflux_range_observation_v2",)obs"
        R"obs("default_switch":"CPNETFLUX_RANGE_TIMING (on by default when phase timing is enabled; off disables detailed range timers and counters)",)obs"
        R"obs("clock":"steady_clock elapsed wall time unless metric unit says thread_cpu_seconds",)obs"
        R"obs("common_scope":"DATA and writer metrics belong to one receiver range attempt; Entry-lock and deferred checkpoint metrics are accumulated per file entry and merged into the next reporting range, so they are not exact range attribution; concurrent timer sums can exceed transfer wall time",)obs"
        R"obs("metrics":{"data_frame_count":{"unit":"frames","scope":"successfully validated DATA frames accepted by this range writer","overlap":"not applicable"},)obs"
        R"obs("effective_data_bytes":{"unit":"logical bytes","scope":"decompressed payload after checksum prefix removal, accepted by this range writer","overlap":"not applicable"},)obs"
        R"obs("range_write_batch_bytes":{"unit":"bytes","scope":"effective BufferedFileWriter contiguous buffer configured for this range; zero means batching is disabled or direct writes are selected","overlap":"configuration value, not elapsed time"},)obs"
        R"obs("parallel_direct_write_calls":{"unit":"calls","scope":"POSIX direct writeRange attempts executed outside Entry, including file_serial/file_dual; not certified progress; zero for serialized backends or serial diagnostic override","overlap":"storage interval is outside entry_lock_hold_seconds"},)obs"
        R"obs("serialized_direct_write_calls":{"unit":"calls","scope":"subset of parallel_direct_write_calls admitted through the per-file physical IO gate in explicit file_serial mode","overlap":"storage interval is outside Entry"},)obs"
        R"obs("bounded_direct_write_calls":{"unit":"calls","scope":"subset of parallel_direct_write_calls admitted through the per-file two-permit physical IO gate in file_dual mode","overlap":"storage interval is outside Entry"},)obs"
        R"obs("direct_write_concurrency":{"unit":"operations","scope":"per-file physical IO cap; 1=file_serial, 2=file_dual, 0=no extra cap or ineligible backend","overlap":"configuration value, not observed concurrency"},)obs"
        R"obs("direct_write_wait_seconds":{"unit":"seconds","scope":"per-file physical IO gate acquisition wall time, summed over file_serial/file_dual attempts","overlap":"excluded from Entry wait/hold and writer_call_seconds; concurrent waits overlap one another"},)obs"
        R"obs("reclaim_wait_calls":{"unit":"calls","scope":"higher-attempt begin calls that waited for the preceding slot write to drain","overlap":"not applicable"},)obs"
        R"obs("reclaim_wait_seconds":{"unit":"seconds","scope":"condition wait and mutex reacquisition during those reclaim waits","overlap":"excluded from entry_lock_wait_seconds and entry_lock_hold_seconds"},)obs"
        R"obs("entry_lock_calls":{"unit":"calls","scope":"Explicit Entry mutex acquisitions and reclaim wait returns during this range plus deferred checkpoint acquisitions merged since the preceding reporting boundary","overlap":"not applicable"},)obs"
        R"obs("entry_lock_wait_seconds":{"unit":"seconds","scope":"steady_clock wait summed over those Entry lock acquisitions","overlap":"may overlap other ranges and range wall time"},)obs"
        R"obs("entry_lock_hold_seconds":{"unit":"seconds","scope":"steady_clock hold summed over those Entry lock acquisitions; excludes unlocked direct writes and reclaim condition waits","overlap":"serialized mode contains writer; direct parallel mode excludes unlocked storage; includes data flush, record update and manifest flush"},)obs"
        R"obs("writer_call_count":{"unit":"calls","scope":"Range writer attempts for this range; POSIX direct independent mode uses stateless writeAtAll","overlap":"not applicable"},)obs"
        R"obs("writer_call_seconds":{"unit":"seconds","scope":"range writeRange storage elapsed time summed for this range; direct parallel storage interval is outside Entry","overlap":"contains file_write_at_all_seconds when a write occurs"},)obs"
        R"obs("file_write_at_all_calls":{"unit":"calls","scope":"lower-level storage writeAtAll attempts reached by this range","overlap":"not applicable"},)obs"
        R"obs("file_write_at_all_bytes":{"unit":"requested bytes","scope":"length requested by each writeAtAll attempt, including failed attempts; when BufferedFileWriter coalesces data, bytes may originate from earlier range calls","overlap":"not applicable"},)obs"
        R"obs("file_write_at_all_failures":{"unit":"calls","scope":"writeAtAll attempts that returned failure","overlap":"not applicable"},)obs"
        R"obs("file_write_at_all_seconds":{"unit":"seconds","scope":"writeAtAll elapsed wall time for the attempt, attributed to the call that triggered it; buffered bytes may have been staged by earlier range calls","overlap":"nested inside writer_call_seconds or data_flush_seconds"},)obs"
        R"obs("data_flush_calls":{"unit":"calls","scope":"BufferedFileWriter::flush invocations at verified chunk boundaries; some may be no-op when the buffer is empty","overlap":"not applicable"},)obs"
        R"obs("data_flush_seconds":{"unit":"seconds","scope":"BufferedFileWriter::flush elapsed wall time; writes buffered bytes to the OS but does not call fsync/fdatasync","overlap":"may contain file_write_at_all_seconds"},)obs"
        R"obs("record_update_calls":{"unit":"calls","scope":"manifest verified-record update attempts","overlap":"not applicable"},)obs"
        R"obs("record_update_seconds":{"unit":"seconds","scope":"record update elapsed time with nested full manifest save time subtracted","overlap":"nested in entry_lock_hold_seconds; excludes measured saveAtomic intervals"},)obs"
        R"obs("checkpoint_manifest_flush_calls":{"unit":"calls","scope":"synchronous and deferred flushManifest attempts observed for this file entry","overlap":"not applicable"},)obs"
        R"obs("checkpoint_manifest_flush_seconds":{"unit":"seconds","scope":"synchronous and deferred flushManifest elapsed wall time observed for this file entry","overlap":"contains nested manifest_save phase timers"},)obs"
        R"obs("manifest_save_attempts":{"unit":"calls","scope":"synchronous and deferred saveAtomic attempts, including failed attempts, observed for this file entry","overlap":"not applicable"},)obs"
        R"obs("manifest_serialize_calls":{"unit":"calls","scope":"synchronous and deferred manifest serialization attempts observed for this file entry","overlap":"not applicable"},)obs"
        R"obs("manifest_serialized_bytes":{"unit":"bytes","scope":"synchronous and deferred serialized manifest bytes produced for this file entry","overlap":"not applicable"},)obs"
        R"obs("manifest_save_seconds":{"unit":"seconds","scope":"synchronous and deferred whole saveAtomic elapsed wall time observed for this file entry","overlap":"contains serialize, temp-write and publication timers"},)obs"
        R"obs("manifest_temp_write_calls":{"unit":"calls","scope":"synchronous and deferred temporary manifest writeAll attempts after temp file open","overlap":"not applicable"},)obs"
        R"obs("manifest_temp_write_seconds":{"unit":"seconds","scope":"synchronous and deferred temporary manifest open/write/close interval","overlap":"part of manifest_save_seconds and checkpoint_manifest_flush_seconds"},)obs"
        R"obs("manifest_publish_calls":{"unit":"calls","scope":"synchronous and deferred manifest rename publication attempts","overlap":"not applicable"},)obs"
        R"obs("manifest_publish_successes":{"unit":"calls","scope":"synchronous and deferred successful manifest rename publications","overlap":"not applicable"},)obs"
        R"obs("manifest_publish_seconds":{"unit":"seconds","scope":"elapsed wall time for manifest rename publication; does not include directory fsync or imply durable-media completion","overlap":"part of manifest_save_seconds and checkpoint_manifest_flush_seconds"},)obs"
        R"obs("manifest_failures":{"unit":"calls","scope":"synchronous and deferred failed saveAtomic attempts","overlap":"not applicable"},)obs"
        R"obs("background_checkpoint_thread_cpu_seconds":{"unit":"thread_cpu_seconds","scope":"CLOCK_THREAD_CPUTIME_ID from before Entry mutex acquisition through deferred flush/status handling; merged at a later receiver boundary and not exact per-range attribution","overlap":"not a wall timer; do not add to elapsed timers"}},)obs"
        R"obs("legacy_timers":{"checkpoint_seconds":"compatibility aggregate described under additional_metrics; do not add to subintervals",)obs"
        R"obs("chunk_flush_seconds":"compatibility alias listed in metric_aliases",)obs"
        R"obs("background_checkpoint_cpu_seconds":"compatibility alias listed in metric_aliases",)obs"
        R"obs("manifest_rename_calls":"compatibility alias listed in metric_aliases",)obs"
        R"obs("manifest_rename_seconds":"compatibility alias listed in metric_aliases"},)obs"
        R"obs("metric_aliases":{"offset_write_calls":"alias of writer_call_count",)obs"
        R"obs("offset_write_seconds":"alias of writer_call_seconds",)obs"
        R"obs("chunk_flush_calls":"alias of data_flush_calls",)obs"
        R"obs("chunk_flush_seconds":"alias of data_flush_seconds",)obs"
        R"obs("manifest_rename_calls":"alias of manifest_publish_calls",)obs"
        R"obs("manifest_rename_seconds":"alias of manifest_publish_seconds",)obs"
        R"obs("background_checkpoint_cpu_seconds":"alias of background_checkpoint_thread_cpu_seconds"},)obs"
        R"obs("additional_metrics":{"chunk_checksum_seconds":{"unit":"seconds","scope":"checksum combination/finalization for a verified chunk","overlap":"inside Entry lock hold and record update path; overlaps legacy checkpoint_seconds"},)obs"
        R"obs("checkpoint_seconds":{"unit":"seconds","scope":"legacy aggregate around range record/checkpoint work","overlap":"contains record_update_seconds and checkpoint_manifest_flush_seconds; do not add"},)obs"
        R"obs("manifest_serialize_seconds":{"unit":"seconds","scope":"synchronous and deferred manifest serialization elapsed wall time","overlap":"part of manifest_save_seconds and checkpoint_manifest_flush_seconds"},)obs"
        R"obs("manifest_temp_write_seconds":{"unit":"seconds","scope":"synchronous and deferred temp manifest open/write/close elapsed wall time","overlap":"part of manifest_save_seconds and checkpoint_manifest_flush_seconds"}}})obs";
}
}  // namespace cpnetflux::core::io
