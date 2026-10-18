#include "cpnetflux/core/io/dynamic_file_queue.h"
#include "cpnetflux/core/io/work_executor.h"
#include "cpnetflux/core/io/persistent_directory_batch.h"
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <netinet/in.h>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include "cpnetflux/core/scheduler/work_item.h"
#include "cpnetflux/core/scheduler/work_item_planner.h"
#include "cpnetflux/core/scheduler/scheduler.h"
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
        ASSERT_TRUE(q.markInFlight(0,next.value()->file).isOk());
        ASSERT_TRUE(q.markCommitPending(0,next.value()->file).isOk());
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
    EXPECT_FALSE(q.complete(0,id).isOk());
    EXPECT_FALSE(q.markInFlight(1,id).isOk());
    for (int field=0;field<15;++field) {
        auto stale=id;
        if(field==0)++stale.generation;
        if(field==1)++stale.totalSize;
        if(field==2)stale.relativePath="other";
        if(field==3)stale.transferId="other";
        if(field==4)++stale.chunkSize;
        if(field==5)++stale.mtimeUnixSeconds;
        if(field==6)++stale.fileId;
        if(field==7)stale.checksumAlgorithm=cpnetflux::checksum::ChecksumAlgorithm::Crc32c;
        if(field==8)stale.checksum.algorithm=cpnetflux::checksum::ChecksumAlgorithm::Crc32c;
        if(field==9)++stale.checksum.value;
        if(field==10)++stale.rangeId;
        if(field==11)++stale.rangeOffset;
        if(field==12)++stale.rangeLength;
        if(field==13)++stale.rangeCount;
        if(field==14)++stale.attempt;
        EXPECT_FALSE(q.markInFlight(0,stale).isOk()) << field;
    }
    ASSERT_TRUE(q.markInFlight(0,id).isOk());
    EXPECT_FALSE(q.markInFlight(0,id).isOk());
    EXPECT_FALSE(q.complete(0,id).isOk());
    EXPECT_FALSE(q.markCommitPending(1,id).isOk());
    auto stale=id; ++stale.attempt;
    EXPECT_FALSE(q.markCommitPending(0,stale).isOk());
    ASSERT_TRUE(q.markCommitPending(0,id).isOk());
    EXPECT_FALSE(q.markCommitPending(0,id).isOk());
    EXPECT_TRUE(q.complete(0,id).isOk());
    EXPECT_FALSE(q.complete(0,id).isOk());
    EXPECT_TRUE(q.claim(0).isOk());
}
TEST(DynamicFileQueueTest, SharedDispatchWindowBoundsOutstandingLeasesAndReleasesOnResult) {
    DynamicFileQueue q(tasks(4),1,2,4);
    auto first=q.claim(0); ASSERT_TRUE(first.isOk()); ASSERT_TRUE(first.value());
    auto second=q.claim(0); ASSERT_TRUE(second.isOk()); ASSERT_TRUE(second.value());
    EXPECT_FALSE(q.claim(0).isOk());
    ASSERT_TRUE(q.markInFlight(0,first.value()->file).isOk());
    ASSERT_TRUE(q.markCommitPending(0,first.value()->file).isOk());
    EXPECT_TRUE(q.complete(0,first.value()->file).isOk());
    auto third=q.claim(0); ASSERT_TRUE(third.isOk()); ASSERT_TRUE(third.value());
    EXPECT_TRUE(q.markInFlight(0,second.value()->file).isOk());
    EXPECT_TRUE(q.markCommitPending(0,second.value()->file).isOk());
    EXPECT_TRUE(q.complete(0,second.value()->file).isOk());
    EXPECT_TRUE(q.markInFlight(0,third.value()->file).isOk());
    EXPECT_TRUE(q.markCommitPending(0,third.value()->file).isOk());
    EXPECT_TRUE(q.complete(0,third.value()->file).isOk());
    EXPECT_EQ(q.outstanding(),0U);
}

