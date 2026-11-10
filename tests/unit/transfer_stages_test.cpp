#include "cpnetflux/core/io/transfer_stages.h"
#include <gtest/gtest.h>
#include "cpnetflux/core/io/dynamic_file_queue.h"
#include <future>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdlib>
using namespace cpnetflux::core::io;
using namespace std::chrono_literals;
TEST(TransferStagesTest, LargeFramesAccountAllOwnersAndRejectBudgetOrShapeDrift) {
 auto stages=TransferStages::create(4,2,false,262148);
 ASSERT_TRUE(stages.isOk());EXPECT_EQ(stages.value()->ownershipBoundBytes,4ULL*8*262148);
 DynamicFileQueue queue(std::vector<PersistentFileIdentity>{},4,1,8);
 ASSERT_TRUE(queue.transferStages(4,2,262148).isOk());
 EXPECT_FALSE(queue.transferStages(4,2,65536).isOk());
 setenv("CPNETFLUX_STAGE_BUFFER_BYTES","8388608",1);
 EXPECT_FALSE(TransferStages::preflight(4,262148).isOk());
 unsetenv("CPNETFLUX_STAGE_BUFFER_BYTES");
 EXPECT_FALSE(TransferStages::create(64,64,false,262148).isOk());
 EXPECT_FALSE(TransferStages::create(4,2,false,262144).isOk());
}
TEST(TransferStagesTest, OneMiBFrameFitsFourLanesWithinExistingBudget) {
 constexpr std::uint32_t frame=1048580;
 auto stages=TransferStages::create(4,2,false,frame);
 ASSERT_TRUE(stages.isOk());
 EXPECT_EQ(stages.value()->ownershipBoundBytes,4ULL*8*frame);
 EXPECT_EQ(stages.value()->bufferBudgetBytes,64ULL*1024*1024);
 setenv("CPNETFLUX_STAGE_BUFFER_BYTES","33554432",1);
 EXPECT_FALSE(TransferStages::create(4,2,false,frame).isOk());
 unsetenv("CPNETFLUX_STAGE_BUFFER_BYTES");
}
TEST(TransferStagesTest, OneWriterBlocksSecondUntilItsPermitLeaves) {
 StageGate gate(1);auto first=gate.acquire();ASSERT_TRUE(first.isOk());
 std::promise<void> started;auto pending=std::async(std::launch::async,[&]{started.set_value();return gate.acquire();});started.get_future().wait();
 EXPECT_EQ(pending.wait_for(50ms),std::future_status::timeout);
 first=StageGate::Permit{};EXPECT_TRUE(pending.get().isOk());
}
TEST(TransferStagesTest, TimeoutDoesNotStrandTheFollowingWaiter) {
 StageGate gate(1);auto first=gate.acquire();ASSERT_TRUE(first.isOk());
 auto expired=gate.acquire(nullptr,std::chrono::steady_clock::now()+20ms);EXPECT_FALSE(expired.isOk());
 first=StageGate::Permit{};EXPECT_TRUE(gate.acquire(nullptr,std::chrono::steady_clock::now()+1s).isOk());
}
TEST(TransferStagesTest, ReadAndWriteLimitsAreIndependent) {
 TransferStages stages(1,1,2);auto read=stages.reads.acquire();auto write=stages.writes.acquire(nullptr,std::chrono::steady_clock::now()+50ms);EXPECT_TRUE(read.isOk());EXPECT_TRUE(write.isOk());
}
TEST(TransferStagesTest, ReleasingAfterExceptionAllowsProgress) {
 StageGate gate(1);try{auto permit=gate.acquire();ASSERT_TRUE(permit.isOk());throw 1;}catch(int){}
 EXPECT_TRUE(gate.acquire(nullptr,std::chrono::steady_clock::now()+100ms).isOk());
}
TEST(TransferStagesTest, CapacityTwoPermitsExactlyTwoActiveOperations) {
 StageGate gate(2);auto a=gate.acquire();auto b=gate.acquire();ASSERT_TRUE(a.isOk());ASSERT_TRUE(b.isOk());
 EXPECT_FALSE(gate.acquire(nullptr,std::chrono::steady_clock::now()+20ms).isOk());
 a=StageGate::Permit{};EXPECT_TRUE(gate.acquire(nullptr,std::chrono::steady_clock::now()+100ms).isOk());
}
TEST(TransferStagesTest, OversizedOwnershipRejectsBeforeAllocatingPayload) {
 setenv("CPNETFLUX_STAGE_BUFFER_BYTES","65536",1);auto stages=TransferStages::create(4,2);unsetenv("CPNETFLUX_STAGE_BUFFER_BYTES");EXPECT_FALSE(stages.isOk());
}
TEST(TransferStagesTest, InvalidConcurrencyIsRejected) {
 setenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY","-1",1);auto stages=TransferStages::create(1,2);unsetenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY");EXPECT_FALSE(stages.isOk());
}
TEST(TransferStagesTest, ReportsConservativeAllLaneOwnership) {
 auto stages=TransferStages::create(4,2);ASSERT_TRUE(stages.isOk());EXPECT_EQ(stages.value()->ownershipBoundBytes,4ULL*8*65536);EXPECT_LE(stages.value()->ownershipBoundBytes,stages.value()->bufferBudgetBytes);
}

