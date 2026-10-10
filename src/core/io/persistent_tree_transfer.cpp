#include "cpnetflux/core/io/persistent_tree_transfer.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <variant>
#include <unordered_set>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <ostream>
#include <iomanip>
#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/checkpoint/download_manifest.h"
#include "cpnetflux/core/session/transfer_session.h"
#include "cpnetflux/core/session/download_session.h"
#include "cpnetflux/core/tree/tree_scan.h"
#include "cpnetflux/storage/posix_file.h"

namespace cpnetflux::core::io {
namespace {
using common::Status;
using protocol::FrameStatusCode;
using protocol::FrameType;
namespace fs = std::filesystem;
using Session = std::variant<session::TransferSession, session::DownloadSession>;
using Clock = std::chrono::steady_clock;
double secondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }
struct Timer {
    double* value; Clock::time_point start = Clock::now();
    ~Timer() { if (value) *value += secondsSince(start); }
};

class FileTransaction {
 public:
    explicit FileTransaction(PersistentFileTiming* timing) : timing_(timing) {}
    Status begin(const std::string& root, const PersistentFileIdentity& id,
                 bool download, const std::string& remoteRoot, bool resume) {
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
        output_ = (parent / relative.filename()).string();
        const auto existing = fs::symlink_status(output_, error);
        if (existing.type() != fs::file_type::not_found) {
            if (!resume || !download || error || !fs::is_regular_file(existing))
                return Status::invalidArgument("persistent output already exists");
            std::error_code sizeError;
            const auto size = fs::file_size(output_, sizeError);
            if (sizeError || size != id.totalSize) return Status::invalidArgument("persistent output identity mismatch");
            skipped_ = true;
            identity_ = id;
            active_ = true;
            return Status::ok();
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
                    sessionTransferId, id.totalSize, id.chunkSize, id.checksumAlgorithm);
                if (!resumed.isOk()) return resumed.status();
                session_ = std::move(resumed.value());
            } else {
                auto resumed = session::TransferSession::resume(output_, id.transferId,
                    id.totalSize, id.chunkSize, id.checksumAlgorithm);
                if (!resumed.isOk()) return resumed.status();
                session_ = std::move(resumed.value());
            }
        } else if (download) {
            auto created = session::DownloadSession::createNew(output_,
                (fs::path(remoteRoot) / relative).lexically_normal().generic_string(), id.transferId, id.totalSize,
                id.chunkSize, id.checksumAlgorithm);
            if (!created.isOk()) return created.status();
            session_ = std::move(created.value());
        } else {
            auto created = session::TransferSession::createNew(output_, id.transferId, id.totalSize,
                id.chunkSize, id.checksumAlgorithm);
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
        checksumComputer_ = checksum::ChecksumComputer(id.checksumAlgorithm);
        auto resized = file_.resize(id.totalSize);
        if (!resized.isOk()) return resized;
        Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
        return resume ? common::Status::ok() : std::visit([](auto& s) { return s.save(); }, session_);
    }
    Status write(std::uint64_t offset, const std::vector<std::uint8_t>& bytes) {
        if (skipped_) return Status::ok();
        const auto writing = Clock::now();
        auto status = file_.writeAtAll(offset, bytes.data(), bytes.size());
        if (timing_) timing_->writeSeconds += secondsSince(writing);
        if (!status.isOk()) return status;
        const auto end = offset + bytes.size();
        std::size_t consumed = 0;
        while (consumed < bytes.size()) {
            const auto chunkOffset = (offset + consumed) / identity_.chunkSize * identity_.chunkSize;
            if (chunkOffset != recorded_) return Status::invalidArgument("persistent chunk order mismatch");
            const auto length = std::min<std::uint64_t>(identity_.chunkSize, identity_.totalSize - recorded_);
            const auto part = std::min<std::size_t>(length - chunkBytes_, bytes.size() - consumed);
            checksumComputer_.update(bytes.data() + consumed, part);
            chunkBytes_ += part;
            consumed += part;
            if (chunkBytes_ != length) continue;
            Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
            status = std::visit([&](auto& s) { return s.recordVerifiedChunk(
                recorded_ / identity_.chunkSize, recorded_, length,
                checksumComputer_.finalize()); }, session_);
            if (!status.isOk()) return status;
            recorded_ += length;
            chunkBytes_ = 0;
            checksumComputer_ = checksum::ChecksumComputer(identity_.checksumAlgorithm);
        }
        return Status::ok();
    }
    Status commit() {
        if (skipped_) { active_ = false; return Status::ok(); }
        timespec times[2]{{0, UTIME_OMIT}, {identity_.mtimeUnixSeconds, 0}};
        if (::futimens(file_.fd(), times) != 0) return Status::runtimeError("persistent mtime failed");
        common::Status status;
        { Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
          status = std::visit([](auto& s) { return s.flushManifest(); }, session_); }
        if (!status.isOk()) return status;
        // Atomic no-replace publication: a raced destination must never be overwritten.
        if (::link(temp_.c_str(), output_.c_str()) != 0)
            return Status::runtimeError("persistent publish failed (output exists or IO error)");
        published_ = true;
        { Timer timer{timing_ ? &timing_->manifestSeconds : nullptr};
          status = std::visit([](auto& s) { return s.markCommitted(); }, session_); }
        if (!status.isOk()) return status;
        if (::unlink(temp_.c_str()) != 0) return Status::runtimeError("persistent temp unlink failed");
        active_ = false;
        return Status::ok();
    }
    ~FileTransaction() {
        // Partial files/checkpoints stay available to the existing v1 resume engine.
        if (active_ && !published_ && !skipped_) (void)std::visit([](auto& s) { return s.markFailed(); }, session_);
    }
 private:
    PersistentFileTiming* timing_;
    Session session_;
    storage::PosixFile file_;
    PersistentFileIdentity identity_;
    checksum::ChecksumComputer checksumComputer_{checksum::ChecksumAlgorithm::None};
    std::string output_, temp_;
    std::uint64_t recorded_ = 0;
    std::uint64_t chunkBytes_ = 0;
    bool active_ = false, published_ = false, skipped_ = false;
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
    std::uint32_t channelIndex, std::uint32_t channelCount, DynamicFileQueue* ready) {
    if (!stats) return Status::invalidArgument("missing persistent stats");
    if (!socket || !socket->valid()) return Status::invalidArgument("invalid persistent data socket");
    if (pendingWindow == 0 || pendingWindow > 16)
        return Status::invalidArgument("persistent pending window must be in range 1..16");
    if (channelCount == 0 || channelCount > kPersistentTreeMaxChannels ||
        channelIndex >= channelCount)
        return Status::invalidArgument("persistent channel index/count is invalid");
    stats->pendingWindow = pendingWindow;
    auto status = PersistentDataSession::setTimeout(socket, 30);
    if (!status.isOk()) return status;
    std::vector<std::uint8_t> buffer(PersistentDataSession::kMaxPayload);
    struct PendingFile { PersistentFileIdentity id; PersistentFileTiming timing; Clock::time_point started, resultStarted; };
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<PendingFile> pending;
    std::mutex callbackMutex;
    Status readerStatus = Status::ok();
    Status overall = Status::ok();
    std::uint64_t writerWireBytes = 0;
    std::uint64_t readerWireBytes = 0;
    bool writerDone = false;
    bool abort = false;
    auto requestAbort = [&](const Status& failure) {
        if (ready) ready->cancel(failure);
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
            PersistentFileTiming timing;
            Clock::time_point fileStarted, resultStarted;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return abort || !pending.empty() || writerDone; });
                if (abort) return;
                if (pending.empty() && writerDone) return;
                expected = pending.front().id;
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
            const auto fileStatus = result.value() == FrameStatusCode::Ok ? Status::ok() :
                Status::runtimeError("persistent receiver rejected file: " + expected.relativePath);
            if (ready) {
                auto completed = ready->complete(channelIndex, expected);
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
                ++stats->files;
                if (fileStatus.isOk()) stats->bytes += expected.totalSize;
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
            if (ready && !fileStatus.isOk()) { requestAbort(fileStatus); return; }
        }
    });
    auto failWriter = [&](const Status& failure) {
        requestAbort(failure);
        if (socket->valid()) (void)::shutdown(socket->fd(), SHUT_RDWR);
    };
    std::size_t nextFile = 0;
    for (;;) {
        PersistentFileIdentity id;
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
            auto next = ready->claim(channelIndex);
            if (!next.isOk()) { failWriter(next.status()); break; }
            if (!next.value()) break;
            id = next.value()->file;
            timing.queueWaitSeconds = next.value()->queueWaitSeconds;
            stats->queueWaitSeconds += next.value()->queueWaitSeconds;
        }
        timing.file = id; timing.channel = channelIndex; timing.sender = true;
        const auto fileStarted = Clock::now();
        status = tree::validateTreeRelativePath(id.relativePath);
        if (!status.isOk()) { failWriter(status); break; }
        UniqueFd fd(::open((fs::path(root) / id.relativePath).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
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
        status = PersistentDataSession::writeBegin(socket, id);
        if (!status.isOk()) { failWriter(status); break; }
        const auto metadata = protocol::encodeSessionInitPayload({protocol::SessionMode::New,
            id.transferId, id.totalSize, id.chunkSize, id.checksumAlgorithm, id.relativePath});
        if (!metadata.isOk()) { failWriter(metadata.status()); break; }
        addWire(protocol::kFrameHeaderSize + metadata.value().size() + 8);
        bool writeFailed = false;
        checksum::ChecksumComputer checksumComputer(id.checksumAlgorithm);
        for (std::uint64_t offset = 0; offset < id.totalSize;) {
            const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), id.totalSize - offset));
            const auto reading = Clock::now();
            status = file.readAtAll(offset, buffer.data(), length);
            timing.readSeconds += secondsSince(reading);
            if (!status.isOk()) { writeFailed = true; break; }
            const auto checksumStarted = Clock::now();
            checksumComputer.update(buffer.data(), length);
            timing.checksumSeconds += secondsSince(checksumStarted);
            const auto payloadStarted = Clock::now();
            status = PersistentDataSession::writeData(socket, id, offset, buffer.data(), length);
            timing.payloadIoSeconds += secondsSince(payloadStarted);
            if (status.isOk() && !timing.firstPayloadSeconds) timing.firstPayloadSeconds = secondsSince(fileStarted);
            if (!status.isOk()) { writeFailed = true; break; }
            offset += length;
            addWire(protocol::kFrameHeaderSize + length);
        }
        struct stat after{};
        if (!writeFailed && (::fstat(file.fd(), &after) != 0 || !sourceMatches(after, id) ||
            before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
            before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)) {
            status = Status::runtimeError("persistent source changed during transfer");
            writeFailed = true;
        }
        if (!writeFailed) status = PersistentDataSession::writeEnd(socket, id, checksumComputer.finalize());
        if (writeFailed || !status.isOk()) { failWriter(status.isOk() ? Status::runtimeError("persistent data write failed") : status); break; }
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending.push_back({id, std::move(timing), fileStarted, Clock::now()});
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
        status = PersistentDataSession::writeDirectoryEnd(socket, stats->files);
        addWire(protocol::kFrameHeaderSize);
        if (!status.isOk()) finalStatus = status;
    }
    stats->wireBytes += writerWireBytes + readerWireBytes;
    if (!finalStatus.isOk()) return finalStatus;
    return overall;
}

