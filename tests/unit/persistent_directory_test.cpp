#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <sys/socket.h>
#include <unistd.h>
#include "cpnetflux/core/io/persistent_directory.h"
#include "cpnetflux/core/io/work_executor.h"
#include "cpnetflux/core/tree/tree_scan.h"
#include "cpnetflux/checksum/crc32c.h"
using namespace cpnetflux::core::io;
using namespace cpnetflux::core::tree;
using namespace cpnetflux::core::protocol;
namespace {
struct Fixture {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("cpnetflux-directory-unit-" + std::to_string(::getpid()));
    Fixture() { std::filesystem::remove_all(path); std::filesystem::create_directories(path); }
    ~Fixture() { std::filesystem::remove_all(path); }
};
struct Pair {
    FramedDataSocket tx,rx;
    Pair() { int fd[2]; if (::socketpair(AF_UNIX,SOCK_STREAM,0,fd)) throw std::runtime_error("socketpair");
        tx=FramedDataSocket(UniqueFd(fd[0])); rx=FramedDataSocket(UniqueFd(fd[1])); }
};
const std::string transferId(32,'a');
std::vector<std::uint8_t> packet(FrameHeader h, const std::string& path, bool corrupt=false) {
    std::vector<std::uint8_t> payload(transferId.begin(),transferId.end());
    payload.insert(payload.end(),path.begin(),path.end());
    h.payloadSize=payload.size()+4;
    auto encoded=encodeFrameHeader(h);
    std::vector<std::uint8_t> bytes(encoded.begin(),encoded.end());
    bytes.insert(bytes.end(),payload.begin(),payload.end());
    auto crc=cpnetflux::checksum::crc32c(bytes.data(),bytes.size());
    if (corrupt) crc ^= 1;
    for (int n=24;n>=0;n-=8) bytes.push_back(crc>>n);
    return bytes;
}
}
TEST(V3DirectoryTest, ScanIncludesEmptyAndIntermediateDirectories) {
    Fixture f;
    std::filesystem::create_directories(f.path/"a"/"nested-empty");
    std::filesystem::create_directory(f.path/"empty");
    std::ofstream(f.path/"a"/"file")<<"payload";
    auto scanned=scanLocalDirectories(f.path.string());
    ASSERT_TRUE(scanned.isOk())<<scanned.status().message();
    EXPECT_EQ(scanned.value(),(std::vector<std::string>{"a","a/nested-empty","empty"}));
}
TEST(V3DirectoryTest, SafeCreationRejectsTraversalSymlinksAndTypeConflicts) {
    Fixture f; auto root=f.path/"root",outside=f.path/"outside";
    std::filesystem::create_directory(root); std::filesystem::create_directory(outside);
    std::filesystem::create_directory_symlink(outside,root/"link");
    std::ofstream(root/"file")<<"unchanged";
    EXPECT_TRUE(ensureTreeDirectory(root.string(),"a/b/c").isOk());
    EXPECT_TRUE(ensureTreeDirectory(root.string(),"a/b/c").isOk());
    for (const auto& path:{"../outside/escape","/absolute","a/../escape","link/escape","file/escape","a//b","a/"})
        EXPECT_FALSE(ensureTreeDirectory(root.string(),path).isOk())<<path;
    EXPECT_FALSE(std::filesystem::exists(outside/"escape"));
    EXPECT_FALSE(ensureTreeDirectory((root/"link").string(),"escape").isOk());
}
TEST(V3DirectoryTest, ManifestPreservesSeparateCompletionFactsInBothSerializers) {
    TreeManifest m; m.rootLogicalPath="dataset";
    m.files={{"zero",0,0,transferId,TreeFileStatus::Completed,""}};
    m.directories={{"empty",transferId,TreeFileStatus::Pending,""}};
    EXPECT_FALSE(isTreeTransferComplete(m));
    auto original=serializeTreeManifest(m,TreeManifestSerializer::Original);
    auto buffered=serializeTreeManifest(m,TreeManifestSerializer::Buffered);
    ASSERT_TRUE(original.isOk()); ASSERT_TRUE(buffered.isOk()); EXPECT_EQ(original.value(),buffered.value());
    auto parsed=parseTreeManifest(original.value()); ASSERT_TRUE(parsed.isOk());
    ASSERT_EQ(parsed.value().directories.size(),1U); EXPECT_EQ(parsed.value().directories[0].relativePath,"empty");
    parsed.value().directories[0].status=TreeFileStatus::Completed;
    EXPECT_TRUE(isTreeTransferComplete(parsed.value()));
    m.version=1; m.directories.clear(); auto legacy=serializeTreeManifest(m); ASSERT_TRUE(legacy.isOk());
    auto input=parseTreeManifest(legacy.value()); ASSERT_TRUE(input.isOk());
    EXPECT_FALSE(isTreeTransferComplete(input.value()));
}
TEST(V3DirectoryTest, ManifestRejectsDuplicateDirectoryAndFileTypeConflict) {
    TreeManifest m; m.rootLogicalPath="dataset";
    m.directories={{"same",transferId},{"same",transferId}};
    EXPECT_FALSE(serializeTreeManifest(m).isOk());
    m.directories.resize(1);m.files={{"same",0,0,transferId}};
    EXPECT_FALSE(serializeTreeManifest(m).isOk());
}
TEST(V3DirectoryTest, TypedLeasesRejectChangingDirectoryToFile) {
    DynamicFileQueue queue(std::vector<WorkItem>{},1,1,1);
    auto worker=queue.worker(0);ASSERT_TRUE(worker.isOk()); WorkExecutor executor(queue,worker.value());
    PersistentFileIdentity identity;identity.fileId=1;identity.generation=1;identity.chunkSize=1;
    identity.relativePath="empty";identity.transferId=transferId;
    auto lease=executor.claimIncoming({identity,transferId,false,WorkKind::Directory}); ASSERT_TRUE(lease.isOk());
    auto forged=lease.value();forged.kind=WorkKind::File;
    EXPECT_FALSE(executor.begin(forged).isOk());
    ASSERT_TRUE(executor.begin(lease.value()).isOk());
    ASSERT_TRUE(executor.finalize(lease.value(),cpnetflux::common::Status::ok()).isOk());
    EXPECT_EQ(queue.outstanding(),0U);
}
TEST(V3DirectoryTest, PreambleTransfersOnlyDirectoriesAndConfirmsCompletion) {
    Fixture f; Pair pair;
    PersistentDirectoryTransfer source;source.entries={{"empty",transferId},{"nested",transferId},{"nested/leaf",transferId}};
    int begins=0,completed=0; PersistentDirectoryTransfer target;
    target.callback=[&](const auto&,bool done){if(done)++completed;else ++begins;return cpnetflux::common::Status::ok();};
    auto sender=std::async(std::launch::async,[&]{auto s=sendPersistentDirectories(&pair.tx,source,0,1,0,nullptr);pair.tx.shutdown();return s;});
    auto received=receivePersistentDirectories(&pair.rx,f.path.string(),target,0,1,nullptr);
    pair.rx.shutdown(); EXPECT_TRUE(received.isOk())<<received.message();
    auto sent=sender.get();EXPECT_TRUE(sent.isOk())<<sent.message();
    EXPECT_EQ(begins,3);EXPECT_EQ(completed,3);
    EXPECT_TRUE(std::filesystem::is_directory(f.path/"empty"));
    EXPECT_TRUE(std::filesystem::is_directory(f.path/"nested"/"leaf"));
}
TEST(V3DirectoryTest, CorruptUnsafeOverLimitAndWrongChannelMetadataAreRejected) {
    Fixture f;
    for (int scenario=0;scenario<5;++scenario) {
        Pair pair; FrameHeader h;h.type=FrameType::DirectoryEntry;h.streamId=1;h.chunkId=1;
        if(scenario==2)h.streamId=65537;
        if(scenario==3)h.streamId=1;
        auto bytes=packet(h,scenario==1?"../escape":"valid",scenario==0);
        if(scenario==4)bytes[0]^=1;
        ASSERT_TRUE(pair.tx.writeAll(bytes.data(),bytes.size()).isOk());pair.tx.shutdown();
        PersistentDirectoryTransfer transfer;
        EXPECT_FALSE(receivePersistentDirectories(&pair.rx,f.path.string(),transfer,scenario==3?1:0,2,nullptr).isOk());
        EXPECT_FALSE(std::filesystem::exists(f.path/"valid"));
    }
}
TEST(V3DirectoryTest, MissingEntryIsNotHiddenByMetadataEndCount) {
    Fixture f;Pair pair;FrameHeader h;h.type=FrameType::DirectoryMetadataEnd;h.totalSize=1;
    auto bytes=encodeFrameHeader(h);ASSERT_TRUE(pair.tx.writeAll(bytes.data(),bytes.size()).isOk());pair.tx.shutdown();
    PersistentDirectoryTransfer transfer;
    EXPECT_FALSE(receivePersistentDirectories(&pair.rx,f.path.string(),transfer,0,1,nullptr).isOk());
}

