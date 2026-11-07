#include "cpnetflux/core/io/transfer_stages.h"
#include <gtest/gtest.h>
#include "cpnetflux/core/io/dynamic_file_queue.h"
#include <future>
#include <atomic>
#include <cstdlib>
using namespace cpnetflux::core::io;
using namespace std::chrono_literals;
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