TEST(DynamicFileQueueTest, CancellationInvalidatesOutstandingLease) {
    DynamicFileQueue q(tasks(1),1,1,1);
    auto next=q.claim(0); ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
    const auto id=next.value()->file;
    ASSERT_TRUE(q.markInFlight(0,id).isOk());
    q.cancel(Status::runtimeError("cancel"));
    EXPECT_FALSE(q.markCommitPending(0,id).isOk());
    EXPECT_FALSE(q.complete(0,id).isOk());
    EXPECT_EQ(q.outstanding(),0U);
}
TEST(DynamicFileQueueTest, ConcurrentClaimsAreUniqueAndCancellationStopsAllClaims) {
    DynamicFileQueue q(tasks(1000),8,2,16);
    std::atomic<unsigned> count{0};std::atomic<unsigned> failures{0};std::vector<std::thread> workers;
    for(unsigned c=0;c<8;++c)workers.emplace_back([&,c] {
        for(;;) {
            auto next=q.claim(c);
            if(!next.isOk() || !next.value())break;
            if(!q.markInFlight(c,next.value()->file).isOk() ||
                !q.markCommitPending(c,next.value()->file).isOk() ||
                !q.complete(c,next.value()->file).isOk()) ++failures;
            ++count;
        }
    });
    for(auto& t:workers)t.join();
    EXPECT_EQ(count,1000U); EXPECT_EQ(failures,0U); EXPECT_LE(q.highWatermark(),16U);
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

TEST(DynamicFileQueueTest, RangesMustShareBaseIdentityAndPartitionOneFile) {
    auto first = tasks(1)[0];
    first.totalSize = 8; first.rangeId = 1; first.rangeLength = 4; first.rangeCount = 2;
    auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    for (int mismatch = 0; mismatch < 6; ++mismatch) {
        auto wrong = second;
        if (mismatch == 0) wrong.relativePath = "other";
        if (mismatch == 1) ++wrong.generation;
        if (mismatch == 2) ++wrong.chunkSize;
        if (mismatch == 3) ++wrong.mtimeUnixSeconds;
        if (mismatch == 4) wrong.rangeOffset = 2;
        if (mismatch == 5) wrong.fileId = 2;
        DynamicFileQueue q({first,wrong},2,1,2);
        EXPECT_FALSE(q.claim(0).isOk()) << mismatch;
    }
    DynamicFileQueue valid({first,second},2,1,2);
    EXPECT_TRUE(valid.claim(0).isOk());
    DynamicFileQueue gap({first},2,1,2);
    EXPECT_FALSE(gap.claim(0).isOk());
}

TEST(PersistentDirectoryBatchTest, OneFileRangesHaveIndependentChannelLeases) {
    PersistentDirectoryBatch batch(1,2);
    for (unsigned c=0;c<2;++c) { ASSERT_TRUE(batch.attach(c).isOk()); batch.bind(c,-1,-1); }
    auto a=tasks(1)[0]; a.totalSize=8; a.rangeId=1; a.rangeLength=4; a.rangeCount=2;
    auto b=a; b.rangeId=2; b.rangeOffset=4;
    ASSERT_TRUE(batch.fileEvent(0,a,Status::ok(),false).isOk());
    ASSERT_TRUE(batch.fileEvent(1,b,Status::ok(),false).isOk());
    EXPECT_FALSE(batch.fileEvent(0,b,Status::ok(),true).isOk());
    ASSERT_TRUE(batch.fileEvent(1,b,Status::ok(),true).isOk());
    ASSERT_TRUE(batch.fileEvent(0,a,Status::ok(),true).isOk());
    Status left;
    std::jthread worker([&] {left=batch.finish(0,std::chrono::seconds(1));});
    EXPECT_TRUE(batch.finish(1,std::chrono::seconds(1)).isOk()); worker.join();
    EXPECT_TRUE(left.isOk());
}


// The same executor contract applies to all three input shapes.
namespace {
enum class WorkShape { SingleFile, DirectoryFile, DirectoryRange };
class WorkLeaseContractTest : public ::testing::TestWithParam<WorkShape> {
 protected:
    std::vector<PersistentFileIdentity> input() const {
        auto file = tasks(1).front();
        file.totalSize = 8;
        if (GetParam() != WorkShape::SingleFile) file.relativePath = "nested/file.bin";
        if (GetParam() != WorkShape::DirectoryRange) return {file};
        file.rangeId = 1; file.rangeLength = 4; file.rangeCount = 2;
        auto second = file; second.rangeId = 2; second.rangeOffset = 4;
        return {file, second};
    }
    const WorkClock::time_point now = WorkClock::time_point(std::chrono::seconds(100));
    const std::chrono::milliseconds ttl{100};
};
}
TEST_P(WorkLeaseContractTest, LeaseBindsWorkOwnerEpochAndDeadline) {
    DynamicFileQueue queue(input(), 2, 1, 2);
    auto owner = queue.worker(0); ASSERT_TRUE(owner.isOk());
    auto next = queue.claim(owner.value(), now, ttl);
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
    const auto& lease = *next.value();
    EXPECT_GT(lease.leaseId, 0U);
    EXPECT_EQ(lease.taskId, lease.file.transferId);
    EXPECT_EQ(lease.owner.workerId, 0U);
    EXPECT_EQ(lease.owner.linkId, "link0");
    EXPECT_EQ(lease.owner.epoch.value, 1U);
    EXPECT_EQ(lease.deadline, now + ttl);
    EXPECT_EQ(lease.state, WorkLeaseState::Claimed);
}
TEST_P(WorkLeaseContractTest, PayloadAndResultMustDrainBeforeCreditOrConnectionCut) {
    DynamicFileQueue queue(input(), 1, 1, 2);
    auto owner = queue.worker(0); ASSERT_TRUE(owner.isOk());
    auto next = queue.claim(owner.value(), now, ttl);
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value()); auto lease = *next.value();
    EXPECT_FALSE(queue.commit(lease, now).isOk());
    ASSERT_TRUE(queue.begin(lease, now).isOk());
    ASSERT_TRUE(queue.startIo(lease, 8, now).isOk());
    ASSERT_TRUE(queue.requestDrain(owner.value()).isOk());
    EXPECT_FALSE(queue.claim(owner.value(), now, ttl).isOk());
    EXPECT_FALSE(queue.awaitResult(lease, now).isOk());
    EXPECT_FALSE(queue.cutReady(owner.value()).value());
    EXPECT_FALSE(queue.reopen(owner.value()).isOk());
    ASSERT_TRUE(queue.finishIo(lease, 8, now).isOk());
    ASSERT_TRUE(queue.awaitResult(lease, now).isOk());
    EXPECT_EQ(queue.outstanding(), 1U);
    EXPECT_FALSE(queue.cutReady(owner.value()).value());
    ASSERT_TRUE(queue.commit(lease, now).isOk());
    EXPECT_EQ(queue.outstanding(), 0U);
    EXPECT_FALSE(queue.commit(lease, now).isOk());
    ASSERT_TRUE(queue.cutReady(owner.value()).value());
    auto reopened = queue.reopen(owner.value()); ASSERT_TRUE(reopened.isOk());
    EXPECT_EQ(reopened.value().epoch.value, 2U);
    EXPECT_FALSE(queue.claim(owner.value(), now, ttl).isOk());
}
TEST_P(WorkLeaseContractTest, RejectsForgedIdentityLeaseWorkerLinkAndEpochAtEveryBoundary) {
    DynamicFileQueue queue(input(), 2, 1, 2);
    auto next = queue.claim(queue.worker(0).value(), now, ttl);
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value()); const auto lease = *next.value();
    for (int field = 0; field < 10; ++field) {
        auto wrong = lease;
        if (field == 0) ++wrong.leaseId;
        if (field == 1) ++wrong.owner.workerId;
        if (field == 2) wrong.owner.linkId = "foreign";
        if (field == 3) ++wrong.owner.epoch.value;
        if (field == 4) ++wrong.file.generation;
        if (field == 5) ++wrong.file.attempt;
        if (field == 6) ++wrong.file.rangeOffset;
        if (field == 7) ++wrong.file.checksum.value;
        if (field == 8) wrong.taskId = "foreign";
        if (field == 9) ++wrong.file.rangeId;
        EXPECT_FALSE(queue.begin(wrong, now).isOk()) << field;
        EXPECT_FALSE(queue.startIo(wrong, 8, now).isOk()) << field;
        EXPECT_FALSE(queue.finishIo(wrong, 8, now).isOk()) << field;
        EXPECT_FALSE(queue.awaitResult(wrong, now).isOk()) << field;
        EXPECT_FALSE(queue.commit(wrong, now).isOk()) << field;
        EXPECT_FALSE(queue.reject(wrong).isOk()) << field;
        EXPECT_FALSE(queue.requeue(wrong, now).isOk()) << field;
    }
    ASSERT_TRUE(queue.begin(lease, now).isOk());
    EXPECT_FALSE(queue.begin(lease, now).isOk());
    ASSERT_TRUE(queue.awaitResult(lease, now).isOk());
    ASSERT_TRUE(queue.commit(lease, now).isOk());
}
TEST_P(WorkLeaseContractTest, ExpiryStopsClaimsAndRetainsIoUntilExplicitDrain) {
    DynamicFileQueue queue(input(), 2, 1, 2);
    auto owner = queue.worker(0).value();
    auto next = queue.claim(owner, now, ttl);
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value()); auto lease = *next.value();
    ASSERT_TRUE(queue.begin(lease, now).isOk());
    ASSERT_TRUE(queue.startIo(lease, 8, now).isOk());
    EXPECT_EQ(queue.expire(now + ttl - std::chrono::milliseconds(1)), 0U);
    EXPECT_EQ(queue.expire(now + ttl), 1U);
    EXPECT_EQ(queue.expire(now + ttl), 0U);
    auto state = queue.snapshot(lease); ASSERT_TRUE(state.isOk());
    EXPECT_EQ(state.value().state, WorkLeaseState::Reclaimable);
    EXPECT_EQ(state.value().inflightFrames, 1U);
    EXPECT_EQ(state.value().inflightBytes, 8U);
    EXPECT_FALSE(queue.commit(lease, now + ttl).isOk());
    EXPECT_FALSE(queue.claim(owner, now + ttl, ttl).isOk());
    EXPECT_FALSE(queue.requeue(lease, now + ttl).isOk());
    ASSERT_TRUE(queue.finishIo(lease, 8, now + ttl).isOk());
    EXPECT_FALSE(queue.requeue(lease, now + ttl).isOk()) << "old transport must be closed before retry";
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    ASSERT_TRUE(queue.requeue(lease, now + ttl).isOk());
    EXPECT_FALSE(queue.snapshot(lease).isOk());
    auto retry = queue.claim(queue.worker(1).value(), now + ttl, ttl);
    ASSERT_TRUE(retry.isOk()); ASSERT_TRUE(retry.value());
    EXPECT_NE(retry.value()->leaseId, lease.leaseId);
    EXPECT_EQ(retry.value()->file.attempt, lease.file.attempt + 1);
    EXPECT_EQ(retry.value()->file.rangeId, lease.file.rangeId);
    EXPECT_FALSE(queue.commit(lease, now).isOk());
}
TEST_P(WorkLeaseContractTest, DeadlineRejectsLateResultEvenWithoutWatchdogTick) {
    DynamicFileQueue queue(input(), 1, 1, 2);
    auto next = queue.claim(queue.worker(0).value(), now, ttl);
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value()); auto lease = *next.value();
    ASSERT_TRUE(queue.begin(lease, now).isOk());
    ASSERT_TRUE(queue.awaitResult(lease, now).isOk());
    EXPECT_FALSE(queue.commit(lease, now + ttl).isOk());
    EXPECT_EQ(queue.outstanding(), 1U);
    EXPECT_EQ(queue.expire(now + ttl), 1U);
    EXPECT_EQ(queue.snapshot(lease).value().state, WorkLeaseState::Reclaimable);
}
TEST_P(WorkLeaseContractTest, DisconnectInvalidatesOldEpochAndRequeuesOnlyUncommittedWork) {
    DynamicFileQueue queue(input(), 1, 1, 2);
    auto owner = queue.worker(0).value();
    auto next = queue.claim(owner, now, ttl);
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value()); auto lease = *next.value();
    ASSERT_TRUE(queue.begin(lease, now).isOk());
    ASSERT_TRUE(queue.startIo(lease, 8, now).isOk());
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    EXPECT_FALSE(queue.commit(lease, now).isOk());
    EXPECT_FALSE(queue.reopen(owner).isOk());
    ASSERT_TRUE(queue.requeue(lease, now).isOk());
    auto reopened = queue.reopen(owner); ASSERT_TRUE(reopened.isOk());
    auto retry = queue.claim(reopened.value(), now, ttl);
    ASSERT_TRUE(retry.isOk()); ASSERT_TRUE(retry.value());
    EXPECT_EQ(retry.value()->owner.epoch.value, 2U);
    EXPECT_FALSE(queue.begin(lease, now).isOk());
    ASSERT_TRUE(queue.begin(*retry.value(), now).isOk());
    ASSERT_TRUE(queue.awaitResult(*retry.value(), now).isOk());
    ASSERT_TRUE(queue.commit(*retry.value(), now).isOk());
    EXPECT_FALSE(queue.requeue(*retry.value(), now).isOk());
}
TEST_P(WorkLeaseContractTest, CancellationInvalidatesAllHandlesAndStopsRequeue) {
    DynamicFileQueue queue(input(), 1, 1, 2);
    auto owner = queue.worker(0).value();
    auto next = queue.claim(owner, now, ttl);
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value()); auto lease = *next.value();
    ASSERT_TRUE(queue.begin(lease, now).isOk());
    queue.cancel(Status::runtimeError("cancel"));
    EXPECT_FALSE(queue.commit(lease, now).isOk());
    EXPECT_FALSE(queue.requeue(lease, now).isOk());
    EXPECT_FALSE(queue.reopen(owner).isOk());
    EXPECT_FALSE(queue.claim(owner, now, ttl).isOk());
    EXPECT_EQ(queue.outstanding(), 0U);
}
INSTANTIATE_TEST_SUITE_P(AllInputShapes, WorkLeaseContractTest,
    ::testing::Values(WorkShape::SingleFile, WorkShape::DirectoryFile, WorkShape::DirectoryRange));


