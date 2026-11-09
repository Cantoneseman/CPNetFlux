#include "cpnetflux/storage/mapped_file_window.h"
#include "cpnetflux/storage/posix_file.h"

#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <limits>
#include <vector>
#include <unistd.h>

namespace {
struct WindowFile {
    std::string path;
    cpnetflux::storage::PosixFile file;
    WindowFile() {
        char name[] = "/tmp/cpnetflux-mapped-window-XXXXXX";
        const int fd = ::mkstemp(name);
        if (fd < 0) throw std::runtime_error("mkstemp");
        path = name;
        file = cpnetflux::storage::PosixFile(cpnetflux::core::io::UniqueFd(fd));
    }
    ~WindowFile() { std::error_code error; std::filesystem::remove(path, error); }
};
using cpnetflux::storage::MappedFileWindow;
}

TEST(MappedFileWindowTest, CrossesWindowBoundaryAndClosesBeforeDescriptor) {
    WindowFile temporary;
    constexpr auto bound = MappedFileWindow::kWindowBytes;
    ASSERT_TRUE(temporary.file.resize(bound + 9).isOk());
    MappedFileWindow window(temporary.file.fd(), bound + 9);
    std::array<std::uint8_t, 7> bytes{1, 2, 3, 4, 5, 6, 7}, actual{};
    ASSERT_TRUE(window.write(bound - 3, bytes).isOk());
    ASSERT_TRUE(temporary.file.readAtAll(bound - 3, actual.data(), actual.size()).isOk());
    EXPECT_EQ(actual, bytes);
    EXPECT_EQ(window.stats().mapCalls, 2U);
    EXPECT_EQ(window.stats().unmapCalls, 1U);
    EXPECT_EQ(window.stats().activeMappedBytes, 9U);
    EXPECT_EQ(window.stats().peakMappedBytes, bound);
    EXPECT_EQ(window.stats().writtenBytes, bytes.size());
    ASSERT_TRUE(window.close().isOk());
    EXPECT_EQ(window.stats().activeMappedBytes, 0U);
    EXPECT_EQ(window.stats().unmapCalls, 2U);
    ASSERT_TRUE(window.close().isOk());
    EXPECT_EQ(window.stats().unmapCalls, 2U);
}
TEST(MappedFileWindowTest, BorrowIsTentativeBoundedAndRetiresThePreviousWindow) {
    WindowFile temporary;
    constexpr auto bound = MappedFileWindow::kWindowBytes;
    ASSERT_TRUE(temporary.file.resize(bound + 9).isOk());
    MappedFileWindow window(temporary.file.fd(), bound + 9);
    auto first = window.writableSpan(bound - 3, 7);
    ASSERT_TRUE(first.isOk());
    ASSERT_EQ(first.value().size(), 3U);
    std::fill(first.value().begin(), first.value().end(), 37);
    auto second = window.writableSpan(bound, 4);
    ASSERT_TRUE(second.isOk());
    ASSERT_EQ(second.value().size(), 4U);
    std::fill(second.value().begin(), second.value().end(), 71);
    EXPECT_EQ(window.stats().mapCalls, 2U);
    EXPECT_EQ(window.stats().unmapCalls, 1U);
    EXPECT_EQ(window.stats().writtenBytes, 0U);
    EXPECT_EQ(window.stats().writeCalls, 0U);
    ASSERT_TRUE(window.close().isOk());
    std::array<std::uint8_t, 7> actual{};
    ASSERT_TRUE(temporary.file.readAtAll(bound-3, actual.data(), actual.size()).isOk());
    EXPECT_EQ(actual, (std::array<std::uint8_t, 7>{37,37,37,71,71,71,71}));
    EXPECT_EQ(window.stats().activeMappedBytes, 0U);
}