TEST(TransferStagesTest, V3BatchSharesOneAdmissionAuthorityAndRejectsShapeDrift) {
 DynamicFileQueue queue(std::vector<PersistentFileIdentity>{},4,1,8);
 auto a=queue.transferStages(4,2);auto b=queue.transferStages(4,2);
 ASSERT_TRUE(a.isOk());ASSERT_TRUE(b.isOk());EXPECT_EQ(a.value(),b.value());
 EXPECT_FALSE(queue.transferStages(2,2).isOk());
}
TEST(TransferStagesTest, ExplicitIndependentConfigurationAndBudgetAreReported) {
 setenv("CPNETFLUX_STAGE_READ_CONCURRENCY","1",1);setenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY","2",1);setenv("CPNETFLUX_STAGE_BUFFER_BYTES","2097152",1);
 auto stages=TransferStages::create(4,2);
 unsetenv("CPNETFLUX_STAGE_READ_CONCURRENCY");unsetenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY");unsetenv("CPNETFLUX_STAGE_BUFFER_BYTES");
 ASSERT_TRUE(stages.isOk());EXPECT_EQ(stages.value()->readConcurrency,1U);EXPECT_EQ(stages.value()->writeConcurrency,2U);EXPECT_EQ(stages.value()->networkLanes,4U);EXPECT_EQ(stages.value()->bufferBudgetBytes,2097152U);
}

TEST(TransferStagesTest, MoveAssignmentReleasesPreviousGateAndTransfersOwnership) {
 StageGate a(1), b(1);auto first=a.acquire();auto second=b.acquire();
 ASSERT_TRUE(first.isOk());ASSERT_TRUE(second.isOk());
 first.value()=std::move(second.value());
 EXPECT_TRUE(a.acquire(nullptr,std::chrono::steady_clock::now()+100ms).isOk());
 EXPECT_FALSE(b.acquire(nullptr,std::chrono::steady_clock::now()+20ms).isOk());
 first=StageGate::Permit{};
 EXPECT_TRUE(b.acquire(nullptr,std::chrono::steady_clock::now()+100ms).isOk());
}
TEST(TransferStagesTest, PermitPinsStateAfterGateOwnerLeaves) {
 StageGate::Permit held;
 { StageGate gate(1);auto result=gate.acquire();ASSERT_TRUE(result.isOk());held=std::move(result.value()); }
 held=StageGate::Permit{};
}
TEST(TransferStagesTest, ExpiredDeadlineRejectsEvenWhenCapacityIsFree) {
 StageGate gate(1);
 EXPECT_FALSE(gate.acquire(nullptr,std::chrono::steady_clock::now()-1ms).isOk());
 EXPECT_TRUE(gate.acquire().isOk());
}
TEST(TransferStagesTest, ExplicitPerLaneModeDoesNotWait) {
 StageGate gate(0);auto first=gate.acquire();double wait=0;
 auto next=gate.acquire(&wait,std::chrono::steady_clock::now()+100ms);
 EXPECT_TRUE(first.isOk());EXPECT_TRUE(next.isOk());EXPECT_EQ(wait,0);
}

TEST(TransferStagesTest, NaturalLaneBoundElidesRedundantAdmission) {
 TransferStages stages(4,1,1);
 EXPECT_FALSE(stages.readAdmissionRequired());EXPECT_FALSE(stages.writeAdmissionRequired());
 TransferStages constrained(1,2,4);
 EXPECT_TRUE(constrained.readAdmissionRequired());EXPECT_TRUE(constrained.writeAdmissionRequired());
}

TEST(TransferStagesTest, ContendedReadyOperationsRespectCapacityAndAllMakeProgress) {
 StageGate gate(2);std::atomic<int> active{0},peak{0},completed{0};
 std::vector<std::future<bool>> workers;
 const auto deadline=std::chrono::steady_clock::now()+2s;
 for(int i=0;i<4;++i)workers.push_back(std::async(std::launch::async,[&]{
  for(int j=0;j<100;++j){auto permit=gate.acquire(nullptr,deadline);if(!permit.isOk())return false;
   auto n=++active;auto old=peak.load();while(old<n && !peak.compare_exchange_weak(old,n)){}
   std::this_thread::sleep_for(10us);--active;++completed;
  }return true;
 }));
 for(auto& worker:workers)EXPECT_TRUE(worker.get());
 EXPECT_LE(peak.load(),2);EXPECT_EQ(completed.load(),400);
}
TEST(TransferStagesTest, TimedOutFrontDoesNotStrandAlreadyWaitingSuccessor) {
 StageGate gate(1);auto held=gate.acquire();ASSERT_TRUE(held.isOk());
 auto front=std::async(std::launch::async,[&]{return gate.acquire(nullptr,std::chrono::steady_clock::now()+50ms);});
 std::this_thread::sleep_for(10ms);
 auto next=std::async(std::launch::async,[&]{return gate.acquire(nullptr,std::chrono::steady_clock::now()+1s);});
 EXPECT_FALSE(front.get().isOk());held=StageGate::Permit{};
 EXPECT_TRUE(next.get().isOk());
}