TEST(WorkExecutionAdapterTest, BindsPlannerProjectionToFullV3Identity) {
    auto file = tasks(1).front(); file.totalSize = 8;
    cpnetflux::core::scheduler::WorkItem plan;
    plan.taskId = "batch"; plan.fileId = file.transferId; plan.relativePath = file.relativePath;
    plan.length = 8; plan.checksumAlgorithm = file.checksumAlgorithm;
    auto bound = bindScheduledWork(plan, file); ASSERT_TRUE(bound.isOk());
    EXPECT_EQ(bound.value().taskId, "batch");
    DynamicFileQueue boundQueue(std::vector<cpnetflux::core::io::WorkItem>{bound.value()}, 1, 1, 1);
    auto boundLease = boundQueue.claim(boundQueue.worker(0).value());
    ASSERT_TRUE(boundLease.isOk()); ASSERT_TRUE(boundLease.value());
    EXPECT_EQ(boundLease.value()->taskId, "batch");
    EXPECT_TRUE(samePersistentIdentity(bound.value().file, file));
    for (int field = 0; field < 9; ++field) {
        auto wrong = plan;
        if (field == 0) wrong.taskId.clear();
        if (field == 1) wrong.fileId = "foreign";
        if (field == 2) wrong.relativePath = "foreign";
        if (field == 3) wrong.offset = 1;
        if (field == 4) wrong.length = 7;
        if (field == 5) wrong.resumeGeneration = 9;
        if (field == 6) wrong.retryCount = 1;
        if (field == 7) wrong.checksumAlgorithm = cpnetflux::checksum::ChecksumAlgorithm::Crc32c;
        if (field == 8) wrong.dispatchable = false;
        EXPECT_FALSE(bindScheduledWork(wrong, file).isOk()) << field;
    }
}
TEST(WorkLeaseEdgeTest, RejectsInvalidWorkerTimeoutAndDeadlineOverflow) {
    DynamicFileQueue queue(tasks(2), 1, 1, 2);
    auto owner = queue.worker(0).value();
    EXPECT_FALSE(queue.worker(1).isOk());
    auto wrong = owner; wrong.linkId = "foreign";
    EXPECT_FALSE(queue.claim(wrong).isOk());
    EXPECT_FALSE(queue.claim(owner, WorkClock::now(), std::chrono::milliseconds(0)).isOk());
    EXPECT_FALSE(queue.claim(owner, WorkClock::time_point::max(), std::chrono::milliseconds(1)).isOk());
}
TEST(WorkLeaseEdgeTest, IoUnderflowOverflowAndResultRejectionPreserveLease) {
    DynamicFileQueue queue(tasks(2), 1, 1, 2);
    auto next = queue.claim(queue.worker(0).value());
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value()); auto lease = *next.value();
    EXPECT_FALSE(queue.startIo(lease, 1).isOk());
    ASSERT_TRUE(queue.begin(lease).isOk());
    EXPECT_FALSE(queue.startIo(lease, 0).isOk());
    EXPECT_FALSE(queue.finishIo(lease, 1).isOk());
    ASSERT_TRUE(queue.startIo(lease, UINT64_MAX).isOk());
    EXPECT_FALSE(queue.startIo(lease, 1).isOk());
    EXPECT_FALSE(queue.finishIo(lease, 1).isOk());
    EXPECT_EQ(queue.snapshot(lease).value().inflightBytes, UINT64_MAX);
    ASSERT_TRUE(queue.finishIo(lease, UINT64_MAX).isOk());
    ASSERT_TRUE(queue.awaitResult(lease).isOk());
    ASSERT_TRUE(queue.reject(lease).isOk());
    EXPECT_FALSE(queue.commit(lease).isOk());
    EXPECT_FALSE(queue.claim(queue.worker(0).value()).isOk());
    EXPECT_FALSE(queue.requeue(lease).isOk());
    ASSERT_TRUE(queue.disconnect(lease.owner).isOk());
    ASSERT_TRUE(queue.requeue(lease).isOk());
    auto reopened = queue.reopen(lease.owner); ASSERT_TRUE(reopened.isOk());
    auto retry = queue.claim(reopened.value()); ASSERT_TRUE(retry.isOk()); ASSERT_TRUE(retry.value());
    EXPECT_EQ(retry.value()->file.attempt, 1U);
}
TEST(WorkLeaseEdgeTest, SuccessfulIoRenewsIdleDeadlineWithoutRevivingExpiredWork) {
    const auto now = WorkClock::time_point(std::chrono::seconds(100));
    DynamicFileQueue queue(tasks(1), 1, 1, 1);
    auto next = queue.claim(queue.worker(0).value(), now, std::chrono::milliseconds(100));
    ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value()); auto lease = *next.value();
    ASSERT_TRUE(queue.begin(lease, now).isOk());
    ASSERT_TRUE(queue.startIo(lease, 1, now).isOk());
    ASSERT_TRUE(queue.finishIo(lease, 1, now + std::chrono::milliseconds(99)).isOk());
    EXPECT_EQ(queue.expire(now + std::chrono::milliseconds(100)), 0U);
    ASSERT_TRUE(queue.startIo(lease, 1, now + std::chrono::milliseconds(100)).isOk());
    ASSERT_TRUE(queue.finishIo(lease, 1, now + std::chrono::milliseconds(199)).isOk());
    EXPECT_FALSE(queue.awaitResult(lease, now + std::chrono::milliseconds(199)).isOk());
    EXPECT_EQ(queue.expire(now + std::chrono::milliseconds(199)), 1U);
}
TEST(WorkLeaseEdgeTest, TcpResetMakesOnlyUncommittedLeaseReclaimable) {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0); ASSERT_GE(listener, 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    ASSERT_EQ(::listen(listener, 1), 0); socklen_t size = sizeof(address);
    ASSERT_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size), 0);
    const int client = ::socket(AF_INET, SOCK_STREAM, 0); ASSERT_GE(client, 0);
    ASSERT_EQ(::connect(client, reinterpret_cast<sockaddr*>(&address), size), 0);
    const int peer = ::accept(listener, nullptr, nullptr); ASSERT_GE(peer, 0); ::close(listener);
    DynamicFileQueue queue(tasks(2), 1, 1, 2); auto owner = queue.worker(0).value();
    auto first = queue.claim(owner); ASSERT_TRUE(first.isOk()); ASSERT_TRUE(first.value());
    ASSERT_TRUE(queue.begin(*first.value()).isOk());
    ASSERT_TRUE(queue.awaitResult(*first.value()).isOk());
    ASSERT_TRUE(queue.commit(*first.value()).isOk());
    auto pending = queue.claim(owner); ASSERT_TRUE(pending.isOk()); ASSERT_TRUE(pending.value());
    ASSERT_TRUE(queue.begin(*pending.value()).isOk());
    ASSERT_TRUE(queue.startIo(*pending.value(), 8).isOk());
    linger reset{1, 0}; ASSERT_EQ(::setsockopt(peer, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)), 0);
    ::close(peer); pollfd descriptor{client, POLLIN, 0}; ASSERT_GT(::poll(&descriptor, 1, 1000), 0);
    char byte; EXPECT_EQ(::recv(client, &byte, 1, 0), -1); EXPECT_EQ(errno, ECONNRESET); ::close(client);
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    EXPECT_FALSE(queue.requeue(*first.value()).isOk());
    ASSERT_TRUE(queue.requeue(*pending.value()).isOk());
    auto reopened = queue.reopen(owner); ASSERT_TRUE(reopened.isOk());
    auto retry = queue.claim(reopened.value()); ASSERT_TRUE(retry.isOk()); ASSERT_TRUE(retry.value());
    EXPECT_EQ(retry.value()->file.fileId, pending.value()->file.fileId);
    EXPECT_FALSE(queue.commit(*pending.value()).isOk());
}

