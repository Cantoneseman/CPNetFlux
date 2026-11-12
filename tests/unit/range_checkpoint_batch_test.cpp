#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/checkpoint/download_manifest.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

namespace {
using namespace cpnetflux::core::io;
using namespace cpnetflux::core::protocol;
namespace fs = std::filesystem;
using cpnetflux::common::Status;
using cpnetflux::checksum::ChecksumAlgorithm;
struct Root {
    fs::path path;
    Root() { char name[]="/tmp/cpnetflux-checkpoint-test-XXXXXX"; path=::mkdtemp(name); }
    ~Root() { std::error_code ec; fs::remove_all(path,ec); }
};
struct Pair {
    FramedDataSocket send, receive;
    Pair() { int fds[2]; if (::socketpair(AF_UNIX,SOCK_STREAM,0,fds)) throw std::runtime_error("socketpair");
        send=FramedDataSocket(UniqueFd(fds[0])); receive=FramedDataSocket(UniqueFd(fds[1])); }
};
std::string manifestPath(const fs::path& root, bool download) {
    const auto output=(root/"large.bin").string();
    return download ? cpnetflux::checkpoint::downloadManifestPathForOutput(output)
                    : cpnetflux::checkpoint::manifestPathForOutput(output);
}
std::pair<std::size_t,std::string> snapshot(const fs::path& root, bool download) {
    if (download) {
        auto m=cpnetflux::checkpoint::loadDownloadManifest(manifestPath(root,true));
        if (!m.isOk()) return {SIZE_MAX,{}};
        return {m.value().verifiedChunks.size(),m.value().tempPath};
    }
    auto m=cpnetflux::checkpoint::ManifestStore::load(manifestPath(root,false));
    if (!m.isOk()) return {SIZE_MAX,{}};
    return {m.value().verifiedChunks.size(),m.value().tempPath};
}
template<class F> bool until(F predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (std::chrono::steady_clock::now()<deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
PersistentTransferPolicy policy(unsigned batch) {
    PersistentTransferPolicy p;
    p.rangeCheckpointBatchChunks=batch;
    // Session would otherwise autosave every chunk: range batching owns its schedule.
    p.options.manifestFlushIntervalChunks=1;
    return p;
}
struct Receiver {
    Root root;
    Pair pair;
    PersistentFileIdentity id{1,7,256,"large.bin","checkpoint-batch",4,0,
        ChecksumAlgorithm::Crc32c,{},1,0,128,2,1};
    PersistentTreeStats stats;
    Status result;
    bool download;
    std::shared_ptr<PersistentTransferContext> context;
    std::jthread worker;
    Receiver(unsigned batch, bool downloading=false) : download(downloading),
        context(makePersistentTransferContext(policy(batch))),
        worker([this] { result=receivePersistentTree(&pair.receive,root.path.string(),download,"remote",
            &stats,{},1,0,1,kUnknownPersistentFileCount,true,false,context); }) {}
    bool begin() {
        return PersistentDataSession::writeBegin(&pair.send,id).isOk() &&
            PersistentDataSession::readResumeResponse(&pair.send,id).isOk();
    }
    bool chunk(unsigned n) {
        const std::uint8_t data[4]={'A','B','C','D'};
        return PersistentDataSession::writeData(&pair.send,id,n*4,data,4).isOk();
    }
    bool dataWritten(unsigned chunks) {
        const auto name=snapshot(root.path,download).second;
        if (name.empty()) return false;
        std::ifstream in(name,std::ios::binary);
        in.seekg(chunks*4-1);
        return in.get()=='D';
    }
    void stop() { ::shutdown(pair.send.fd(),SHUT_RDWR); if (worker.joinable()) worker.join(); }
    ~Receiver() { stop(); }
};

TEST(RangeCheckpointBatchTest, ThresholdAndIdleTimerCoverUploadAndDownload) {
    for (const bool download : {false,true}) {
        Receiver r(4,download); ASSERT_TRUE(r.begin());
        ASSERT_TRUE(r.chunk(0));
        ASSERT_TRUE(until([&] { return r.dataWritten(1); }));
        EXPECT_EQ(snapshot(r.root.path,download).first,0U);
        for (unsigned i=1;i<4;++i) ASSERT_TRUE(r.chunk(i));
        ASSERT_TRUE(until([&] { return snapshot(r.root.path,download).first==4; }));
        ASSERT_TRUE(r.chunk(4));
        ASSERT_TRUE(until([&] { return r.dataWritten(5); }));
        EXPECT_EQ(snapshot(r.root.path,download).first,4U);
        // No further DATA/FILE_END: a timer, not the next packet, must save.
        ASSERT_TRUE(until([&] { return snapshot(r.root.path,download).first==5; }));
        r.stop();
        EXPECT_FALSE(r.result.isOk());
    }
}
TEST(RangeCheckpointBatchTest, DisconnectFlushesEvenWhileContextRemainsAlive) {
    for (const bool download : {false,true}) {
        Receiver r(16,download); ASSERT_TRUE(r.begin()); ASSERT_TRUE(r.chunk(0));
        ASSERT_TRUE(until([&] { return r.dataWritten(1); }));
        r.stop();
        EXPECT_FALSE(r.result.isOk());
        EXPECT_EQ(snapshot(r.root.path,download).first,1U);
        // Context/transaction still alive; destructor cannot rescue the checkpoint.
        ASSERT_TRUE(r.context);
    }
}
TEST(RangeCheckpointBatchTest, RangeResultForcesCheckpointBeforeSuccess) {
    for (const bool download : {false,true}) {
        Receiver r(16,download);
        r.id.rangeLength=4;
        ASSERT_TRUE(r.begin()); ASSERT_TRUE(r.chunk(0));
        cpnetflux::checksum::ChecksumComputer crc(r.id.checksumAlgorithm);
        crc.update(reinterpret_cast<const std::uint8_t*>("ABCD"),4);
        ASSERT_TRUE(PersistentDataSession::writeEnd(&r.pair.send,r.id,crc.finalize()).isOk());
        auto result=PersistentDataSession::readResult(&r.pair.send,r.id);
        ASSERT_TRUE(result.isOk()); EXPECT_EQ(result.value(),FrameStatusCode::Ok);
        EXPECT_EQ(snapshot(r.root.path,download).first,1U);
        EXPECT_FALSE(fs::exists(r.root.path/"large.bin")); // other range still pending
    }
}
TEST(RangeCheckpointBatchTest, TimerSaveFailureStaysFailedAfterDiskPathIsRepaired) {
    for (const bool download : {false,true}) {
        Receiver r(16,download); r.id.rangeLength=4;
        ASSERT_TRUE(r.begin());
        const auto path=manifestPath(r.root.path,download);
        fs::remove(path); fs::create_directory(path);
        std::ofstream(fs::path(path)/"block")<<"prevents rename";
        ASSERT_TRUE(r.chunk(0));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        // Repairing the path cannot erase an already observed background failure.
        fs::remove(fs::path(path)/"block"); fs::remove(path);
        cpnetflux::checksum::ChecksumComputer crc(r.id.checksumAlgorithm); crc.update(reinterpret_cast<const std::uint8_t*>("ABCD"),4);
        ASSERT_TRUE(PersistentDataSession::writeEnd(&r.pair.send,r.id,crc.finalize()).isOk());
        auto result=PersistentDataSession::readResult(&r.pair.send,r.id);
        ASSERT_TRUE(result.isOk()); EXPECT_NE(result.value(),FrameStatusCode::Ok);
        r.stop(); EXPECT_FALSE(r.result.isOk());
        EXPECT_FALSE(fs::exists(r.root.path/"large.bin"));
    }
}
TEST(RangeCheckpointBatchTest, ForcedFlushFailureCannotAcknowledgeRange) {
    Receiver r(16); r.id.rangeLength=4; ASSERT_TRUE(r.begin());
    const auto path=manifestPath(r.root.path,false);
    fs::remove(path); fs::create_directory(path); std::ofstream(fs::path(path)/"block")<<"x";
    ASSERT_TRUE(r.chunk(0));
    cpnetflux::checksum::ChecksumComputer crc(r.id.checksumAlgorithm); crc.update(reinterpret_cast<const std::uint8_t*>("ABCD"),4);
    ASSERT_TRUE(PersistentDataSession::writeEnd(&r.pair.send,r.id,crc.finalize()).isOk());
    auto result=PersistentDataSession::readResult(&r.pair.send,r.id);
    ASSERT_TRUE(result.isOk()); EXPECT_NE(result.value(),FrameStatusCode::Ok);
    EXPECT_FALSE(fs::exists(r.root.path/"large.bin"));
}
TEST(RangeCheckpointBatchTest, RejectsInvalidBatchBeforeCreatingOutput) {
    Receiver r(32);
    ASSERT_TRUE(PersistentDataSession::writeBegin(&r.pair.send,r.id).isOk());
    auto result=PersistentDataSession::readResumeResponse(&r.pair.send,r.id);
    ASSERT_TRUE(result.isOk()); EXPECT_NE(result.value().statusCode,FrameStatusCode::Ok);
    EXPECT_FALSE(fs::exists(manifestPath(r.root.path,false)));
}
TEST(RangeCheckpointBatchTest, SigkillReplaysOnlyDiskVerifiedCoverageBothDirections) {
    for (const bool download : {false,true}) for (const unsigned batch : {4,16}) {
        SCOPED_TRACE(std::to_string(download)+"/"+std::to_string(batch));
        Root source, destination;
        const std::string content(256,'Q');
        std::ofstream(source.path/"large.bin",std::ios::binary)<<content;
        struct stat st{}; ASSERT_EQ(::stat((source.path/"large.bin").c_str(),&st),0);
        PersistentFileIdentity a{1,7,256,"large.bin","crash-checkpoint",4,st.st_mtim.tv_sec,
            ChecksumAlgorithm::Crc32c,{},1,0,128,2,1};
        auto b=a; b.rangeId=2; b.rangeOffset=128;
        Pair pair;
        const auto pid=::fork(); ASSERT_GE(pid,0);
        if (!pid) {
            // Close the sender's descriptor in the real child receiver process.
            ::close(pair.send.fd());
            PersistentTreeStats stats;
            const auto result=receivePersistentTree(&pair.receive,destination.path.string(),download,"remote",
                &stats,{},1,0,1,kUnknownPersistentFileCount,true,false,makePersistentTransferContext(policy(batch)));
            ::_exit(result.isOk()?0:2);
        }
        struct Child { pid_t pid; ~Child() { if (pid>0) {::kill(pid,SIGKILL); ::waitpid(pid,nullptr,0);} } } child{pid};
        ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.send,a).isOk());
        ASSERT_TRUE(PersistentDataSession::readResumeResponse(&pair.send,a).isOk());
        const unsigned sent=batch+3;
        for (unsigned i=0;i<sent;++i)
            ASSERT_TRUE(PersistentDataSession::writeData(&pair.send,a,i*4,
                reinterpret_cast<const std::uint8_t*>(content.data()+i*4),4).isOk());
        ASSERT_TRUE(until([&] {
            auto m=snapshot(destination.path,download);
            if (m.second.empty()) return false;
            std::ifstream in(m.second,std::ios::binary); in.seekg(sent*4-1);
            return in.get()=='Q';
        }));
        ASSERT_EQ(::kill(pid,SIGKILL),0);
        int state=0; ASSERT_EQ(::waitpid(pid,&state,0),pid); child.pid=0;
        ASSERT_TRUE(WIFSIGNALED(state)); ASSERT_EQ(WTERMSIG(state),SIGKILL);
        const auto persisted=snapshot(destination.path,download).first;
        ASSERT_NE(persisted,SIZE_MAX);
        EXPECT_EQ(persisted,batch); // three flushed data chunks had no saved records
        Pair resumePair; PersistentTreeStats received, sentStats; Status receivedStatus;
        std::jthread receiver([&] {receivedStatus=receivePersistentTree(&resumePair.receive,
            destination.path.string(),download,"remote",&received,{},1,0,1,1,true,true,
            makePersistentTransferContext(policy(batch)));});
        const auto sentStatus=sendPersistentTree(&resumePair.send,source.path.string(),{a,b},&sentStats);
        ::shutdown(resumePair.send.fd(),SHUT_RDWR); receiver.join();
        ASSERT_TRUE(sentStatus.isOk())<<sentStatus.message();
        ASSERT_TRUE(receivedStatus.isOk())<<receivedStatus.message();
        EXPECT_EQ(sentStats.payloadBytes,content.size()-persisted*4);
        std::ifstream in(destination.path/"large.bin",std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in),{}),content);
    }
}
} // namespace