TEST(ReadyIoExecutorTest, BoundsConcurrencyAndWaitsForBorrowToEnd) {
 ReadyIoExecutor pool(1,2);std::promise<void> entered,release;auto unlock=release.get_future().share();
 std::atomic<int> completed{0};double waited=0;
 auto first=std::async(std::launch::async,[&]{return pool.run([&]{entered.set_value();unlock.wait();++completed;return cpnetflux::common::Status::ok();});});
 entered.get_future().wait();
 auto next=std::async(std::launch::async,[&]{return pool.run([&]{++completed;return cpnetflux::common::Status::ok();},&waited);});
 EXPECT_EQ(next.wait_for(30ms),std::future_status::timeout);EXPECT_EQ(completed.load(),0);
 release.set_value();EXPECT_TRUE(first.get().isOk());EXPECT_TRUE(next.get().isOk());
 EXPECT_EQ(completed.load(),2);EXPECT_GT(waited,0.01);
}
TEST(ReadyIoExecutorTest, RejectsOutstandingOverflowAndReleasesBeforeCompletion) {
 ReadyIoExecutor pool(1,1);std::promise<void> entered,release;auto unlock=release.get_future().share();
 auto pending=std::async(std::launch::async,[&]{return pool.run([&]{entered.set_value();unlock.wait();return cpnetflux::common::Status::ok();});});entered.get_future().wait();
 EXPECT_FALSE(pool.run([]{return cpnetflux::common::Status::ok();}).isOk());
 release.set_value();ASSERT_TRUE(pending.get().isOk());
 for(int i=0;i<100;++i)EXPECT_TRUE(pool.run([]{return cpnetflux::common::Status::ok();}).isOk());
}
TEST(ReadyIoExecutorTest, ExpiredQueuedJobDoesNotAccessBorrowAndSuccessorRuns) {
 ReadyIoExecutor pool(1,2);std::promise<void> entered,release;auto unlock=release.get_future().share();std::atomic<bool> touched{false};
 auto first=std::async(std::launch::async,[&]{return pool.run([&]{entered.set_value();unlock.wait();return cpnetflux::common::Status::ok();});});entered.get_future().wait();
 auto queued=std::async(std::launch::async,[&]{return pool.run([&]{touched=true;return cpnetflux::common::Status::ok();},nullptr,std::chrono::steady_clock::now()+20ms);});
 std::this_thread::sleep_for(40ms);release.set_value();EXPECT_TRUE(first.get().isOk());EXPECT_FALSE(queued.get().isOk());EXPECT_FALSE(touched.load());
 EXPECT_TRUE(pool.run([]{return cpnetflux::common::Status::ok();}).isOk());
}
TEST(ReadyIoExecutorTest, ExceptionCompletesPromiseAndNextOperationRuns) {
 ReadyIoExecutor pool(1,2);
 EXPECT_FALSE(pool.run([]()->cpnetflux::common::Status{throw 1;}).isOk());
 EXPECT_TRUE(pool.run([]{return cpnetflux::common::Status::ok();}).isOk());
}
TEST(ReadyIoExecutorTest, BorrowedCaptureIsDestroyedBeforeRunReturns) {
 ReadyIoExecutor pool(1,2);auto owner=std::make_shared<int>(1);std::weak_ptr<int> weak=owner;
 std::function<cpnetflux::common::Status()> callback=[owner=std::move(owner)]{return cpnetflux::common::Status::ok();};
 EXPECT_TRUE(pool.run(std::move(callback)).isOk());EXPECT_TRUE(weak.expired());
}
TEST(ReadyIoExecutorTest, InvalidWorkerOrCapacityBoundsAreRejected) {
 EXPECT_THROW(ReadyIoExecutor(0,1),std::invalid_argument);
 EXPECT_THROW(ReadyIoExecutor(2,1),std::invalid_argument);
 EXPECT_THROW(ReadyIoExecutor(1,65),std::invalid_argument);
}
TEST(ReadyIoExecutorTest, SelectionElidesPoolAtNaturalLaneBound) {
 setenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY","1",1);setenv("CPNETFLUX_STAGE_WRITE_EXECUTOR","pool",1);
 auto source=TransferStages::create(4,2);auto shared=TransferStages::create(4,2,true);auto natural=TransferStages::create(1,2,true);
 ASSERT_TRUE(source.isOk());EXPECT_FALSE(source.value()->writerPool);ASSERT_TRUE(shared.isOk());ASSERT_TRUE(natural.isOk());EXPECT_TRUE(shared.value()->writerPool);EXPECT_FALSE(natural.value()->writerPool);
 setenv("CPNETFLUX_STAGE_WRITE_EXECUTOR","admission",1);auto admission=TransferStages::create(4,2,true);ASSERT_TRUE(admission.isOk());EXPECT_FALSE(admission.value()->writerPool);
 unsetenv("CPNETFLUX_STAGE_WRITE_EXECUTOR");unsetenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY");
}