TEST(WorkLeaseEdgeTest, ForeignQueueCannotReuseHandleForIdenticalWork) {
    DynamicFileQueue left(tasks(1), 1, 1, 1), right(tasks(1), 1, 1, 1);
    auto a = left.claim(left.worker(0).value()), b = right.claim(right.worker(0).value());
    ASSERT_TRUE(a.isOk()); ASSERT_TRUE(a.value()); ASSERT_TRUE(b.isOk()); ASSERT_TRUE(b.value());
    EXPECT_NE(a.value()->leaseId, b.value()->leaseId);
    EXPECT_FALSE(right.begin(*a.value()).isOk());
    EXPECT_TRUE(right.begin(*b.value()).isOk());
}
TEST(WorkExecutionAdapterTest, RejectsMalformedFullIdentityAndIncompleteRangePartition) {
    auto file = tasks(1).front();
    cpnetflux::core::scheduler::WorkItem plan;
    plan.taskId = "batch"; plan.fileId = file.transferId; plan.relativePath = file.relativePath;
    plan.length = file.totalSize; plan.checksumAlgorithm = file.checksumAlgorithm;
    for (int field = 0; field < 5; ++field) {
        auto wrong = file;
        if (field == 0) wrong.fileId = 0;
        if (field == 1) wrong.generation = 0;
        if (field == 2) wrong.chunkSize = 0;
        if (field == 3) wrong.rangeLength = 1;
        if (field == 4) wrong.rangeCount = 1;
        EXPECT_FALSE(bindScheduledWork(plan, wrong).isOk()) << field;
        DynamicFileQueue queue(std::vector<PersistentFileIdentity>{wrong}, 1, 1, 1);
        EXPECT_FALSE(queue.claim(0).isOk()) << field;
    }
}