common::Status receivePersistentTree(FramedDataSocket* socket, const std::string& root,
    bool download, const std::string& remoteRoot, PersistentTreeStats* stats,
    const PersistentFileCallback& callback, std::uint32_t pendingWindow,
    std::uint32_t channelIndex, std::uint32_t channelCount,
    std::uint32_t expectedFileCount, bool dynamic, bool resume) {
    if (!stats) return Status::invalidArgument("missing persistent stats");
    if (!socket || !socket->valid()) return Status::invalidArgument("invalid persistent data socket");
    if (pendingWindow == 0 || pendingWindow > 16)
        return Status::invalidArgument("persistent pending window must be in range 1..16");
    if (channelCount == 0 || channelCount > kPersistentTreeMaxChannels ||
        channelIndex >= channelCount)
        return Status::invalidArgument("persistent channel index/count is invalid");
    stats->pendingWindow = pendingWindow; // Receiver stays serial; report negotiated sender credit.
    auto status = PersistentDataSession::setTimeout(socket, 30);
    if (!status.isOk()) return status;
    PersistentDataSession state(dynamic);
    Status overall = Status::ok();
    for (;;) {
        auto frame = PersistentDataSession::readNext(socket, PersistentDataSession::kMaxPayload);
        if (!frame.isOk()) return frame.status();
        stats->wireBytes += protocol::kFrameHeaderSize + frame.value().payload.size();
        if (frame.value().header.type == FrameType::DirectoryEnd) {
            if (frame.value().header.totalSize != stats->files)
                return Status::invalidArgument("persistent directory file count mismatch");
            if (expectedFileCount != kUnknownPersistentFileCount &&
                expectedFileCount != stats->files)
                return Status::invalidArgument("persistent channel file count mismatch");
            return overall;
        }
        auto decoded = PersistentDataSession::decodeBegin(frame.value());
        if (!decoded.isOk()) return decoded.status();
        const auto id = decoded.value();
        if (!dynamic && !persistentFileAssignedToChannel(id.fileId, channelIndex, channelCount))
            return Status::invalidArgument("persistent file arrived on wrong channel");
        status = state.begin(frame.value().header, id.relativePath, id.checksumAlgorithm);
        if (!status.isOk()) return status;
        PersistentFileTiming timing;
        timing.file = id; timing.channel = channelIndex;
        const auto fileStarted = Clock::now();
        const auto manifestStarted = Clock::now();
        status = notify(callback, id, Status::ok(), false);
        timing.manifestSeconds += secondsSince(manifestStarted);
        if (!status.isOk()) return status;
        FileTransaction transaction(stats->phaseTiming ? &timing : nullptr);
        Status fileStatus = transaction.begin(root, id, download, remoteRoot, resume);
        for (;;) {
            const auto payloadStarted = Clock::now();
            auto data = PersistentDataSession::readNext(socket, PersistentDataSession::kMaxPayload);
            if (!data.isOk()) {
                (void)notify(callback, id, data.status(), true);
                return data.status();
            }
            stats->wireBytes += protocol::kFrameHeaderSize + data.value().payload.size();
            if (data.value().header.type == FrameType::FileEnd) {
                auto ended = state.end(data.value().header, data.value().payload.data(), data.value().payload.size());
                if (!ended.isOk()) return ended.status();
                break;
            }
            timing.payloadIoSeconds += secondsSince(payloadStarted);
            if (!timing.firstPayloadSeconds) timing.firstPayloadSeconds = secondsSince(fileStarted);
            const auto checksumStarted = Clock::now();
            status = state.data(data.value().header, data.value().payload.size(), data.value().payload.data());
            timing.checksumSeconds += secondsSince(checksumStarted);
            if (!status.isOk()) return status;
            if (fileStatus.isOk()) fileStatus = transaction.write(data.value().header.offset, data.value().payload);
        }
        if (fileStatus.isOk()) { Timer timer{&timing.finalizeSeconds}; fileStatus = transaction.commit(); }
        ++stats->files;
        if (fileStatus.isOk()) stats->bytes += id.totalSize;
        else if (overall.isOk()) overall = fileStatus;
        const auto completedStarted = Clock::now();
        status = notify(callback, id, fileStatus, true);
        timing.manifestSeconds += secondsSince(completedStarted);
        if (!status.isOk()) return status;
        const auto resultStarted = Clock::now();
        status = PersistentDataSession::writeResult(socket, id,
            fileStatus.isOk() ? FrameStatusCode::Ok : FrameStatusCode::WriteFailed);
        if (!status.isOk()) return status;
        stats->wireBytes += protocol::kFrameHeaderSize;
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
            << ",\"size\":" << t.file.totalSize << ",\"channel_index\":" << t.channel << ",\"relative_path\":";
        quoted(out,t.file.relativePath);
        out << ",\"transfer_id\":"; quoted(out,t.file.transferId);
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
            << ",\"finalize_scope\":" << (t.sender ? "null" : "\"receiver_commit_includes_manifest_flush\"")
            << ",\"payload_io_seconds\":" << t.payloadIoSeconds
            << ",\"file_result_seconds\":" << t.fileResultSeconds
            << ",\"manifest_seconds\":" << t.manifestSeconds << ",\"checksum_seconds\":"
            << t.checksumSeconds << ",\"finalize_seconds\":";
        if (!t.sender) out << t.finalizeSeconds; else out << "null";
        out << ",\"wall_seconds\":" << t.wallSeconds << '}';
    }
    out << ']';
}
}  // namespace cpnetflux::core::io
