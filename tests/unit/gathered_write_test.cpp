#include "cpnetflux/storage/file_io.h"
#include <gtest/gtest.h>
#include <sys/uio.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace {
struct TempFile {
    std::filesystem::path path;
    cpnetflux::storage::PosixFile file;
    TempFile() {
        char pattern[]="/tmp/cpnetflux-gather-XXXXXX";
        const int fd=::mkstemp(pattern);
        if(fd<0) throw std::runtime_error("mkstemp");
        path=pattern;
        file=cpnetflux::storage::PosixFile(cpnetflux::core::io::UniqueFd(fd));
    }
    ~TempFile(){std::error_code ec;std::filesystem::remove(path,ec);}
};
struct Faults {
    std::vector<int> results;
    std::size_t next=0;
    std::vector<std::uint64_t> offsets;
    std::vector<const void*> firstPointers;
};
thread_local Faults* active=nullptr;
struct Inject { explicit Inject(Faults& f){active=&f;} ~Inject(){active=nullptr;} };
}
extern "C" ssize_t __real_pwritev(int,const struct iovec*,int,off_t);
extern "C" ssize_t __wrap_pwritev(int fd,const struct iovec* vectors,int count,off_t offset) {
    if(!active) return __real_pwritev(fd,vectors,count,offset);
    active->offsets.push_back(offset);
    active->firstPointers.push_back(count ? vectors[0].iov_base : nullptr);
    if(active->next>=active->results.size()) return __real_pwritev(fd,vectors,count,offset);
    const int result=active->results[active->next++];
    if(result<0){errno=-result;return -1;}
    if(!result)return 0;
    std::array<iovec,2> shortened{};
    std::size_t remaining=result;
    int n=0;
    for(int i=0;i<count && remaining;++i){
        shortened[n]=vectors[i];
        shortened[n].iov_len=std::min(remaining,vectors[i].iov_len);
        remaining-=shortened[n++].iov_len;
    }
    return __real_pwritev(fd,shortened.data(),n,offset);
}
using namespace cpnetflux::storage;
TEST(GatheredWriteTest, WritesBorrowedSlicesAtOffsetWithoutChangingInputs) {
    TempFile t;ASSERT_TRUE(t.file.resize(16).isOk());
    const std::array<std::uint8_t,4> a{'A','B','C','D'};
    const std::array<std::uint8_t,3> b{'x','y','z'};
    const std::array<FileWriteSlice,2> slices{{{a.data(),a.size()},{b.data(),b.size()}}};
    FileIoStats stats;FileWriteObservation observation;Faults faults;Inject inject(faults);
    auto status=writeGatheredAtAll(t.file,5,slices,&stats,&observation);
    ASSERT_TRUE(status.isOk())<<status.message();
    std::array<std::uint8_t,16> bytes{};ASSERT_TRUE(t.file.readAtAll(0,bytes.data(),bytes.size()).isOk());
    EXPECT_EQ(std::string(bytes.begin()+5,bytes.begin()+12),"ABCDxyz");
    EXPECT_EQ(bytes[4],0);EXPECT_EQ(bytes[12],0);
    ASSERT_EQ(faults.firstPointers.size(),1U);EXPECT_EQ(faults.firstPointers[0],a.data());
    EXPECT_EQ(slices[0].data,a.data());EXPECT_EQ(slices[0].size,4U);
    EXPECT_EQ(stats.posixWriteSyscallCount(),1U);EXPECT_EQ(stats.posixWriteSyscallBytes(),7U);
    EXPECT_EQ(observation.writeAtAllCalls,1U);EXPECT_EQ(observation.writeAtAllBytes,7U);
    EXPECT_EQ(observation.writeAtAllFailures,0U);
}
TEST(GatheredWriteTest, RetriesInterruptedAndShortWritesAcrossSliceBoundary) {
    TempFile t;const std::array<std::uint8_t,8> bytes{'A','B','C','D','E','F','G','H'};
    const std::array<FileWriteSlice,2> slices{{{bytes.data(),4},{bytes.data()+4,4}}};
    Faults faults{{-EINTR,2,3,-EAGAIN}};Inject inject(faults);FileIoStats stats;
    const auto status=writeGatheredAtAll(t.file,3,slices,&stats);
    ASSERT_TRUE(status.isOk())<<status.message();
    std::array<std::uint8_t,8> actual{};ASSERT_TRUE(t.file.readAtAll(3,actual.data(),8).isOk());
    EXPECT_EQ(actual,bytes);
    EXPECT_EQ(faults.offsets,(std::vector<std::uint64_t>{3,3,5,8,8}));
    ASSERT_EQ(faults.firstPointers.size(),5U);
    EXPECT_EQ(faults.firstPointers[2],bytes.data()+2);
    EXPECT_EQ(faults.firstPointers[3],bytes.data()+5);
    EXPECT_EQ(stats.posixWriteRetryCount(),2U);EXPECT_EQ(stats.posixWriteShortCount(),2U);
    EXPECT_EQ(stats.posixWriteSyscallCount(),3U);EXPECT_EQ(stats.posixWriteSyscallBytes(),8U);
    EXPECT_EQ(slices[0].size,4U);EXPECT_EQ(slices[1].data,bytes.data()+4);
}
TEST(GatheredWriteTest, PartialFailureAndZeroAreErrorsWithoutInventingCompletion) {
    for(const auto results:{std::vector<int>{2,-EIO},std::vector<int>{0}}){
        TempFile t;const std::array<std::uint8_t,4> bytes{'A','B','C','D'};
        const std::array<FileWriteSlice,2> slices{{{bytes.data(),2},{bytes.data()+2,2}}};
        Faults faults{results};Inject inject(faults);FileIoStats stats;FileWriteObservation observation;
        const auto status=writeGatheredAtAll(t.file,0,slices,&stats,&observation);
        EXPECT_FALSE(status.isOk());EXPECT_EQ(observation.writeAtAllFailures,1U);
        const auto size=t.file.fileSize();ASSERT_TRUE(size.isOk());
        EXPECT_EQ(size.value(),results.front()==2 ? 2U : 0U);
        EXPECT_EQ(stats.posixWriteSyscallBytes(),results.front()==2 ? 2U : 0U);
        if(results.front()==2){
            std::array<std::uint8_t,2> actual{};ASSERT_TRUE(t.file.readAtAll(0,actual.data(),2).isOk());
            EXPECT_EQ(actual[0],'A');EXPECT_EQ(actual[1],'B');
        }else EXPECT_EQ(stats.posixWriteZeroCount(),1U);
    }
}
TEST(GatheredWriteTest, RejectsInvalidSlicesAndOverflowBeforeAnySyscall) {
    TempFile t;const std::uint8_t byte=7;Faults faults;Inject inject(faults);
    const std::array<FileWriteSlice,2> overflow{{{&byte,1},{&byte,1}}};
    EXPECT_FALSE(writeGatheredAtAll(t.file,std::numeric_limits<off_t>::max(),overflow,nullptr).isOk());
    const std::array<FileWriteSlice,1> null{{{nullptr,1}}};
    EXPECT_FALSE(writeGatheredAtAll(t.file,0,null,nullptr).isOk());
    const std::array<FileWriteSlice,3> tooMany{{{&byte,1},{&byte,1},{&byte,1}}};
    EXPECT_FALSE(writeGatheredAtAll(t.file,0,tooMany,nullptr).isOk());
    EXPECT_TRUE(faults.offsets.empty());EXPECT_EQ(t.file.fileSize().value(),0U);
}
TEST(GatheredWriteTest, EmptySlicesDoNotWriteOrDereferenceNull) {
    TempFile t;const std::array<FileWriteSlice,2> empty{};
    FileIoStats stats;FileWriteObservation observation;
    EXPECT_TRUE(writeGatheredAtAll(t.file,0,empty,&stats,&observation).isOk());
    EXPECT_EQ(stats.writeCalls(),0U);EXPECT_EQ(observation.writeAtAllCalls,0U);
    const std::uint8_t byte=42;
    const std::array<FileWriteSlice,2> mixed{{{nullptr,0},{&byte,1}}};
    EXPECT_TRUE(writeGatheredAtAll(t.file,0,mixed,&stats).isOk());
    std::uint8_t actual=0;ASSERT_TRUE(t.file.readAtAll(0,&actual,1).isOk());EXPECT_EQ(actual,byte);
}
