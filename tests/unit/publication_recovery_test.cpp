#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/core/session/download_session.h"
#include "cpnetflux/core/session/transfer_session.h"
#include "cpnetflux/checkpoint/download_manifest.h"
#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/storage/posix_file.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace cpnetflux;
using namespace cpnetflux::core::io;
namespace fs = std::filesystem;

namespace {
std::string faultManifest, faultTemp, replacement, replacementOutput;
std::atomic<bool> failRename{false}, failUnlink{false}, failSync{false}, replaceOnSave{false};
std::atomic<unsigned> syncCalls{0};
}
extern "C" int __real_rename(const char*, const char*);
extern "C" int __real_unlink(const char*);
extern "C" int __real_fsync(int);
extern "C" int __wrap_rename(const char* from, const char* to) {
    if (faultManifest == to && failRename.exchange(false)) { errno = EIO; return -1; }
    const auto result = __real_rename(from, to);
    if (result == 0 && faultManifest == to && replaceOnSave.exchange(false))
        (void)__real_rename(replacement.c_str(), replacementOutput.c_str());
    return result;
}
extern "C" int __wrap_unlink(const char* path) {
    if (faultTemp == path && failUnlink.exchange(false)) { errno = EACCES; return -1; }
    return __real_unlink(path);
}
extern "C" int __wrap_fsync(int fd) {
    ++syncCalls;
    if (failSync.exchange(false)) { errno = EIO; return -1; }
    return __real_fsync(fd);
}

