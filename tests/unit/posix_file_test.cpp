#include "gridflux/storage/posix_file.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

}  // namespace

TEST(PosixFileTest, ReadsAndWritesAtOffsets) {
    const std::filesystem::path path = tempPath("gridflux-posix-file-test.bin");
    std::filesystem::remove(path);

    auto writeResult = gridflux::storage::PosixFile::openWriteTruncate(path.string());
    ASSERT_TRUE(writeResult.isOk()) << writeResult.status().message();
    gridflux::storage::PosixFile output = std::move(writeResult.value());

    const std::vector<std::uint8_t> first{'a', 'b', 'c'};
    const std::vector<std::uint8_t> second{'x', 'y', 'z'};

    EXPECT_TRUE(output.writeAtAll(0, first.data(), first.size()).isOk());
    EXPECT_TRUE(output.writeAtAll(6, second.data(), second.size()).isOk());
    EXPECT_TRUE(output.resize(9).isOk());

    output = gridflux::storage::PosixFile();

    auto readResult = gridflux::storage::PosixFile::openReadOnly(path.string());
    ASSERT_TRUE(readResult.isOk()) << readResult.status().message();
    gridflux::storage::PosixFile input = std::move(readResult.value());

    auto size = input.fileSize();
    ASSERT_TRUE(size.isOk()) << size.status().message();
    EXPECT_EQ(size.value(), 9U);

    std::vector<std::uint8_t> data(9);
    EXPECT_TRUE(input.readAtAll(0, data.data(), data.size()).isOk());
    EXPECT_EQ(data[0], 'a');
    EXPECT_EQ(data[1], 'b');
    EXPECT_EQ(data[2], 'c');
    EXPECT_EQ(data[6], 'x');
    EXPECT_EQ(data[7], 'y');
    EXPECT_EQ(data[8], 'z');

    std::filesystem::remove(path);
}

TEST(PosixFileTest, ReportsUnexpectedEof) {
    const std::filesystem::path path = tempPath("gridflux-posix-file-eof-test.bin");
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << "abc";
    }

    auto readResult = gridflux::storage::PosixFile::openReadOnly(path.string());
    ASSERT_TRUE(readResult.isOk()) << readResult.status().message();

    std::vector<std::uint8_t> data(4);
    EXPECT_FALSE(readResult.value().readAtAll(0, data.data(), data.size()).isOk());

    std::filesystem::remove(path);
}

TEST(PosixFileTest, SupportsExclusiveCreateRenameAndRemove) {
    const std::filesystem::path path = tempPath("gridflux-posix-file-exclusive-test.bin");
    const std::filesystem::path renamed = tempPath("gridflux-posix-file-renamed-test.bin");
    std::filesystem::remove(path);
    std::filesystem::remove(renamed);

    auto exists = gridflux::storage::PosixFile::pathExists(path.string());
    ASSERT_TRUE(exists.isOk()) << exists.status().message();
    EXPECT_FALSE(exists.value());

    {
        auto fileResult = gridflux::storage::PosixFile::openWriteExclusive(path.string());
        ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
        gridflux::storage::PosixFile file = std::move(fileResult.value());
        const std::vector<std::uint8_t> data{'o', 'k'};
        EXPECT_TRUE(file.writeAtAll(0, data.data(), data.size()).isOk());

        auto duplicate = gridflux::storage::PosixFile::openWriteExclusive(path.string());
        EXPECT_FALSE(duplicate.isOk());
    }

    EXPECT_TRUE(gridflux::storage::PosixFile::renamePath(path.string(), renamed.string()).isOk());

    exists = gridflux::storage::PosixFile::pathExists(renamed.string());
    ASSERT_TRUE(exists.isOk()) << exists.status().message();
    EXPECT_TRUE(exists.value());

    EXPECT_TRUE(gridflux::storage::PosixFile::removePath(renamed.string()).isOk());
    exists = gridflux::storage::PosixFile::pathExists(renamed.string());
    ASSERT_TRUE(exists.isOk()) << exists.status().message();
    EXPECT_FALSE(exists.value());
}

TEST(PosixFileTest, OpensExistingFileForWriteOnlyResume) {
    const std::filesystem::path path = tempPath("gridflux-posix-file-writeonly-test.bin");
    std::filesystem::remove(path);

    {
        auto fileResult = gridflux::storage::PosixFile::openWriteTruncate(path.string());
        ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
        const std::vector<std::uint8_t> data{'a', 'b', 'c'};
        EXPECT_TRUE(fileResult.value().writeAtAll(0, data.data(), data.size()).isOk());
    }

    auto writeOnly = gridflux::storage::PosixFile::openWriteOnly(path.string());
    ASSERT_TRUE(writeOnly.isOk()) << writeOnly.status().message();
    const std::vector<std::uint8_t> patch{'z'};
    EXPECT_TRUE(writeOnly.value().writeAtAll(1, patch.data(), patch.size()).isOk());

    std::filesystem::remove(path);
}

TEST(PosixFileTest, OpensExistingFileForReadWriteResume) {
    const std::filesystem::path path = tempPath("gridflux-posix-file-readwrite-test.bin");
    std::filesystem::remove(path);

    {
        auto fileResult = gridflux::storage::PosixFile::openReadWriteExclusive(path.string());
        ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
        const std::vector<std::uint8_t> data{'a', 'b', 'c'};
        EXPECT_TRUE(fileResult.value().writeAtAll(0, data.data(), data.size()).isOk());
    }

    auto readWrite = gridflux::storage::PosixFile::openReadWrite(path.string());
    ASSERT_TRUE(readWrite.isOk()) << readWrite.status().message();

    const std::vector<std::uint8_t> patch{'z'};
    EXPECT_TRUE(readWrite.value().writeAtAll(1, patch.data(), patch.size()).isOk());

    std::vector<std::uint8_t> data(3);
    EXPECT_TRUE(readWrite.value().readAtAll(0, data.data(), data.size()).isOk());
    EXPECT_EQ(data[0], 'a');
    EXPECT_EQ(data[1], 'z');
    EXPECT_EQ(data[2], 'c');

    auto duplicate = gridflux::storage::PosixFile::openReadWriteExclusive(path.string());
    EXPECT_FALSE(duplicate.isOk());

    std::filesystem::remove(path);
}

TEST(PosixFileTest, PreallocatesFileSpace) {
    const std::filesystem::path path = tempPath("gridflux-posix-file-preallocate-test.bin");
    std::filesystem::remove(path);

    auto fileResult = gridflux::storage::PosixFile::openWriteTruncate(path.string());
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    gridflux::storage::PosixFile file = std::move(fileResult.value());

    ASSERT_TRUE(file.preallocate(4096).isOk());
    auto size = file.fileSize();
    ASSERT_TRUE(size.isOk()) << size.status().message();
    EXPECT_GE(size.value(), 4096U);

    std::filesystem::remove(path);
}