TEST(WorkExecutionAdapterTest, PreservesGlobalSchedulerPlanOrderInTheSharedQueue) {
    auto first = tasks(2);
    first[0].totalSize = 8;
    first[1].totalSize = 16;
    cpnetflux::core::scheduler::FilePlan planA;
    planA.taskId = "global"; planA.fileId = first[0].transferId; planA.relativePath = first[0].relativePath;
    planA.totalBytes = first[0].totalSize; planA.resumeGeneration = first[0].generation;
    planA.checksumAlgorithm = first[0].checksumAlgorithm; planA.workItems.push_back({"global", first[0].transferId,
        first[0].relativePath, 0, first[0].totalSize, cpnetflux::core::scheduler::SchedulerDirection::Upload,
        first[0].checksumAlgorithm, {}, 0, first[0].generation, true});
    cpnetflux::core::scheduler::FilePlan planB = planA;
    planB.fileId = first[1].transferId; planB.relativePath = first[1].relativePath;
    planB.totalBytes = first[1].totalSize; planB.resumeGeneration = first[1].generation;
    planB.workItems.front().fileId = first[1].transferId; planB.workItems.front().relativePath = first[1].relativePath;
    planB.workItems.front().length = first[1].totalSize;
    auto bound = bindScheduledPlans({planB, planA}, first);
    ASSERT_TRUE(bound.isOk()) << bound.status().message();
    DynamicFileQueue queue(bound.value(), 1, 1, 2, true);
    auto lease = queue.claim(queue.worker(0).value());
    ASSERT_TRUE(lease.isOk()); ASSERT_TRUE(lease.value());
    EXPECT_EQ(lease.value()->file.fileId, first[1].fileId);
}

TEST(WorkExecutionAdapterTest, IncomingLeaseUsesSchedulerIdentityAndEpoch) {
    cpnetflux::core::scheduler::SchedulerConfig config;
    config.taskId = "incoming-global";
    config.linkId = "research-link";
    config.initialConnections = 1;
    config.maxConnections = 1;
    auto scheduler = cpnetflux::core::scheduler::GlobalScheduler::create(config);
    ASSERT_TRUE(scheduler.isOk());
    DynamicFileQueue queue(std::vector<WorkItem>{}, 1, 1, 1, true);
    ASSERT_TRUE(queue.setConnectionEpoch(ConnectionEpoch{7}).isOk());
    ASSERT_TRUE(queue.attachScheduler(&scheduler.value()).isOk());
    auto owner = queue.worker(0);
    ASSERT_TRUE(owner.isOk());
    auto file = tasks(1).front();
    auto incoming = queue.claimIncoming(owner.value(), WorkItem{file, scheduler.value().taskId()});
    ASSERT_TRUE(incoming.isOk());
    EXPECT_EQ(incoming.value().owner.epoch.value, 7U);
    ASSERT_TRUE(queue.begin(incoming.value()).isOk());
    ASSERT_TRUE(queue.awaitResult(incoming.value()).isOk());
    ASSERT_TRUE(queue.commit(incoming.value()).isOk());
    EXPECT_FALSE(queue.commit(incoming.value()).isOk());
    EXPECT_EQ(queue.outstanding(), 0U);
}

TEST(WorkExecutionAdapterTest, GlobalSchedulerUsesQueueLeaseOwnerAndEpoch) {
    cpnetflux::core::scheduler::SchedulerConfig config;
    config.taskId = "shared-global"; config.linkId = "research-link";
    config.initialConnections = 1; config.maxConnections = 2;
    config.policy = cpnetflux::core::scheduler::SchedulerPolicy::Adaptive;
    auto scheduler = cpnetflux::core::scheduler::GlobalScheduler::create(config);
    ASSERT_TRUE(scheduler.isOk());
    DynamicFileQueue queue(tasks(2), 2, 1, 2);
    ASSERT_TRUE(queue.setConnectionEpoch(ConnectionEpoch{2}).isOk());
    ASSERT_TRUE(queue.attachScheduler(&scheduler.value()).isOk());
    auto worker = queue.worker(0); ASSERT_TRUE(worker.isOk());
    EXPECT_EQ(worker.value().linkId, "research-link"); EXPECT_EQ(worker.value().epoch.value, 2U);
    auto next = queue.claim(worker.value()); ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
    ASSERT_TRUE(queue.begin(*next.value()).isOk()); ASSERT_TRUE(queue.awaitResult(*next.value()).isOk());
    ASSERT_TRUE(queue.commit(*next.value()).isOk());
    EXPECT_FALSE(queue.attachScheduler(&scheduler.value()).isOk());
    EXPECT_FALSE(queue.setConnectionEpoch(ConnectionEpoch{3}).isOk());
}

TEST(WorkExecutorTest, FinalizeSharesSuccessAndFailureLeaseTransitions) {
    DynamicFileQueue queue(tasks(2), 1, 1, 2);
    auto owner = queue.worker(0); ASSERT_TRUE(owner.isOk());
    WorkExecutor executor(queue, owner.value());
    auto first = executor.claim(); ASSERT_TRUE(first.isOk()); ASSERT_TRUE(first.value());
    ASSERT_TRUE(executor.begin(*first.value()).isOk());
    ASSERT_TRUE(executor.finalize(*first.value(), Status::ok()).isOk());
    EXPECT_EQ(queue.outstanding(), 0U);
    auto second = executor.claim(); ASSERT_TRUE(second.isOk()); ASSERT_TRUE(second.value());
    ASSERT_TRUE(executor.begin(*second.value()).isOk());
    ASSERT_TRUE(executor.finalize(*second.value(), Status::runtimeError("receiver failed")).isOk());
    EXPECT_EQ(queue.outstanding(), 1U);
    EXPECT_EQ(queue.snapshot(*second.value()).value().state, WorkLeaseState::Reclaimable);
}

TEST(WorkExecutorTest, TerminalFailureReleasesCreditAndAllowsNextFile) {
    DynamicFileQueue queue(tasks(2), 1, 1, 2);
    WorkExecutor executor(queue, queue.worker(0).value());

    auto first = executor.claim();
    ASSERT_TRUE(first.isOk());
    ASSERT_TRUE(first.value());
    ASSERT_TRUE(executor.begin(*first.value()).isOk());
    ASSERT_TRUE(executor.awaitResult(*first.value()).isOk());
    const auto failed = Status::runtimeError("file rejected");
    ASSERT_TRUE(executor.failTerminal(*first.value(), failed).isOk());
    EXPECT_EQ(queue.outstanding(), 0U);
    EXPECT_FALSE(executor.finalize(*first.value(), Status::ok()).isOk());

    auto second = executor.claim();
    ASSERT_TRUE(second.isOk());
    ASSERT_TRUE(second.value());
    EXPECT_NE(second.value()->file.fileId, first.value()->file.fileId);
    ASSERT_TRUE(executor.begin(*second.value()).isOk());
    ASSERT_TRUE(executor.finalize(*second.value(), Status::ok()).isOk());
    EXPECT_EQ(queue.outstanding(), 0U);
    EXPECT_FALSE(executor.claim().value().has_value());
}

TEST(WorkExecutorTest, StaticAffinityKeepsPersistentChannelOwnership) {
    DynamicFileQueue queue(tasks(4), 2, 1, 4, true, true);
    WorkExecutor left(queue, queue.worker(0).value());
    WorkExecutor right(queue, queue.worker(1).value());

    for (std::uint32_t expected = 1; expected <= 4; ++expected) {
        auto& executor = expected % 2 == 1 ? left : right;
        auto next = executor.claim();
        ASSERT_TRUE(next.isOk());
        ASSERT_TRUE(next.value());
        EXPECT_EQ(next.value()->file.fileId, expected);
        ASSERT_TRUE(executor.begin(*next.value()).isOk());
        ASSERT_TRUE(executor.finalize(*next.value(), Status::ok()).isOk());
    }
    EXPECT_EQ(queue.outstanding(), 0U);
}