TEST(MappedFileWindowTest, BorrowRejectsZeroOverflowAndMapFailureWithoutCertifyingBytes) {
    MappedFileWindow window(-1, 8);
    EXPECT_FALSE(window.writableSpan(0, 0).isOk());
    EXPECT_FALSE(window.writableSpan(8, 1).isOk());
    EXPECT_FALSE(window.writableSpan(0, 9).isOk());
    EXPECT_FALSE(window.writableSpan(std::numeric_limits<std::uint64_t>::max(), 1).isOk());
    EXPECT_EQ(window.stats().mapCalls, 0U);
    EXPECT_FALSE(window.writableSpan(0, 8).isOk());
    EXPECT_EQ(window.stats().mapFailures, 1U);
    EXPECT_EQ(window.stats().writtenBytes + window.stats().activeMappedBytes, 0U);
}
TEST(MappedFileWindowTest, RejectsOverflowAndOutOfBoundsWithoutMapping) {
    WindowFile temporary;
    ASSERT_TRUE(temporary.file.resize(8).isOk());
    MappedFileWindow window(temporary.file.fd(), 8);
    const std::array<std::uint8_t, 2> bytes{5, 6};
    EXPECT_FALSE(window.write(7, bytes).isOk());
    EXPECT_FALSE(window.write(std::numeric_limits<std::uint64_t>::max(), bytes).isOk());
    EXPECT_TRUE(window.write(8, {}).isOk());
    EXPECT_EQ(window.stats().mapCalls, 0U);
    EXPECT_EQ(window.stats().writtenBytes, 0U);
}
TEST(MappedFileWindowTest, MapFailureDoesNotCertifyBytesOrRetainMapping) {
    MappedFileWindow window(-1, 8);
    const std::array<std::uint8_t, 2> bytes{5, 6};
    const auto status = window.write(0, bytes);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), cpnetflux::common::StatusCode::SystemError);
    EXPECT_EQ(window.stats().mapCalls, 1U);
    EXPECT_EQ(window.stats().mapFailures, 1U);
    EXPECT_EQ(window.stats().writtenBytes, 0U);
    EXPECT_EQ(window.stats().activeMappedBytes, 0U);
    EXPECT_TRUE(window.close().isOk());
    EXPECT_EQ(window.stats().unmapCalls, 0U);
}

TEST(MappedFileWindowTest, ProfileDisabledLeavesEveryProfileCounterZero) {
    WindowFile temporary;
    ASSERT_TRUE(temporary.file.resize(65536).isOk());
    MappedFileWindow window(temporary.file.fd(), 65536);
    const std::array<std::uint8_t, 2> bytes{8, 9};
    ASSERT_TRUE(window.write(0, bytes).isOk());
    ASSERT_TRUE(window.close().isOk());
    const auto& p = window.stats().profile;
    EXPECT_FALSE(p.enabled);
    EXPECT_EQ(p.copyCalls + p.copyBytes + p.errors + p.copyMinorFaults + p.copyMajorFaults, 0U);
    EXPECT_EQ(p.mapCpuSeconds + p.mapWallSeconds + p.unmapCpuSeconds + p.unmapWallSeconds +
              p.copyCpuSeconds + p.copyWallSeconds, 0.0);
}

TEST(MappedFileWindowTest, ProfileColdAndWarmCopyOnSameLiveMappingPreservesContent) {
    WindowFile temporary;
    constexpr std::size_t length = 1024 * 1024;
    ASSERT_TRUE(temporary.file.resize(length).isOk());
    MappedFileWindow window(temporary.file.fd(), length, true);
    std::vector<std::uint8_t> bytes(length, 53), actual(length);
    ASSERT_TRUE(window.write(0, bytes).isOk());
    const auto cold = window.stats().profile;
    EXPECT_GT(cold.copyMinorFaults, 0U);
    EXPECT_EQ(cold.copyMajorFaults, 0U);
    std::fill(bytes.begin(), bytes.end(), 91);
    ASSERT_TRUE(window.write(0, bytes).isOk());
    ASSERT_TRUE(temporary.file.readAtAll(0, actual.data(), actual.size()).isOk());
    EXPECT_EQ(actual, bytes);
    EXPECT_EQ(window.stats().mapCalls, 1U);
    ASSERT_TRUE(window.close().isOk());
    const auto& p = window.stats().profile;
    EXPECT_TRUE(p.enabled);
    EXPECT_EQ(p.errors, 0U);
    EXPECT_EQ(p.copyCalls, 2U);
    EXPECT_EQ(p.copyBytes, 2 * length);
    EXPECT_GE(p.copyMinorFaults, cold.copyMinorFaults);
    EXPECT_EQ(p.copyMajorFaults, 0U);
    EXPECT_GT(p.copyCpuSeconds, cold.copyCpuSeconds);
    EXPECT_GT(p.mapCpuSeconds, 0);
    EXPECT_GT(p.unmapCpuSeconds, 0);
    EXPECT_GT(p.copyWallSeconds, 0);
}

