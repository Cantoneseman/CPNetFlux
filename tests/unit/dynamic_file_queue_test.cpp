#include "cpnetflux/core/io/dynamic_file_queue.h"
#include "cpnetflux/core/io/persistent_directory_batch.h"
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
using namespace cpnetflux::core::io;
using cpnetflux::common::Status;
namespace {
std::vector<PersistentFileIdentity> tasks(std::uint32_t n) {
    std::vector<PersistentFileIdentity> out;
    for (std::uint32_t i=1; i<=n; ++i) out.push_back({i,i,i,"f"+std::to_string(i),"t"+std::to_string(i),64,12});
    return out;
}
}
TEST(DynamicFileQueueTest, RingWrapsAndIdleChannelClaimsBeyondOldShard) {
    DynamicFileQueue q(tasks(20),2,1,3);
    for (std::uint32_t i=20; i>0; --i) {
        auto next=q.claim(0); ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
        EXPECT_EQ(next.value()->file.fileId,i);
        EXPECT_GE(next.value()->queueWaitSeconds,0);
        EXPECT_TRUE(q.complete(0,next.value()->file).isOk());
    }
    EXPECT_FALSE(q.claim(1).value());
    EXPECT_EQ(q.highWatermark(),3U);
    EXPECT_EQ(q.outstanding(),0U);
}
TEST(DynamicFileQueueTest, CompletionChecksAllIdentityFieldsAndChannelCredit) {
    DynamicFileQueue q(tasks(3),2,1,1);
    auto next=q.claim(0); ASSERT_TRUE(next.isOk()); auto id=next.value()->file;
    EXPECT_FALSE(q.claim(0).isOk());
    EXPECT_FALSE(q.complete(1,id).isOk());
    for (int field=0;field<7;++field) {
        auto stale=id;
        if(field==0)++stale.generation;
        if(field==1)++stale.totalSize;
        if(field==2)stale.relativePath="other";
        if(field==3)stale.transferId="other";
        if(field==4)++stale.chunkSize;
        if(field==5)++stale.mtimeUnixSeconds;
        if(field==6)++stale.fileId;
        EXPECT_FALSE(q.complete(0,stale).isOk());
    }
    EXPECT_TRUE(q.complete(0,id).isOk());
    EXPECT_FALSE(q.complete(0,id).isOk());
    EXPECT_TRUE(q.claim(0).isOk());
}
TEST(DynamicFileQueueTest, ConcurrentClaimsAreUniqueAndCancellationStopsAllClaims) {
    DynamicFileQueue q(tasks(1000),8,2,16);
    std::atomic<unsigned> count{0};std::vector<std::thread> workers;
    for(unsigned c=0;c<8;++c)workers.emplace_back([&,c] {
        for(;;) {
            auto next=q.claim(c);
            if(!next.isOk() || !next.value())break;
            EXPECT_TRUE(q.complete(c,next.value()->file).isOk());++count;
        }
    });
    for(auto& t:workers)t.join();
    EXPECT_EQ(count,1000U); EXPECT_LE(q.highWatermark(),16U);
    q.cancel(Status::runtimeError("cancel"));
    EXPECT_FALSE(q.claim(0).isOk());EXPECT_EQ(q.outstanding(),0U);
}
TEST(DynamicFileQueueTest, RejectsInvalidBoundsAndDuplicateSourceMetadata) {
    auto duplicate=tasks(2);duplicate[1]=duplicate[0];
    DynamicFileQueue q(duplicate,2,2,2); EXPECT_FALSE(q.claim(0).isOk());
    DynamicFileQueue zero(tasks(1),1,1,0); EXPECT_FALSE(zero.claim(0).isOk());
    DynamicFileQueue large(tasks(1),1,1,257); EXPECT_FALSE(large.claim(0).isOk());
}
TEST(PersistentDirectoryBatchTest, RejectsDuplicatePathsForeignAndStaleCompletions) {
    PersistentDirectoryBatch batch(2,2);
    ASSERT_TRUE(batch.attach(0).isOk());ASSERT_TRUE(batch.attach(1).isOk());
    batch.bind(0,-1,-1);batch.bind(1,-1,-1);
    auto files=tasks(2);
    ASSERT_TRUE(batch.fileEvent(0,files[0],Status::ok(),false).isOk());
    auto samePath=files[1];samePath.relativePath=files[0].relativePath;
    EXPECT_FALSE(batch.fileEvent(1,samePath,Status::ok(),false).isOk());
    EXPECT_FALSE(batch.fileEvent(1,files[0],Status::ok(),true).isOk());
    auto stale=files[0];++stale.generation;
    EXPECT_FALSE(batch.fileEvent(0,stale,Status::ok(),true).isOk());
    ASSERT_TRUE(batch.fileEvent(0,files[0],Status::ok(),true).isOk());
    EXPECT_FALSE(batch.fileEvent(0,files[0],Status::ok(),true).isOk());
}
TEST(PersistentDirectoryBatchTest, MissingParticipantTimesOutAndReleasesSockets) {
    int fds[2]; ASSERT_EQ(::socketpair(AF_UNIX,SOCK_STREAM,0,fds),0);
    PersistentDirectoryBatch batch(0,2);ASSERT_TRUE(batch.attach(0).isOk());batch.bind(0,fds[0],-1);
    EXPECT_FALSE(batch.waitReady(std::chrono::milliseconds(10)).isOk());
    char byte; EXPECT_EQ(::recv(fds[1],&byte,1,0),0);
    batch.unbind(0);::close(fds[0]);::close(fds[1]);
}
TEST(PersistentDirectoryBatchTest, WholeBatchCompletionRequiresEveryFileAndChannel) {
    PersistentDirectoryBatch batch(1,2);
    for(unsigned c=0;c<2;++c){ASSERT_TRUE(batch.attach(c).isOk());batch.bind(c,-1,-1);}
    auto file=tasks(1)[0];
    ASSERT_TRUE(batch.fileEvent(1,file,Status::ok(),false).isOk());
    ASSERT_TRUE(batch.fileEvent(1,file,Status::ok(),true).isOk());
    Status left;
    std::thread t([&]{left=batch.finish(0,std::chrono::seconds(1));});
    EXPECT_TRUE(batch.finish(1,std::chrono::seconds(1)).isOk());t.join();EXPECT_TRUE(left.isOk());
    PersistentDirectoryBatch missing(1,1);ASSERT_TRUE(missing.attach(0).isOk());missing.bind(0,-1,-1);
    EXPECT_FALSE(missing.finish(0,std::chrono::milliseconds(1)).isOk());
}

TEST(PersistentDirectoryBatchTest, ActiveControlDisconnectReclaimsIdleDataSocket) {
    int control[2], data[2];
    ASSERT_EQ(::socketpair(AF_UNIX,SOCK_STREAM,0,control),0);
    ASSERT_EQ(::socketpair(AF_UNIX,SOCK_STREAM,0,data),0);
    PersistentDirectoryBatch batch(1,1);
    ASSERT_TRUE(batch.attach(0).isOk());
    batch.bind(0,control[0],data[0]);
    ASSERT_TRUE(batch.waitReady(std::chrono::seconds(1)).isOk());
    ::close(control[1]);
    pollfd descriptor{data[1],POLLIN,0};
    EXPECT_GT(::poll(&descriptor,1,1000),0) << "control disconnect must interrupt data without waiting for payload timeout";
    batch.unbind(0);
    ::close(control[0]);::close(data[0]);::close(data[1]);
}