TEST(WorkExecutorTest, StaticAffinityPreservesPerChannelFileOrderAcrossInterleaving) {
    DynamicFileQueue queue(tasks(6), 2, 4, 2, true, true);
    EXPECT_EQ(queue.highWatermark(), 6U);
    auto worker0 = queue.worker(0);
    auto worker1 = queue.worker(1);
    ASSERT_TRUE(worker0.isOk());
    ASSERT_TRUE(worker1.isOk());

    auto rightFirst = queue.claim(worker1.value());
    ASSERT_TRUE(rightFirst.isOk());
    ASSERT_TRUE(rightFirst.value());
    EXPECT_EQ(rightFirst.value()->file.fileId, 2U);

    auto leftFirst = queue.claim(worker0.value());
    ASSERT_TRUE(leftFirst.isOk());
    ASSERT_TRUE(leftFirst.value());
    EXPECT_EQ(leftFirst.value()->file.fileId, 1U);

    auto rightSecond = queue.claim(worker1.value());
    ASSERT_TRUE(rightSecond.isOk());
    ASSERT_TRUE(rightSecond.value());
    EXPECT_EQ(rightSecond.value()->file.fileId, 4U);

    auto leftSecond = queue.claim(worker0.value());
    ASSERT_TRUE(leftSecond.isOk());
    ASSERT_TRUE(leftSecond.value());
    EXPECT_EQ(leftSecond.value()->file.fileId, 3U);
}

TEST(WorkExecutorTest, StaticIncomingLeaseRejectsWrongChannel) {
    DynamicFileQueue queue(std::vector<WorkItem>{}, 2, 1, 2, true, true);
    WorkExecutor left(queue, queue.worker(0).value());
    WorkExecutor right(queue, queue.worker(1).value());
    const auto file = tasks(1).front();
    EXPECT_FALSE(right.claimIncoming(WorkItem{file, file.transferId}).isOk());
    auto accepted = left.claimIncoming(WorkItem{file, file.transferId});
    ASSERT_TRUE(accepted.isOk());
    ASSERT_TRUE(left.begin(accepted.value()).isOk());
    ASSERT_TRUE(left.finalize(accepted.value(), Status::ok()).isOk());
}

TEST(WorkExecutorTest, StopClaimsKeepsActiveLeaseUntilWorkerQuiesces) {
    DynamicFileQueue queue(tasks(2), 1, 1, 2);
    WorkExecutor executor(queue, queue.worker(0).value());
    auto active = executor.claim();
    ASSERT_TRUE(active.isOk());
    ASSERT_TRUE(active.value());
    ASSERT_TRUE(executor.begin(*active.value()).isOk());

    ASSERT_TRUE(queue.stopClaims().isOk());
    auto stopped = executor.claim();
    ASSERT_TRUE(stopped.isOk());
    EXPECT_FALSE(stopped.value());
    EXPECT_EQ(queue.outstanding(), 1U);
    ASSERT_TRUE(executor.finalize(*active.value(), Status::ok()).isOk());
    EXPECT_EQ(queue.outstanding(), 0U);
}

TEST(WorkRecoveryTest, ReconcilesOnlyMissingActiveWorkAndAdvancesEpochAndAttempt) {
    DynamicFileQueue queue(tasks(2), 1, 2, 2);
    auto owner = queue.worker(0); ASSERT_TRUE(owner.isOk());
    auto first = queue.claim(owner.value()); ASSERT_TRUE(first.isOk()); ASSERT_TRUE(first.value());
    auto second = queue.claim(owner.value()); ASSERT_TRUE(second.isOk()); ASSERT_TRUE(second.value());
    ASSERT_TRUE(queue.begin(*first.value()).isOk());
    ASSERT_TRUE(queue.begin(*second.value()).isOk());
    ASSERT_TRUE(queue.disconnect(owner.value()).isOk());
    auto reopened = queue.recover(owner.value(), {
        {*first.value(), WorkRecoveryDisposition::Committed},
        {*second.value(), WorkRecoveryDisposition::Missing}}, 3);
    ASSERT_TRUE(reopened.isOk()) << reopened.status().message();
    EXPECT_EQ(reopened.value().epoch.value, owner.value().epoch.value + 1);
    EXPECT_EQ(queue.outstanding(), 0U);
    auto retry = queue.claim(reopened.value()); ASSERT_TRUE(retry.isOk()); ASSERT_TRUE(retry.value());
    EXPECT_EQ(retry.value()->file.fileId, second.value()->file.fileId);
    EXPECT_EQ(retry.value()->file.attempt, second.value()->file.attempt + 1);
    EXPECT_FALSE(queue.begin(*second.value()).isOk());
    ASSERT_TRUE(queue.begin(*retry.value()).isOk());
    ASSERT_TRUE(queue.awaitResult(*retry.value()).isOk());
    ASSERT_TRUE(queue.commit(*retry.value()).isOk());
}

TEST(WorkRecoveryTest, RejectsRetryBeyondBoundWithoutMutatingQueue) {
    DynamicFileQueue queue(tasks(1), 1, 1, 1);
    auto owner = queue.worker(0).value();
    auto first = queue.claim(owner); ASSERT_TRUE(first.isOk()); ASSERT_TRUE(first.value());
    ASSERT_TRUE(queue.begin(*first.value()).isOk());
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    auto reopened = queue.recover(owner, {{*first.value(), WorkRecoveryDisposition::Missing}}, 1);
    ASSERT_TRUE(reopened.isOk());
    auto retry = queue.claim(reopened.value()); ASSERT_TRUE(retry.isOk()); ASSERT_TRUE(retry.value());
    ASSERT_TRUE(queue.begin(*retry.value()).isOk());
    ASSERT_TRUE(queue.disconnect(reopened.value()).isOk());
    auto bounded = queue.recover(reopened.value(), {{*retry.value(), WorkRecoveryDisposition::Missing}}, 1);
    EXPECT_FALSE(bounded.isOk());
    EXPECT_EQ(queue.outstanding(), 1U);
    EXPECT_FALSE(queue.claim(reopened.value()).isOk());
}

TEST(WorkExecutorTest, RejectsAnotherWorkersValidLeaseAtEveryBoundary) {
    DynamicFileQueue queue(tasks(2), 2, 1, 2);
    WorkExecutor left(queue, queue.worker(0).value());
    WorkExecutor right(queue, queue.worker(1).value());
    auto next = right.claim(); ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
    const auto lease = *next.value();
    EXPECT_FALSE(left.begin(lease).isOk());
    ASSERT_TRUE(right.begin(lease).isOk());
    EXPECT_FALSE(left.startIo(lease, 1).isOk());
    ASSERT_TRUE(right.startIo(lease, 1).isOk());
    EXPECT_FALSE(left.finishIo(lease, 1).isOk());
    ASSERT_TRUE(right.finishIo(lease, 1).isOk());
    EXPECT_FALSE(left.awaitResult(lease).isOk());
    ASSERT_TRUE(right.awaitResult(lease).isOk());
    EXPECT_FALSE(left.commit(lease).isOk());
    EXPECT_FALSE(left.reject(lease).isOk());
    EXPECT_FALSE(left.finalize(lease, Status::ok()).isOk());
    ASSERT_TRUE(right.commit(lease).isOk());
}

