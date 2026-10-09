#include "cpnetflux/core/io/persistent_tree_transfer.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <variant>
#include <unordered_set>
#include "cpnetflux/checkpoint/manifest_store.h"
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

class FileTransaction {
 public:
    Status begin(const std::string& root, const PersistentFileIdentity& id,
                 bool download, const std::string& remoteRoot) {
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
        if (fs::symlink_status(output_, error).type() != fs::file_type::not_found)
            return Status::invalidArgument("persistent output already exists");
        if (download) {
            auto created = session::DownloadSession::createNew(output_,
                (fs::path(remoteRoot) / relative).lexically_normal().generic_string(), id.transferId, id.totalSize,
                id.chunkSize, checksum::ChecksumAlgorithm::None);
            if (!created.isOk()) return created.status();
            session_ = std::move(created.value());
        } else {
            auto created = session::TransferSession::createNew(output_, id.transferId, id.totalSize,
                id.chunkSize, checksum::ChecksumAlgorithm::None);
            if (!created.isOk()) return created.status();
            session_ = std::move(created.value());
        }
        temp_ = std::visit([](auto& s) { return s.manifest().tempPath; }, session_);
        const auto manifest = std::visit([](auto& s) { return s.manifestPath(); }, session_);
        if (fs::symlink_status(manifest, error).type() != fs::file_type::not_found)
            return Status::invalidArgument("persistent manifest already exists; use resume");
        auto file = storage::PosixFile::openReadWriteExclusive(temp_);
        if (!file.isOk()) return file.status();
        file_ = std::move(file.value());
        active_ = true;
        identity_ = id;
        auto resized = file_.resize(id.totalSize);
        if (!resized.isOk()) return resized;
        return std::visit([](auto& s) { return s.save(); }, session_);
    }
    Status write(std::uint64_t offset, const std::vector<std::uint8_t>& bytes) {
        auto status = file_.writeAtAll(offset, bytes.data(), bytes.size());
        if (!status.isOk()) return status;
        const auto end = offset + bytes.size();
        while (recorded_ < end) {
            const auto length = std::min(identity_.chunkSize, identity_.totalSize - recorded_);
            if (length > end - recorded_) break;
            status = std::visit([&](auto& s) { return s.recordVerifiedChunk(
                recorded_ / identity_.chunkSize, recorded_, length,
                checksum::ChecksumValue{checksum::ChecksumAlgorithm::None, 0}); }, session_);
            if (!status.isOk()) return status;
            recorded_ += length;
        }
        return Status::ok();
    }
    Status commit() {
        timespec times[2]{{0, UTIME_OMIT}, {identity_.mtimeUnixSeconds, 0}};
        if (::futimens(file_.fd(), times) != 0) return Status::runtimeError("persistent mtime failed");
        auto status = std::visit([](auto& s) { return s.flushManifest(); }, session_);
        if (!status.isOk()) return status;
        // Atomic no-replace publication: a raced destination must never be overwritten.
        if (::link(temp_.c_str(), output_.c_str()) != 0)
            return Status::runtimeError("persistent publish failed (output exists or IO error)");
        published_ = true;
        status = std::visit([](auto& s) { return s.markCommitted(); }, session_);
        if (!status.isOk()) return status;
        if (::unlink(temp_.c_str()) != 0) return Status::runtimeError("persistent temp unlink failed");
        active_ = false;
        return Status::ok();
    }
    ~FileTransaction() {
        // Partial files/checkpoints stay available to the existing v1 resume engine.
        if (active_ && !published_) (void)std::visit([](auto& s) { return s.markFailed(); }, session_);
    }
 private:
    Session session_;
    storage::PosixFile file_;
    PersistentFileIdentity identity_;
    std::string output_, temp_;
    std::uint64_t recorded_ = 0;
    bool active_ = false, published_ = false;
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
    const PersistentFileCallback& callback) {
    if (!stats) return Status::invalidArgument("missing persistent stats");
    auto status = PersistentDataSession::setTimeout(socket, 30);
    if (!status.isOk()) return status;
    std::vector<std::uint8_t> buffer(PersistentDataSession::kMaxPayload);
    Status overall = Status::ok();
    for (const auto& id : files) {
        status = tree::validateTreeRelativePath(id.relativePath);
        if (!status.isOk()) return status;
        UniqueFd fd(::open((fs::path(root) / id.relativePath).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (!fd.isValid()) return Status::runtimeError("open persistent source failed");
        struct stat before{};
        if (::fstat(fd.get(), &before) != 0 || !sourceMatches(before, id))
            return Status::runtimeError("persistent source changed before transfer");
        storage::PosixFile file(std::move(fd));
        status = notify(callback, id, Status::ok(), false);
        if (!status.isOk()) return status;
        status = PersistentDataSession::writeBegin(socket, id);
        if (!status.isOk()) return status;
        const auto metadata = protocol::encodeSessionInitPayload({protocol::SessionMode::New,
            id.transferId, id.totalSize, id.chunkSize, checksum::ChecksumAlgorithm::None, id.relativePath});
        if (!metadata.isOk()) return metadata.status();
        stats->wireBytes += protocol::kFrameHeaderSize + metadata.value().size() + 8;
        for (std::uint64_t offset = 0; offset < id.totalSize;) {
            const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), id.totalSize - offset));
            status = file.readAtAll(offset, buffer.data(), length);
            if (!status.isOk()) return status;
            status = PersistentDataSession::writeData(socket, id, offset, buffer.data(), length);
            if (!status.isOk()) return status;
            offset += length;
            stats->wireBytes += protocol::kFrameHeaderSize + length;
        }
        struct stat after{};
        if (::fstat(file.fd(), &after) != 0 || !sourceMatches(after, id) ||
            before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
            before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
            return Status::runtimeError("persistent source changed during transfer");
        status = PersistentDataSession::writeEnd(socket, id);
        if (!status.isOk()) return status;
        const auto started = std::chrono::steady_clock::now();
        auto result = PersistentDataSession::readResult(socket, id);
        stats->completeWaitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (!result.isOk()) return result.status();
        stats->wireBytes += 2 * protocol::kFrameHeaderSize;
        ++stats->files;
        auto fileStatus = result.value() == FrameStatusCode::Ok ? Status::ok() :
            Status::runtimeError("persistent receiver rejected file: " + id.relativePath);
        if (fileStatus.isOk()) stats->bytes += id.totalSize;
        else if (overall.isOk()) overall = fileStatus;
        status = notify(callback, id, fileStatus, true);
        if (!status.isOk()) return status;
    }
    status = PersistentDataSession::writeDirectoryEnd(socket, stats->files);
    stats->wireBytes += protocol::kFrameHeaderSize;
    return status.isOk() ? overall : status;
}

