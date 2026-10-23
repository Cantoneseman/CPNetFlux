// Dedicated executable: syscall interception never enters product binaries.
#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <thread>
#include <optional>
#include <cerrno>
#include "cpnetflux/checkpoint/manifest_store.h"
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
using namespace cpnetflux::core::io;
using namespace cpnetflux::core::protocol;
namespace fs=std::filesystem;
using cpnetflux::common::Status;
struct Probe {
    std::string directory;
    std::mutex mutex;
    std::condition_variable changed;
    int active=0, maximum=0, arrived=0;
    bool released=false;
    std::optional<off_t> blockOffset, failOffset;
    int receiveFd=-1;
    std::size_t expectedReadBytes=0, readBytes=0;
    bool nextReadStarted=false;
};
struct ProbeScope {
    explicit ProbeScope(Probe& value);
    ~ProbeScope();
};
std::atomic<Probe*> probe{nullptr};
bool beforeWrite(int fd,off_t offset) {
    auto* p=probe.load(); if (!p) return true;
    char link[64],path[4096];
    std::snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);
    const auto n=::readlink(link,path,sizeof(path)-1);
    if (n<0) return true;
    path[n]=0;
    std::unique_lock lock(p->mutex);
    if (std::string_view(path).find(p->directory)!=0) return true;
    if (p->failOffset && offset==*p->failOffset) {errno=EIO;return false;}
    if (p->blockOffset && (offset!=*p->blockOffset || p->arrived))return true;
    if (p->arrived>=2) return true;
    ++p->arrived; ++p->active; p->maximum=std::max(p->maximum,p->active); p->changed.notify_all();
    p->changed.wait_until(lock,std::chrono::system_clock::now()+std::chrono::seconds(2),[&] {return (!p->blockOffset && p->active>=2) || p->released;});
    if (p->active>=2) {p->released=true;p->changed.notify_all();}
    --p->active;
    return true;
}
ProbeScope::ProbeScope(Probe& value){probe=&value;}
ProbeScope::~ProbeScope(){probe=nullptr;}
bool entered(Probe& p) {
    std::unique_lock lock(p.mutex);
    return p.changed.wait_until(lock,std::chrono::system_clock::now()+std::chrono::seconds(1),[&]{return p.arrived!=0;});
}
void target(Probe& p,const std::string& path){std::lock_guard lock(p.mutex);p.directory=path;}
void release(Probe& p){std::lock_guard lock(p.mutex);p.released=true;p.changed.notify_all();}