TEST(WorkRecoveryTest, RejectsAlteredFullIdentityAndForeignWorkerBeforeMutation) {
    DynamicFileQueue queue(tasks(3), 2, 2, 3);
    auto owner = queue.worker(0).value();
    auto first = queue.claim(owner); ASSERT_TRUE(first.isOk()); ASSERT_TRUE(first.value());
    auto foreign = queue.claim(queue.worker(1).value()); ASSERT_TRUE(foreign.isOk()); ASSERT_TRUE(foreign.value());
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    for (int field = 0; field < 7; ++field) {
        auto changed = *first.value();
        if (field == 0) ++changed.file.totalSize;
        if (field == 1) ++changed.file.chunkSize;
        if (field == 2) ++changed.file.mtimeUnixSeconds;
        if (field == 3) ++changed.file.rangeOffset;
        if (field == 4) ++changed.file.checksum.value;
        if (field == 5) ++changed.file.generation;
        if (field == 6) ++changed.file.attempt;
        EXPECT_FALSE(queue.recover(owner, {{changed, WorkRecoveryDisposition::Missing}}, 3).isOk()) << field;
    }
    EXPECT_FALSE(queue.recover(owner, {{*foreign.value(), WorkRecoveryDisposition::Missing}}, 3).isOk());
    EXPECT_EQ(queue.outstanding(), 2U);
    EXPECT_EQ(queue.worker(0).value(), owner);
}

TEST(WorkRecoveryTest, RequiresCompleteUniqueLedgerAndConfirmedTeardown) {
    DynamicFileQueue queue(tasks(2), 1, 2, 2);
    auto owner = queue.worker(0).value();
    auto a = queue.claim(owner), b = queue.claim(owner);
    ASSERT_TRUE(a.isOk()); ASSERT_TRUE(a.value());
    ASSERT_TRUE(b.isOk()); ASSERT_TRUE(b.value());
    const WorkRecoveryDecision first{*a.value(), WorkRecoveryDisposition::Missing};
    const WorkRecoveryDecision second{*b.value(), WorkRecoveryDisposition::Committed};
    EXPECT_FALSE(queue.recover(owner, {first, second}).isOk());
    ASSERT_TRUE(queue.requestDrain(owner).isOk());
    EXPECT_FALSE(queue.recover(owner, {first, second}).isOk());
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    EXPECT_FALSE(queue.recover(owner, {first}).isOk());
    EXPECT_FALSE(queue.recover(owner, {first, first}).isOk());
    auto forged = first; ++forged.lease.leaseId;
    EXPECT_FALSE(queue.recover(owner, {forged, second}).isOk());
    auto invalid = first; invalid.disposition = static_cast<WorkRecoveryDisposition>(9);
    EXPECT_FALSE(queue.recover(owner, {invalid, second}).isOk());
    EXPECT_EQ(queue.outstanding(), 2U);
    EXPECT_EQ(queue.worker(0).value(), owner);
    ASSERT_TRUE(queue.recover(owner, {first, second}).isOk());
    EXPECT_FALSE(queue.commit(*a.value()).isOk());
    EXPECT_FALSE(queue.commit(*b.value()).isOk());
}

TEST(WorkRecoveryTest, ZeroRetryBudgetAllowsConfirmedCommitButNeverMissingWork) {
    DynamicFileQueue queue(tasks(1), 1, 1, 1);
    auto owner = queue.worker(0).value();
    auto next = queue.claim(owner); ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    EXPECT_FALSE(queue.recover(owner, {{*next.value(), WorkRecoveryDisposition::Missing}}, 0).isOk());
    auto recovered = queue.recover(owner, {{*next.value(), WorkRecoveryDisposition::Committed}}, 0);
    ASSERT_TRUE(recovered.isOk());
    auto empty = queue.claim(recovered.value()); ASSERT_TRUE(empty.isOk()); EXPECT_FALSE(empty.value());
    EXPECT_EQ(queue.outstanding(), 0U);
}

TEST(WorkRecoveryTest, EpochExhaustionAndCancellationLeaveRecoveryUnavailable) {
    DynamicFileQueue queue(tasks(1), 1, 1, 1);
    ASSERT_TRUE(queue.setConnectionEpoch({UINT64_MAX}).isOk());
    auto owner = queue.worker(0).value();
    auto next = queue.claim(owner); ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    const std::vector<WorkRecoveryDecision> ledger{{*next.value(), WorkRecoveryDisposition::Committed}};
    EXPECT_FALSE(queue.recover(owner, ledger).isOk());
    EXPECT_EQ(queue.outstanding(), 1U);
    EXPECT_EQ(queue.worker(0).value().epoch.value, UINT64_MAX);
    queue.cancel(Status::runtimeError("explicit cancellation"));
    EXPECT_FALSE(queue.recover(owner, ledger).isOk());
}

TEST(WorkExecutorTest, RecoveredExecutorClaimsNewEpochAndRejectsOldHandles) {
    DynamicFileQueue queue(tasks(1), 1, 1, 1);
    WorkExecutor executor(queue, queue.worker(0).value());
    auto next = executor.claim(); ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
    ASSERT_TRUE(executor.begin(*next.value()).isOk());
    ASSERT_TRUE(queue.disconnect(executor.owner()).isOk());
    ASSERT_TRUE(executor.recover({{*next.value(), WorkRecoveryDisposition::Missing}}, 1).isOk());
    EXPECT_EQ(executor.owner().epoch.value, 2U);
    EXPECT_FALSE(executor.finalize(*next.value(), Status::ok()).isOk());
    auto retried = executor.claim(); ASSERT_TRUE(retried.isOk()); ASSERT_TRUE(retried.value());
    ASSERT_TRUE(executor.begin(*retried.value()).isOk());
    ASSERT_TRUE(executor.startIo(*retried.value(), 1).isOk());
    EXPECT_FALSE(executor.finalize(*retried.value(), Status::ok()).isOk());
    ASSERT_TRUE(executor.finishIo(*retried.value(), 1).isOk());
    ASSERT_TRUE(executor.awaitResult(*retried.value()).isOk());
    ASSERT_TRUE(executor.finalize(*retried.value(), Status::ok()).isOk());
    EXPECT_FALSE(executor.finalize(*retried.value(), Status::ok()).isOk());
}