common::Status receivePersistentTree(FramedDataSocket* socket, const std::string& root,
    bool download, const std::string& remoteRoot, PersistentTreeStats* stats,
    const PersistentFileCallback& callback) {
    if (!stats) return Status::invalidArgument("missing persistent stats");
    auto status = PersistentDataSession::setTimeout(socket, 30);
    if (!status.isOk()) return status;
    PersistentDataSession state;
    Status overall = Status::ok();
    for (;;) {
        auto frame = PersistentDataSession::readNext(socket, PersistentDataSession::kMaxPayload);
        if (!frame.isOk()) return frame.status();
        stats->wireBytes += protocol::kFrameHeaderSize + frame.value().payload.size();
        if (frame.value().header.type == FrameType::DirectoryEnd) {
            if (frame.value().header.totalSize != stats->files)
                return Status::invalidArgument("persistent directory file count mismatch");
            return overall;
        }
        auto decoded = PersistentDataSession::decodeBegin(frame.value());
        if (!decoded.isOk()) return decoded.status();
        const auto id = decoded.value();
        status = state.begin(frame.value().header, id.relativePath);
        if (!status.isOk()) return status;
        status = notify(callback, id, Status::ok(), false);
        if (!status.isOk()) return status;
        FileTransaction transaction;
        Status fileStatus = transaction.begin(root, id, download, remoteRoot);
        for (;;) {
            auto data = PersistentDataSession::readNext(socket, PersistentDataSession::kMaxPayload);
            if (!data.isOk()) {
                (void)notify(callback, id, data.status(), true);
                return data.status();
            }
            stats->wireBytes += protocol::kFrameHeaderSize + data.value().payload.size();
            if (data.value().header.type == FrameType::FileEnd) {
                auto ended = state.end(data.value().header);
                if (!ended.isOk()) return ended.status();
                break;
            }
            status = state.data(data.value().header, data.value().payload.size());
            if (!status.isOk()) return status;
            if (fileStatus.isOk()) fileStatus = transaction.write(data.value().header.offset, data.value().payload);
        }
        if (fileStatus.isOk()) fileStatus = transaction.commit();
        ++stats->files;
        if (fileStatus.isOk()) stats->bytes += id.totalSize;
        else if (overall.isOk()) overall = fileStatus;
        status = notify(callback, id, fileStatus, true);
        if (!status.isOk()) return status;
        status = PersistentDataSession::writeResult(socket, id,
            fileStatus.isOk() ? FrameStatusCode::Ok : FrameStatusCode::WriteFailed);
        if (!status.isOk()) return status;
        stats->wireBytes += protocol::kFrameHeaderSize;
    }
}
}  // namespace cpnetflux::core::io