namespace {
void require(common::Status s) { if (!s.isOk()) throw std::runtime_error(s.message()); }
std::string text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
struct Fixture {
    fs::path root;
    std::string output, temp, manifest;
    bool download;
    PersistentFileIdentity id{1, 7, 4096, "file.bin", "publication31", 2048, 12,
        checksum::ChecksumAlgorithm::Crc32c};
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(4096, 37);
    explicit Fixture(bool d, bool linked = true, bool committed = false,
                     checksum::ChecksumAlgorithm algorithm = checksum::ChecksumAlgorithm::Crc32c) : download(d) {
        char pattern[] = "/tmp/cpnetflux-publication-unit-XXXXXX";
        const auto directory = ::mkdtemp(pattern);
        if (!directory) throw std::runtime_error("mkdtemp");
        root = directory; output = (root / id.relativePath).string(); id.checksumAlgorithm = algorithm;
        if (download) {
            auto created = core::session::DownloadSession::createNew(output, "remote/tree/file.bin",
                id.transferId, id.totalSize, id.chunkSize, algorithm, checksum::ChecksumBackend::Auto,
                core::session::ManifestFlushPolicy::EveryNChunks, 16);
            if (!created.isOk()) throw std::runtime_error(created.status().message());
            stage(created.value(), linked, committed);
        } else {
            auto created = core::session::TransferSession::createNew(output, id.transferId,
                id.totalSize, id.chunkSize, algorithm, checksum::ChecksumBackend::Auto,
                core::session::ManifestFlushPolicy::EveryNChunks, 16);
            if (!created.isOk()) throw std::runtime_error(created.status().message());
            stage(created.value(), linked, committed);
        }
        faultManifest = manifest; faultTemp = temp;
    }
    ~Fixture() {
        failRename = failUnlink = failSync = replaceOnSave = false;
        faultManifest.clear(); faultTemp.clear();
        std::error_code ec; fs::remove_all(root, ec);
    }
    template<class Session> void stage(Session& session, bool linked, bool committed) {
        temp = session.manifest().tempPath; manifest = session.manifestPath();
        auto opened = storage::PosixFile::openReadWriteExclusive(temp);
        if (!opened.isOk()) throw std::runtime_error(opened.status().message());
        auto& file = opened.value(); require(file.resize(bytes.size())); require(session.save());
        require(file.writeAtAll(0, bytes.data(), bytes.size()));
        for (std::uint64_t offset = 0; offset < bytes.size(); offset += id.chunkSize) {
            checksum::ChecksumComputer crc(id.checksumAlgorithm); crc.update(bytes.data() + offset, id.chunkSize);
            require(session.recordVerifiedChunk(offset / id.chunkSize, offset, id.chunkSize, crc.finalize()));
        }
        timespec times[2]{{0, UTIME_OMIT}, {id.mtimeUnixSeconds, 0}};
        if (::futimens(file.fd(), times)) throw std::runtime_error("futimens");
        require(session.flushManifest());
        if (linked && ::link(temp.c_str(), output.c_str())) throw std::runtime_error("link");
        if (committed) { require(session.markCommitted()); fs::remove(temp); }
    }
    template<class Fn> void mutate(Fn fn) {
        if (download) {
            auto m = checkpoint::loadDownloadManifest(manifest);
            if (!m.isOk()) throw std::runtime_error(m.status().message());
            fn(m.value()); require(checkpoint::saveDownloadManifestAtomic(manifest, m.value()));
        } else {
            auto m = checkpoint::ManifestStore::load(manifest);
            if (!m.isOk()) throw std::runtime_error(m.status().message());
            fn(m.value()); require(checkpoint::ManifestStore::saveAtomic(manifest, m.value()));
        }
    }
    checkpoint::ManifestState state() const {
        if (download) return checkpoint::loadDownloadManifest(manifest).value().state;
        return checkpoint::ManifestStore::load(manifest).value().state;
    }
    struct Result { bool ok = false, accepted = false; std::uint64_t payload = 0, saves = 0; std::string error; };
    Result attempt(const PersistentTransferPolicy& policy = {}, bool ranges = false, bool cancel = false) {
        const bool alreadyPublished = fs::exists(output);
        int fds[2]; if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds)) throw std::runtime_error("socketpair");
        FramedDataSocket sender{UniqueFd(fds[0])}, receiver{UniqueFd(fds[1])};
        PersistentTreeStats stats; stats.phaseTiming = true; common::Status status;
        auto context = makePersistentTransferContext(policy);
        std::jthread worker([&] {
            status = receivePersistentTree(&receiver, root.string(), download, "remote/tree",
                &stats, {}, 1, 0, 1, 1, ranges, true, context);
        });
        struct Stop { FramedDataSocket& a; FramedDataSocket& b;
            ~Stop() { a.shutdown(); b.shutdown(); } } stop{sender, receiver};
        Result result;
        for (unsigned i = 0; i < (ranges ? 4U : 1U); ++i) {
            auto next = id;
            if (ranges) { next.rangeId = i + 1; next.rangeCount = 4; next.rangeOffset = i * 1024; next.rangeLength = 1024; }
            require(PersistentDataSession::writeBegin(&sender, next, true));
            if (cancel) { sender.shutdown(); break; }
            auto mask = PersistentDataSession::readResumeResponse(&sender, next);
            result.accepted = mask.isOk() && mask.value().statusCode == core::protocol::FrameStatusCode::Ok;
            if (!result.accepted) { sender.shutdown(); break; }
            EXPECT_TRUE(mask.value().missingRanges.empty());
            if (alreadyPublished)
                EXPECT_EQ(state(), checkpoint::ManifestState::Committed) << "must persist before empty-mask success";
            require(PersistentDataSession::writeEnd(&sender, next, {}, true));
            auto terminal = PersistentDataSession::readResult(&sender, next);
            if (!terminal.isOk()) throw std::runtime_error(terminal.status().message());
            EXPECT_EQ(terminal.value(), core::protocol::FrameStatusCode::Ok);
        }
        if (result.accepted) require(PersistentDataSession::writeDirectoryEnd(&sender, 1));
        worker.join(); result.ok = result.accepted && status.isOk();
        result.payload = stats.payloadBytes; result.error = status.message();
        for (const auto& timing : stats.fileTimings) result.saves += timing.wholeFileObservation.manifestSave.attempts;
        return result;
    }
};
class PublicationRecovery : public testing::TestWithParam<bool> {};