struct Root {
    fs::path path;
    Root() { char pattern[]="/tmp/cpnetflux-range-writer-XXXXXX"; path=::mkdtemp(pattern); }
    ~Root() {std::error_code ec;fs::remove_all(path,ec);}
};
struct Pair {
    FramedDataSocket sender, receiver;
    Pair() {int fds[2];if (::socketpair(AF_UNIX,SOCK_STREAM,0,fds))throw std::runtime_error("socketpair");
        sender=FramedDataSocket(UniqueFd(fds[0]));receiver=FramedDataSocket(UniqueFd(fds[1]));}
};
struct Harness {
    Root root;
    Pair pairs[4];
    PersistentTreeStats stats[4];
    Status results[4];
    std::shared_ptr<PersistentTransferContext> context;
    std::jthread receivers[4];
    unsigned lanes;
    Harness(const PersistentTransferPolicy& policy={},unsigned count=2):context(makePersistentTransferContext(policy)),lanes(count) {
        for (unsigned i=0;i<lanes;++i) {
            stats[i].phaseTiming=true;
            receivers[i]=std::jthread([&,i] {
            results[i]=receivePersistentTree(&pairs[i].receiver,root.path.string(),false,"",
                &stats[i],{},1,i,lanes,kUnknownPersistentFileCount,true,false,context);
            });
        }
    }
    void stop() {
        for(auto& pair:pairs)::shutdown(pair.sender.fd(),SHUT_RDWR);
        for(auto& receiver:receivers)if(receiver.joinable())receiver.join();
    }
    ~Harness(){stop();}
};
PersistentFileIdentity identity(unsigned index) {
    return PersistentFileIdentity{1,7,131072,"large.bin","range-write",65536,0,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c,{},index+1,index*65536ULL,65536,2,1};
}
class RangeWriterTest:public ::testing::Test {
    std::optional<std::string> saved_;
 protected:
    void SetUp() override {
        if(const auto* value=std::getenv("CPNETFLUX_RANGE_WRITE_MODE"))saved_=value;
        ::setenv("CPNETFLUX_RANGE_WRITE_MODE","parallel",1);
    }
    void TearDown() override {
        if(saved_)::setenv("CPNETFLUX_RANGE_WRITE_MODE",saved_->c_str(),1);
        else ::unsetenv("CPNETFLUX_RANGE_WRITE_MODE");
    }
};
auto begin(Pair& p,const PersistentFileIdentity& id) {
    const auto status=PersistentDataSession::writeBegin(&p.sender,id);
    if(!status.isOk())throw std::runtime_error(status.message());
    return PersistentDataSession::readResumeResponse(&p.sender,id);
}
Status payload(Pair& p,const PersistentFileIdentity& id,std::uint64_t offset,const std::string& data) {
    for(std::size_t pos=0;pos<data.size();pos+=32768) {
        auto status=PersistentDataSession::writeData(&p.sender,id,offset+pos,
            reinterpret_cast<const std::uint8_t*>(data.data()+pos),std::min<std::size_t>(32768,data.size()-pos));
        if(!status.isOk())return status;
    }
    return Status::ok();
}
auto end(Pair& p,const PersistentFileIdentity& id,const std::string& sent) {
    cpnetflux::checksum::ChecksumComputer crc(id.checksumAlgorithm);
    crc.update(reinterpret_cast<const std::uint8_t*>(sent.data()),sent.size());
    const auto status=PersistentDataSession::writeEnd(&p.sender,id,crc.finalize());
    if(!status.isOk())throw std::runtime_error(status.message());
    return PersistentDataSession::readResult(&p.sender,id);
}
bool matches(const fs::path& path,const std::string& expected) {
    std::ifstream in(path,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),{})==expected;
}
TEST_F(RangeWriterTest, NonOverlappingRangesWriteAtSameTime) {
    const auto* prior=std::getenv("CPNETFLUX_RANGE_WRITE_MODE");
    const std::string saved=prior?prior:"";const bool present=prior;
    ::setenv("CPNETFLUX_RANGE_WRITE_MODE","parallel",1);
    struct Restore {std::string value;bool present;~Restore(){if(present)::setenv("CPNETFLUX_RANGE_WRITE_MODE",value.c_str(),1);else ::unsetenv("CPNETFLUX_RANGE_WRITE_MODE");}} restore{saved,present};
    Harness h;
    for(unsigned i=0;i<2;++i) {
        ASSERT_TRUE(PersistentDataSession::writeBegin(&h.pairs[i].sender,identity(i)).isOk());
        ASSERT_TRUE(PersistentDataSession::readResumeResponse(&h.pairs[i].sender,identity(i)).isOk());
    }
    Probe p;target(p,h.root.path.string()+"/");probe=&p;
    std::future<Status> senders[2];
    for(unsigned i=0;i<2;++i) senders[i]=std::async(std::launch::async,[&,i] {
        const auto id=identity(i);
        std::vector<std::uint8_t> data(65536,static_cast<std::uint8_t>('A'+i));
        Status status;
        for (std::size_t pos=0;pos<data.size();pos+=32768) {
            status=PersistentDataSession::writeData(&h.pairs[i].sender,id,id.rangeOffset+pos,data.data()+pos,32768);
            if (!status.isOk())return status;
        }
        cpnetflux::checksum::ChecksumComputer crc(id.checksumAlgorithm);crc.update(data.data(),data.size());
        status=PersistentDataSession::writeEnd(&h.pairs[i].sender,id,crc.finalize());
        if (!status.isOk())return status;
        auto result=PersistentDataSession::readResult(&h.pairs[i].sender,id);
        if(!result.isOk())return result.status();
        if(result.value()!=FrameStatusCode::Ok)return Status::runtimeError("range failed");
        return PersistentDataSession::writeDirectoryEnd(&h.pairs[i].sender,i?1:0);
    });
    for(auto& sender:senders){const auto result=sender.get();EXPECT_TRUE(result.isOk())<<result.message();}
    h.stop();probe=nullptr;
    for(unsigned i=0;i<h.lanes;++i)EXPECT_TRUE(h.results[i].isOk())<<h.results[i].message();
    EXPECT_EQ(h.stats[0].fileIoStats.writeBytes()+h.stats[1].fileIoStats.writeBytes(),131072U);
    EXPECT_GE(p.maximum,2)<<"independent ranges still serialize the real pwrite calls";
    std::ifstream in(h.root.path/"large.bin",std::ios::binary);
    EXPECT_TRUE(std::string(std::istreambuf_iterator<char>(in),{})==std::string(65536,'A')+std::string(65536,'B'));
}