TEST(MappedFileWindowTest, ProfileCountsSplitCopiesAndRejectsInvalidWrites) {
    WindowFile temporary;
    constexpr auto bound = MappedFileWindow::kWindowBytes;
    ASSERT_TRUE(temporary.file.resize(bound + 9).isOk());
    MappedFileWindow window(temporary.file.fd(), bound + 9, true);
    const std::array<std::uint8_t, 7> bytes{1, 2, 3, 4, 5, 6, 7};
    EXPECT_FALSE(window.write(bound + 8, bytes).isOk());
    ASSERT_TRUE(window.write(bound + 9, {}).isOk());
    EXPECT_EQ(window.stats().profile.copyCalls, 0U);
    ASSERT_TRUE(window.write(bound - 3, bytes).isOk());
    ASSERT_TRUE(window.close().isOk());
    const auto before = window.stats().profile;
    ASSERT_TRUE(window.close().isOk());
    EXPECT_EQ(window.stats().profile.unmapCpuSeconds, before.unmapCpuSeconds);
    EXPECT_EQ(before.copyCalls, 2U);
    EXPECT_EQ(before.copyBytes, 7U);
    EXPECT_EQ(before.errors, 0U);
    EXPECT_EQ(window.stats().mapCalls, 2U);
    EXPECT_EQ(window.stats().unmapCalls, 2U);
}

TEST(MappedFileWindowTest, ProfileMapErrorDoesNotInventCopyOrUnmap) {
    MappedFileWindow window(-1, 8, true);
    const std::array<std::uint8_t, 2> bytes{1, 2};
    ASSERT_FALSE(window.write(0, bytes).isOk());
    ASSERT_TRUE(window.close().isOk());
    EXPECT_EQ(window.stats().mapFailures, 1U);
    const auto& p = window.stats().profile;
    EXPECT_GT(p.mapCpuSeconds, 0);
    EXPECT_EQ(p.copyCalls + p.copyBytes + p.errors + p.copyMinorFaults + p.copyMajorFaults, 0U);
    EXPECT_EQ(p.copyCpuSeconds + p.unmapCpuSeconds, 0);
}

#include "../../tools/perf/mapped_window_collapse.h"
#include <cstring>
using cpnetflux::diagnostic::pmdBytes;
using cpnetflux::diagnostic::prepareCollapse;

