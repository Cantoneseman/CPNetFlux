#include "cpnetflux/storage/mapped_file_window.h"
#include "cpnetflux/storage/posix_file.h"

#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <limits>
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