TEST(WorkRecoveryTest, SchedulerAccountingTracksReconciliationAndMissingRetries) {
    char pattern[] = "/tmp/cpnetflux-recovery-metrics-XXXXXX";
    const char* created = ::mkdtemp(pattern); ASSERT_NE(created, nullptr);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{created};
    for (const bool rejected : {false, true}) {
        cpnetflux::core::scheduler::SchedulerConfig config;
        const auto eventPath = cleanup.root / (rejected ? "rejected.jsonl" : "disconnected.jsonl");
        config.taskId = "recovery-test"; config.metricsPaths.eventsJsonl = eventPath.string();
        auto scheduler = cpnetflux::core::scheduler::GlobalScheduler::create(config);
        ASSERT_TRUE(scheduler.isOk());
        DynamicFileQueue queue(tasks(3), 1, 2, 3);
        ASSERT_TRUE(queue.attachScheduler(&scheduler.value()).isOk());
        auto owner = queue.worker(0).value();
        auto a = queue.claim(owner), b = queue.claim(owner);
        ASSERT_TRUE(a.isOk()); ASSERT_TRUE(a.value());
        ASSERT_TRUE(b.isOk()); ASSERT_TRUE(b.value());
        const auto* link = scheduler.value().linkManager().link("link0");
        ASSERT_NE(link, nullptr);
        EXPECT_EQ(link->inflightBytes, a.value()->file.totalSize + b.value()->file.totalSize);
        if (rejected) ASSERT_TRUE(queue.reject(*a.value()).isOk());
        ASSERT_TRUE(queue.disconnect(owner).isOk());
        auto recovered = queue.recover(owner, {
            {*a.value(), WorkRecoveryDisposition::Missing},
            {*b.value(), WorkRecoveryDisposition::Committed}});
        ASSERT_TRUE(recovered.isOk()) << recovered.status().message();
        EXPECT_EQ(link->inflightBytes, 0U);
        EXPECT_EQ(link->retryCount, 1U);
        EXPECT_EQ(link->readyBytes, a.value()->file.totalSize + 1U);
        auto retried = queue.claim(recovered.value());
        ASSERT_TRUE(retried.isOk()); ASSERT_TRUE(retried.value());
        EXPECT_EQ(retried.value()->file.fileId, a.value()->file.fileId);
        WorkExecutor executor(queue, recovered.value());
        ASSERT_TRUE(executor.begin(*retried.value()).isOk());
        ASSERT_TRUE(executor.finalize(*retried.value(), Status::ok()).isOk());
        auto untouched = executor.claim();
        ASSERT_TRUE(untouched.isOk()); ASSERT_TRUE(untouched.value());
        EXPECT_EQ(untouched.value()->file.fileId, 1U);
        ASSERT_TRUE(executor.begin(*untouched.value()).isOk());
        ASSERT_TRUE(executor.finalize(*untouched.value(), Status::ok()).isOk());
        EXPECT_EQ(link->readyBytes, 0U); EXPECT_EQ(link->inflightBytes, 0U);
        std::ifstream events(eventPath);
        const std::string text((std::istreambuf_iterator<char>(events)), {});
        EXPECT_NE(text.find("\"event\":\"workitem_reconciled\""), std::string::npos);
        EXPECT_NE(text.find("\"reason\":\"manifest_confirmed_commit\""), std::string::npos);
        EXPECT_NE(text.find("\"event\":\"workitem_retry\""), std::string::npos);
        EXPECT_NE(text.find("\"connection_epoch\":2"), std::string::npos);
    }
}
TEST(DynamicFileQueueTest, RangeAffinityAssignsDistinctLaneIds) {
    auto file = tasks(1).front();
    file.totalSize = 8;
    file.rangeId = 1; file.rangeOffset = 0; file.rangeLength = 4; file.rangeCount = 2;
    auto second = file;
    second.rangeId = 2; second.rangeOffset = 4;
    DynamicFileQueue queue(
        std::vector<PersistentFileIdentity>{file, second}, 2, 1, 2, true, false,
        std::vector<std::string>{"link0", "link1"}, true);
    auto first = queue.claim(queue.worker(0).value());
    ASSERT_TRUE(first.isOk()) << first.status().message();
    ASSERT_TRUE(first.value());
    EXPECT_EQ(first.value()->file.rangeId, 1U);
    EXPECT_EQ(first.value()->owner.linkId, "link0");
    auto secondLease = queue.claim(queue.worker(1).value());
    ASSERT_TRUE(secondLease.isOk()) << secondLease.status().message();
    ASSERT_TRUE(secondLease.value());
    EXPECT_EQ(secondLease.value()->file.rangeId, 2U);
    EXPECT_EQ(secondLease.value()->owner.linkId, "link1");
}
TEST(DynamicFileQueueTest, GlobalSchedulerSelectsHealthyLogicalLaneAndPublishesRecovery) {
    cpnetflux::core::scheduler::SchedulerConfig config;
    config.taskId = "flowcut-queue";
    config.linkId = "link0";
    config.capacityGbps = 100.0;
    config.initialConnections = 2;
    config.maxConnections = 2;
    config.linkSetLinks = 2;
    auto schedulerResult = cpnetflux::core::scheduler::GlobalScheduler::create(config);
    ASSERT_TRUE(schedulerResult.isOk()) << schedulerResult.status().message();
    auto scheduler = std::move(schedulerResult.value());

    auto fileSet = tasks(1);
    fileSet[0].rangeId = 1;
    fileSet[0].rangeOffset = 0;
    fileSet[0].rangeLength = fileSet[0].totalSize;
    fileSet[0].rangeCount = 1;
    DynamicFileQueue queue(fileSet, 2, 1, 2, true, false,
                           std::vector<std::string>{"link0", "link1"}, true);
    ASSERT_TRUE(queue.attachScheduler(&scheduler).isOk());
    ASSERT_TRUE(scheduler.linkSet().setRuntime("link0", 1000, 10, 100).isOk());
    ASSERT_TRUE(scheduler.linkSet().setRuntime("link1", 1, 10, 100).isOk());

    auto laneOne = queue.worker(1);
    ASSERT_TRUE(laneOne.isOk());
    auto lease = queue.claim(laneOne.value());
    ASSERT_TRUE(lease.isOk()) << lease.status().message();
    ASSERT_TRUE(lease.value());
    EXPECT_EQ(lease.value()->owner.linkId, "link1");
    ASSERT_TRUE(queue.begin(*lease.value()).isOk());
    ASSERT_TRUE(queue.awaitResult(*lease.value()).isOk());
    ASSERT_TRUE(queue.commit(*lease.value()).isOk());

    ASSERT_TRUE(queue.requestDrain(laneOne.value()).isOk());
    EXPECT_TRUE(scheduler.linkSet().runtime("link1")->draining);
    ASSERT_TRUE(queue.disconnect(laneOne.value()).isOk());
    EXPECT_FALSE(scheduler.linkSet().runtime("link1")->healthy);

    auto recovered = queue.recover(laneOne.value(), {});
    ASSERT_TRUE(recovered.isOk()) << recovered.status().message();
    EXPECT_EQ(recovered.value().epoch.value, laneOne.value().epoch.value + 1);
    EXPECT_TRUE(scheduler.linkSet().runtime("link1")->healthy);
    EXPECT_FALSE(scheduler.linkSet().runtime("link1")->draining);
    EXPECT_EQ(scheduler.linkSet().runtime("link1")->connectionEpoch,
              recovered.value().epoch.value);
}