TEST_F(RangeWriterTest, ClaimConflictDoesNotWaitForAnotherSlotsSlowWrite) {
    Probe p;p.blockOffset=0;ProbeScope observed(p);
    Harness h;target(p,h.root.path.string()+"/");
    auto a=identity(0),b=identity(1);b.rangeOffset=32768;
    ASSERT_TRUE(begin(h.pairs[0],a).isOk());
    ASSERT_TRUE(payload(h.pairs[0],a,0,std::string(32768,'A')).isOk());
    ASSERT_TRUE(entered(p));
    auto incoming=std::async(std::launch::async,[&]{return begin(h.pairs[1],b);});
    const auto ready=incoming.wait_for(std::chrono::milliseconds(100));
    EXPECT_EQ(ready,std::future_status::ready)<<"state claim waits for unrelated data I/O";
    release(p);
    auto response=incoming.get();ASSERT_TRUE(response.isOk());
    EXPECT_NE(response.value().statusCode,FrameStatusCode::Ok);
}
TEST_F(RangeWriterTest, ReplacementDrainsOldWriteAndRejectsLateData) {
    Probe p;p.blockOffset=0;ProbeScope observed(p);
    Harness h({},3);target(p,h.root.path.string()+"/");
    auto a=identity(0);a.chunkSize=32768;a.rangeLength=65536;
    auto newer=a;newer.attempt=2;
    auto b=a;b.rangeId=2;b.rangeOffset=65536;
    ASSERT_TRUE(begin(h.pairs[0],a).isOk());ASSERT_TRUE(begin(h.pairs[2],b).isOk());
    const std::string first(32768,'A'),second(32768,'B'),late(32768,'X'),last(65536,'C');
    ASSERT_TRUE(payload(h.pairs[0],a,0,first).isOk());
    ASSERT_TRUE(entered(p));
    auto replacement=std::async(std::launch::async,[&]{return begin(h.pairs[1],newer);});
    EXPECT_EQ(replacement.wait_for(std::chrono::milliseconds(50)),std::future_status::timeout);
    release(p);
    auto resumed=replacement.get();ASSERT_TRUE(resumed.isOk());
    // The old DATA was fully written/checkpointed before the new attempt got a mask.
    ASSERT_EQ(resumed.value().missingRanges.size(),1U);
    EXPECT_EQ(resumed.value().missingRanges.front().begin,32768U);
    ASSERT_TRUE(payload(h.pairs[0],a,32768,late).isOk());
    auto oldEnd=end(h.pairs[0],a,first+late);ASSERT_TRUE(oldEnd.isOk());
    EXPECT_NE(oldEnd.value(),FrameStatusCode::Ok);
    ASSERT_TRUE(payload(h.pairs[1],newer,32768,second).isOk());
    auto newEnd=end(h.pairs[1],newer,second);ASSERT_TRUE(newEnd.isOk());EXPECT_EQ(newEnd.value(),FrameStatusCode::Ok);
    ASSERT_TRUE(payload(h.pairs[2],b,b.rangeOffset,last).isOk());
    auto otherEnd=end(h.pairs[2],b,last);ASSERT_TRUE(otherEnd.isOk());EXPECT_EQ(otherEnd.value(),FrameStatusCode::Ok);
    EXPECT_TRUE(matches(h.root.path/"large.bin",first+second+last));
}
TEST_F(RangeWriterTest, ChangedGeometryCannotTakeOverAnExistingRange) {
    Harness h;auto a=identity(0),changed=a;changed.attempt=2;changed.rangeOffset=32768;
    ASSERT_TRUE(begin(h.pairs[0],a).isOk());
    auto result=begin(h.pairs[1],changed);ASSERT_TRUE(result.isOk());
    EXPECT_NE(result.value().statusCode,FrameStatusCode::Ok);
}
TEST_F(RangeWriterTest, PublishWaitsForAllWritingRanges) {
    Probe p;p.blockOffset=0;ProbeScope observed(p);
    Harness h;target(p,h.root.path.string()+"/");
    auto a=identity(0),b=identity(1);
    ASSERT_TRUE(begin(h.pairs[0],a).isOk());ASSERT_TRUE(begin(h.pairs[1],b).isOk());
    const std::string first(65536,'A'),second(65536,'B');
    ASSERT_TRUE(payload(h.pairs[0],a,0,first).isOk());ASSERT_TRUE(entered(p));
    auto other=std::async(std::launch::async,[&]{
        auto status=payload(h.pairs[1],b,b.rangeOffset,second);
        if(!status.isOk())throw std::runtime_error(status.message());
        return end(h.pairs[1],b,second);
    });
    const auto ready=other.wait_for(std::chrono::milliseconds(100));
    EXPECT_EQ(ready,std::future_status::ready);
    EXPECT_FALSE(fs::exists(h.root.path/"large.bin"));
    release(p);auto result=other.get();ASSERT_TRUE(result.isOk());EXPECT_EQ(result.value(),FrameStatusCode::Ok);
    auto final=end(h.pairs[0],a,first);ASSERT_TRUE(final.isOk());EXPECT_EQ(final.value(),FrameStatusCode::Ok);
    EXPECT_TRUE(matches(h.root.path/"large.bin",first+second));
}
TEST_F(RangeWriterTest, WriteFailureNeverPublishesOrCheckpointsTheFailedChunk) {
    Probe p;p.failOffset=0;p.released=true;ProbeScope observed(p);
    Harness h;target(p,h.root.path.string()+"/");
    auto a=identity(0),b=identity(1);
    ASSERT_TRUE(begin(h.pairs[0],a).isOk());ASSERT_TRUE(begin(h.pairs[1],b).isOk());
    const std::string first(65536,'A'),second(65536,'B');
    ASSERT_TRUE(payload(h.pairs[0],a,0,first).isOk());
    auto result=end(h.pairs[0],a,first);ASSERT_TRUE(result.isOk());EXPECT_NE(result.value(),FrameStatusCode::Ok);
    ASSERT_TRUE(payload(h.pairs[1],b,b.rangeOffset,second).isOk());
    auto other=end(h.pairs[1],b,second);ASSERT_TRUE(other.isOk());EXPECT_NE(other.value(),FrameStatusCode::Ok);
    h.stop();
    EXPECT_FALSE(fs::exists(h.root.path/"large.bin"));
    auto checkpoint=cpnetflux::checkpoint::ManifestStore::load(
        cpnetflux::checkpoint::manifestPathForOutput((h.root.path/"large.bin").string()));
    ASSERT_TRUE(checkpoint.isOk());EXPECT_TRUE(checkpoint.value().verifiedChunks.empty());
}
TEST_F(RangeWriterTest, BufferedWritersKeepInterleavedRangesIndependent) {
    PersistentTransferPolicy policy;
    policy.options.fileIo.bufferSize=65536;
    policy.options.fileIo.posixWriteStrategy=cpnetflux::storage::PosixWriteStrategy::Coalesced;
    Harness h(policy);
    auto a=identity(0),b=identity(1);
    ASSERT_TRUE(begin(h.pairs[0],a).isOk());ASSERT_TRUE(begin(h.pairs[1],b).isOk());
    for(unsigned piece=0;piece<2;++piece) {
        ASSERT_TRUE(payload(h.pairs[0],a,piece*32768,std::string(32768,'A')).isOk());
        ASSERT_TRUE(payload(h.pairs[1],b,b.rangeOffset+piece*32768,std::string(32768,'B')).isOk());
    }
    auto first=end(h.pairs[0],a,std::string(65536,'A'));ASSERT_TRUE(first.isOk());EXPECT_EQ(first.value(),FrameStatusCode::Ok);
    auto second=end(h.pairs[1],b,std::string(65536,'B'));ASSERT_TRUE(second.isOk());EXPECT_EQ(second.value(),FrameStatusCode::Ok);
    EXPECT_TRUE(matches(h.root.path/"large.bin",std::string(65536,'A')+std::string(65536,'B')));
    EXPECT_EQ(h.stats[0].fileIoStats.posixWriteSyscallCount()+h.stats[1].fileIoStats.posixWriteSyscallCount(),2U);
}
TEST_F(RangeWriterTest, ReplacementDiscardsTheUnverifiedBufferedTail) {
    PersistentTransferPolicy policy;
    policy.options.fileIo.bufferSize=65536;
    policy.options.fileIo.posixWriteStrategy=cpnetflux::storage::PosixWriteStrategy::Coalesced;
    Probe p;p.released=true;ProbeScope observed(p);
    Harness h(policy,3);target(p,h.root.path.string()+"/");
    auto a=identity(0),newer=a,b=identity(1);newer.attempt=2;
    ASSERT_TRUE(begin(h.pairs[0],a).isOk());ASSERT_TRUE(begin(h.pairs[2],b).isOk());
    {
        std::lock_guard lock(p.mutex);
        p.receiveFd=h.pairs[0].receiver.fd();
        p.expectedReadBytes=kFrameHeaderSize+32768+4;
    }
    ASSERT_TRUE(payload(h.pairs[0],a,0,std::string(32768,'A')).isOk());
    {
        std::unique_lock lock(p.mutex);
        ASSERT_TRUE(p.changed.wait_until(lock,std::chrono::system_clock::now()+std::chrono::seconds(1),
            [&]{return p.nextReadStarted;}));
    }
    // The receiver asked for the next frame: the old 32 KiB tail is actually buffered.
    auto response=begin(h.pairs[1],newer);ASSERT_TRUE(response.isOk());
    ASSERT_EQ(response.value().missingRanges.size(),1U);
    EXPECT_EQ(response.value().missingRanges.front().begin,0U);
    ASSERT_TRUE(payload(h.pairs[0],a,32768,std::string(32768,'X')).isOk());
    auto old=end(h.pairs[0],a,std::string(32768,'A')+std::string(32768,'X'));
    ASSERT_TRUE(old.isOk());EXPECT_NE(old.value(),FrameStatusCode::Ok);
    ASSERT_TRUE(payload(h.pairs[1],newer,0,std::string(65536,'B')).isOk());
    auto replacement=end(h.pairs[1],newer,std::string(65536,'B'));ASSERT_TRUE(replacement.isOk());
    EXPECT_EQ(replacement.value(),FrameStatusCode::Ok);
    ASSERT_TRUE(payload(h.pairs[2],b,b.rangeOffset,std::string(65536,'C')).isOk());
    auto other=end(h.pairs[2],b,std::string(65536,'C'));ASSERT_TRUE(other.isOk());
    EXPECT_EQ(other.value(),FrameStatusCode::Ok);
    EXPECT_TRUE(matches(h.root.path/"large.bin",std::string(65536,'B')+std::string(65536,'C')));
}
TEST_F(RangeWriterTest, InvalidModeFailsBeforeCreatingAnyCheckpoint) {
    ::setenv("CPNETFLUX_RANGE_WRITE_MODE","invalid",1);
    Harness h;auto result=begin(h.pairs[0],identity(0));ASSERT_TRUE(result.isOk());
    EXPECT_NE(result.value().statusCode,FrameStatusCode::Ok);
    EXPECT_FALSE(fs::exists(cpnetflux::checkpoint::manifestPathForOutput((h.root.path/"large.bin").string())));
}
} // namespace
extern "C" ssize_t pwrite(int fd,const void* data,size_t length,off_t offset) {
    if(!beforeWrite(fd,offset))return -1;
    return static_cast<ssize_t>(::syscall(SYS_pwrite64,fd,data,length,offset));
}

// Observe the next frame read to distinguish a buffered tail from queued DATA.
extern "C" ssize_t recv(int fd,void* data,size_t length,int flags) {
    if(auto* p=probe.load()) {
        std::lock_guard lock(p->mutex);
        if(fd==p->receiveFd && p->readBytes>=p->expectedReadBytes) {
            p->nextReadStarted=true;p->changed.notify_all();
        }
    }
    const auto n=static_cast<ssize_t>(::syscall(SYS_recvfrom,fd,data,length,flags,nullptr,nullptr));
    if(auto* p=probe.load()) {
        std::lock_guard lock(p->mutex);
        if(n>0 && fd==p->receiveFd)p->readBytes+=static_cast<std::size_t>(n);
    }
    return n;
}