TEST_P(PublicationRecovery, LinkedTransferringResumesBeforeAcknowledgementAndIsIdempotent) {
    Fixture f(GetParam()); const auto bytes = text(f.output);
    struct stat before{}; ASSERT_EQ(::stat(f.output.c_str(), &before), 0);
    const auto first = f.attempt(); EXPECT_TRUE(first.ok) << first.error;
    EXPECT_EQ(first.payload, 0U); EXPECT_EQ(first.saves, 1U); EXPECT_FALSE(fs::exists(f.temp));
    EXPECT_EQ(f.state(), checkpoint::ManifestState::Committed);
    EXPECT_TRUE(f.attempt().ok); EXPECT_EQ(text(f.output), bytes);
    struct stat after{}; ASSERT_EQ(::stat(f.output.c_str(), &after), 0); EXPECT_EQ(before.st_ino, after.st_ino);
}
TEST_P(PublicationRecovery, BeforeLinkAndCommittedControlsStillResume) {
    for (bool committed : {false, true}) {
        Fixture f(GetParam(), committed, committed);
        EXPECT_TRUE(f.attempt().ok); EXPECT_EQ(f.state(), checkpoint::ManifestState::Committed);
        EXPECT_FALSE(fs::exists(f.temp));
    }
}
TEST_P(PublicationRecovery, SaveFailureDoesNotAcknowledgeOrRetireData) {
    Fixture f(GetParam()); failRename = true;
    EXPECT_FALSE(f.attempt().accepted); EXPECT_EQ(f.state(), checkpoint::ManifestState::Transferring);
    EXPECT_TRUE(fs::equivalent(f.temp, f.output)); EXPECT_TRUE(f.attempt().ok);
}
TEST_P(PublicationRecovery, UnlinkFailureRetriesCommittedAliasWithoutRetransmission) {
    Fixture f(GetParam()); failUnlink = true;
    EXPECT_FALSE(f.attempt().accepted); EXPECT_EQ(f.state(), checkpoint::ManifestState::Committed);
    EXPECT_TRUE(fs::equivalent(f.temp, f.output));
    EXPECT_TRUE(f.attempt().ok); EXPECT_FALSE(fs::exists(f.temp));
}
TEST_P(PublicationRecovery, RequestedSyncFailureKeepsTransferringThenRetries) {
    Fixture f(GetParam()); PersistentTransferPolicy policy;
    policy.options.commitSyncPolicy = core::session::CommitSyncPolicy::FsyncFileAndDir;
    syncCalls = 0; failSync = true;
    EXPECT_FALSE(f.attempt(policy).accepted); EXPECT_EQ(f.state(), checkpoint::ManifestState::Transferring);
    EXPECT_TRUE(fs::equivalent(f.temp, f.output)); EXPECT_TRUE(f.attempt(policy).ok); EXPECT_GE(syncCalls.load(), 3U);
}
TEST_P(PublicationRecovery, ReplacementDuringSaveCannotBeAcknowledgedOrUnlinked) {
    Fixture f(GetParam()); replacement = (f.root / "replacement").string(); replacementOutput = f.output;
    fs::copy_file(f.output, replacement); const auto bytes = text(f.output); replaceOnSave = true;
    EXPECT_FALSE(f.attempt().accepted); EXPECT_TRUE(fs::exists(f.temp));
    EXPECT_FALSE(fs::equivalent(f.temp, f.output)); EXPECT_EQ(text(f.output), bytes);
    EXPECT_FALSE(f.attempt().accepted);
}
TEST_P(PublicationRecovery, UnprovenAndCorruptStatesFailWithoutChangingArtifacts) {
    for (int kind = 0; kind != 17; ++kind) {
        SCOPED_TRACE(kind); Fixture f(GetParam());
        if (kind == 0) { auto value = text(f.manifest); value.at(value.find("state=transferring") + 6) = 'T'; std::ofstream(f.manifest) << value; }
        if (kind == 1) {
            { std::fstream out(f.output, std::ios::in | std::ios::out); out.put('X'); }
            timespec times[2]{{0, UTIME_OMIT}, {f.id.mtimeUnixSeconds, 0}};
            ASSERT_EQ(::utimensat(AT_FDCWD, f.output.c_str(), times, 0), 0);
        }
        if (kind == 2) f.mutate([](auto& m) { m.verifiedChunks.pop_back(); });
        if (kind == 3) { auto copy = f.root / "copy"; fs::copy_file(f.output, copy);
            fs::last_write_time(copy, fs::last_write_time(f.output)); fs::rename(copy, f.output); }
        if (kind == 4) fs::remove(f.temp);
        if (kind == 5) { fs::remove(f.temp); fs::create_symlink(f.output, f.temp); }
        if (kind == 6) { fs::remove(f.output); fs::create_symlink(f.temp, f.output); }
        if (kind == 7) f.mutate([](auto& m) { m.tempPath += ".other"; });
        if (kind == 8) f.mutate([](auto& m) { m.chunkSize *= 2; });
        if (kind == 9) f.id.mtimeUnixSeconds++;
        if (kind == 10) f.mutate([](auto& m) { m.state = checkpoint::ManifestState::Failed; });
        if (kind == 11) f.mutate([](auto& m) { m.transferId = "different-transfer"; });
        if (kind == 12) f.mutate([](auto& m) {
            if constexpr (requires { m.targetPath; }) m.targetPath += ".other";
            else m.outputPath += ".other";
        });
        if (kind == 13) f.mutate([](auto& m) {
            if constexpr (requires { m.sourcePath; }) m.sourcePath += ".other";
            else m.transferId = "other-source-identity";
        });
        if (kind == 14) { const auto copy = f.root / "sidecar-copy"; fs::copy_file(f.manifest, copy);
            fs::remove(f.manifest); fs::create_symlink(copy, f.manifest); }
        if (kind == 15) fs::resize_file(f.output, 2048);
        if (kind == 16) f.mutate([](auto& m) { m.verifiedChunks.front().checksum.value ^= 1; });
        const auto manifest = text(f.manifest), output = text(f.output), temp = text(f.temp);
        EXPECT_FALSE(f.attempt().accepted); EXPECT_EQ(text(f.manifest), manifest);
        EXPECT_EQ(text(f.output), output); EXPECT_EQ(text(f.temp), temp);
    }
}
TEST_P(PublicationRecovery, NoChecksumUncommittedStateIsNotPromoted) {
    Fixture f(GetParam(), true, false, checksum::ChecksumAlgorithm::None);
    EXPECT_FALSE(f.attempt().accepted); EXPECT_EQ(f.state(), checkpoint::ManifestState::Transferring);
}
TEST_P(PublicationRecovery, PeerCancellationPreservesRecoverableVerifiedData) {
    Fixture f(GetParam()); const auto bytes = text(f.output);
    EXPECT_FALSE(f.attempt({}, false, true).ok); EXPECT_EQ(text(f.output), bytes);
    EXPECT_TRUE(f.attempt().ok);
}
TEST_P(PublicationRecovery, FourRangesShareReconciledPublishedFile) {
    Fixture f(GetParam()); EXPECT_TRUE(f.attempt({}, true).ok); EXPECT_FALSE(fs::exists(f.temp));
}
TEST_P(PublicationRecovery, IncomingDownloadIdentityKeepsValidatedStoredLocalTransferId) {
    Fixture f(GetParam()); f.id.transferId = "new-remote-transfer";
    const auto result = f.attempt();
    EXPECT_EQ(result.ok, GetParam()) << result.error;
    if (GetParam()) EXPECT_FALSE(fs::exists(f.temp));
    else EXPECT_EQ(f.state(), checkpoint::ManifestState::Transferring);
}
INSTANTIATE_TEST_SUITE_P(UploadDownload, PublicationRecovery, testing::Bool());
}  // namespace