TEST(V3DirectoryTest, ManifestRejectsRegularFileAsDirectoryAncestor) {
    TreeManifest m; m.rootLogicalPath="dataset";
    m.files={{"parent",0,0,transferId}};
    m.directories={{"parent/empty",transferId}};
    EXPECT_FALSE(serializeTreeManifest(m).isOk());
}

TEST(V3DirectoryTest, DuplicateEntryAndMissingParentMetadataAreRejected) {
    Fixture f;
    for (bool duplicate : {false,true}) {
        Pair pair; FrameHeader h;h.type=FrameType::DirectoryEntry;h.streamId=1;h.chunkId=1;
        auto bytes=packet(h,duplicate?"repeated":"missing/leaf");
        ASSERT_TRUE(pair.tx.writeAll(bytes.data(),bytes.size()).isOk());
        if (duplicate) {
            h.streamId=2;bytes=packet(h,"repeated");
            ASSERT_TRUE(pair.tx.writeAll(bytes.data(),bytes.size()).isOk());
        }
        PersistentDirectoryTransfer transfer;
        auto status=receivePersistentDirectories(&pair.rx,f.path.string(),transfer,0,1,nullptr);
        EXPECT_FALSE(status.isOk());
        EXPECT_NE(status.message().find(duplicate?"duplicate":"parent"),std::string::npos)<<status.message();
        EXPECT_FALSE(std::filesystem::exists(f.path/"missing"));
    }
}
TEST(V3DirectoryTest, InterruptedPreambleReplaysActualDirectoriesWithFreshLeases) {
    Fixture f;PersistentDirectoryTransfer source;source.entries={{"first",transferId},{"second",transferId}};
    { Pair pair;PersistentDirectoryTransfer target;
      target.callback=[](const auto& d,bool done){return d.relativePath=="second"&&!done
          ? cpnetflux::common::Status::transportError("injected metadata disconnect") : cpnetflux::common::Status::ok();};
      auto sender=std::async(std::launch::async,[&]{auto s=sendPersistentDirectories(&pair.tx,source,0,1,0,nullptr);pair.tx.shutdown();return s;});
      auto result=receivePersistentDirectories(&pair.rx,f.path.string(),target,0,1,nullptr);
      pair.rx.shutdown();EXPECT_FALSE(result.isOk());EXPECT_FALSE(sender.get().isOk());
      EXPECT_TRUE(std::filesystem::is_directory(f.path/"first"));
      EXPECT_FALSE(std::filesystem::exists(f.path/"second")); }
    std::filesystem::remove(f.path/"first");
    Pair pair;PersistentDirectoryTransfer target;
    auto sender=std::async(std::launch::async,[&]{auto s=sendPersistentDirectories(&pair.tx,source,0,1,1,nullptr);pair.tx.shutdown();return s;});
    auto result=receivePersistentDirectories(&pair.rx,f.path.string(),target,0,1,nullptr);
    pair.rx.shutdown();EXPECT_TRUE(result.isOk())<<result.message();EXPECT_TRUE(sender.get().isOk());
    EXPECT_TRUE(std::filesystem::is_directory(f.path/"first"));
    EXPECT_TRUE(std::filesystem::is_directory(f.path/"second"));
}
TEST(V3DirectoryTest, SenderRejectsOverBudgetInventoryAndPathBeforeTransportUse) {
    Pair pair; PersistentDirectoryTransfer transfer;
    transfer.entries.resize(kMaxTreeDirectories+1);
    EXPECT_FALSE(sendPersistentDirectories(&pair.tx,transfer,0,1,0,nullptr).isOk());
    transfer.entries={{std::string(kMaxTreeDirectoryPathBytes+1,'x'),transferId}};
    EXPECT_FALSE(sendPersistentDirectories(&pair.tx,transfer,0,1,0,nullptr).isOk());
}