TEST(MappedCollapseDiagnosticTest, PreservesExistingBytesAndOnlyPreparesOwnedInterior) {
    WindowFile temporary;
    constexpr std::size_t size = 8 * 1024 * 1024;
    ASSERT_TRUE(temporary.file.resize(size).isOk());
    std::vector<std::uint8_t> expected(size), actual(size);
    for (std::size_t i=0;i<size;++i) expected[i]=1+(i*31+i/4096)%251;
    ASSERT_TRUE(temporary.file.writeAtAll(0,expected.data(),size).isOk());
    MappedFileWindow writer(temporary.file.fd(),size);
    auto borrowed=writer.writableSpan(0,size); ASSERT_TRUE(borrowed.isOk());
    // Partial borrowed subrange: first/last pages and all alignment edges are guards.
    auto owned=borrowed.value().subspan(4096,size-8192);
    unsigned calls=0;
    auto report=prepareCollapse(owned,[&](void* p,std::size_t n,int kind) {
        const auto start=reinterpret_cast<std::uintptr_t>(p);
        EXPECT_EQ(start%pmdBytes,0U); EXPECT_EQ(n%pmdBytes,0U);
        EXPECT_GE(start,reinterpret_cast<std::uintptr_t>(owned.data()));
        EXPECT_LE(start+n,reinterpret_cast<std::uintptr_t>(owned.data())+owned.size());
        EXPECT_EQ(kind,++calls==1?MADV_HUGEPAGE:cpnetflux::diagnostic::collapseAdvice);
        return 0;
    });
    EXPECT_EQ(calls,2U); EXPECT_GE(report.eligibleBytes,2*pmdBytes);
    EXPECT_EQ(report.eligibleBytes+report.edgeBytes,owned.size());
    EXPECT_EQ(report.seeds,report.eligibleBytes/pmdBytes);
    EXPECT_EQ(writer.stats().writeCalls+writer.stats().writtenBytes,0U);
    ASSERT_TRUE(writer.close().isOk()); EXPECT_EQ(writer.stats().activeMappedBytes,0U);
    ASSERT_TRUE(temporary.file.readAtAll(0,actual.data(),size).isOk()); EXPECT_EQ(actual,expected);
}
TEST(MappedCollapseDiagnosticTest, CancellationAndAdviceFailuresDoNotCertifyAndCanClose) {
    for (int scenario=0;scenario<3;++scenario) {
        WindowFile temporary; constexpr std::size_t size=8*1024*1024;
        ASSERT_TRUE(temporary.file.resize(size).isOk());
        MappedFileWindow writer(temporary.file.fd(),size);
        auto view=writer.writableSpan(0,size); ASSERT_TRUE(view.isOk());
        unsigned calls=0,checks=0;
        EXPECT_THROW(prepareCollapse(view.value(),[&](void*,std::size_t,int) {
            ++calls; if (calls==static_cast<unsigned>(scenario)) {errno=EIO;return -1;} return 0;
        },[&] {return scenario==0 && ++checks==3;}),std::runtime_error);
        EXPECT_EQ(calls,scenario==2?2U:1U);
        EXPECT_EQ(writer.stats().writeCalls+writer.stats().writtenBytes,0U);
        ASSERT_TRUE(writer.close().isOk()); EXPECT_EQ(writer.stats().activeMappedBytes,0U);
        EXPECT_EQ(writer.stats().mapCalls,writer.stats().unmapCalls);
    }
}
TEST(MappedCollapseDiagnosticTest, RejectsEmptySmallOversizedAndOverflowSpansBeforeSyscalls) {
    unsigned calls=0;auto advice=[&](void*,std::size_t,int){++calls;return 0;};
    std::uint8_t value=0;
    EXPECT_THROW(prepareCollapse({},advice),std::invalid_argument);
    EXPECT_THROW(prepareCollapse({&value,1},advice),std::invalid_argument);
    EXPECT_THROW(prepareCollapse({&value,65ULL*1024*1024},advice),std::invalid_argument);
    EXPECT_THROW(prepareCollapse({reinterpret_cast<std::uint8_t*>(
        std::numeric_limits<std::uintptr_t>::max()-4096),pmdBytes},advice),std::invalid_argument);
    EXPECT_EQ(calls,0U);
}
TEST(MappedCollapseDiagnosticTest, ExplicitAlignmentStaysBoundedAcrossWindows) {
    WindowFile temporary;const auto size=MappedFileWindow::kWindowBytes+8*1024*1024;
    ASSERT_TRUE(temporary.file.resize(size).isOk());
    MappedFileWindow writer(temporary.file.fd(),size,false,
        cpnetflux::storage::MappedWindowAlignment::HugePage2MiB);
    for (const std::uint64_t offset:std::array<std::uint64_t,2>{0,MappedFileWindow::kWindowBytes}) {
        auto view=writer.writableSpan(offset,1); ASSERT_TRUE(view.isOk());
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(view.value().data())%pmdBytes,0U);
        EXPECT_LE(writer.stats().activeMappedBytes,MappedFileWindow::kWindowBytes);
    }
    ASSERT_TRUE(writer.close().isOk()); EXPECT_EQ(writer.stats().activeMappedBytes,0U);
    EXPECT_EQ(writer.stats().mapCalls,writer.stats().unmapCalls);
    EXPECT_EQ(writer.stats().writeCalls+writer.stats().writtenBytes,0U);
}
