#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/core/tree/tree_hash.h"

namespace {
using cpnetflux::checksum::ChecksumAlgorithm;
using cpnetflux::checksum::ChecksumBackend;
using cpnetflux::checksum::ChecksumComputer;
using cpnetflux::core::tree::TreeHashChunkCrc;
using cpnetflux::core::tree::TreeHashFileCrc;
using cpnetflux::core::tree::treeHashFromVerifiedChunks;

std::uint32_t crc(const std::vector<std::uint8_t>& bytes) {
    ChecksumComputer computer(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
    computer.update(bytes.data(), bytes.size());
    return computer.finalize().value;
}

TreeHashFileCrc makeFile(const std::string& path, const std::vector<std::uint8_t>& data,
                         std::size_t pieceSize) {
    TreeHashFileCrc file;
    file.relativePath = path;
    file.size = data.size();
    for (std::size_t offset = 0; offset < data.size();) {
        const auto length = std::min(pieceSize, data.size() - offset);
        ChecksumComputer part(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
        part.update(data.data() + offset, length);
        file.chunks.push_back(TreeHashChunkCrc{offset, length, part.finalize().value});
        offset += length;
    }
    return file;
}

std::string independentReference(const std::vector<std::pair<std::string,std::vector<std::uint8_t>>>& files) {
    ChecksumComputer tree(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
    auto ordered=files;
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    const std::uint8_t zero=0;
    for(const auto& [path,bytes]:ordered) {
        tree.update(reinterpret_cast<const std::uint8_t*>(path.data()),path.size());
        tree.update(&zero,1);
        const auto size=std::to_string(bytes.size());
        tree.update(reinterpret_cast<const std::uint8_t*>(size.data()),size.size());
        tree.update(&zero,1);
        tree.update(bytes.data(),bytes.size());
        tree.update(&zero,1);
    }
    constexpr char digits[]="0123456789abcdef";
    const auto value=tree.finalize().value;
    std::string result="crc32c:00000000";
    for(int i=0;i<8;++i)result[14-i]=digits[(value>>(i*4))&0xfU];
    return result;
}
} // namespace

TEST(TreeHashTest, VerifiedChunksReproduceTheExactTreeHash) {
    std::vector<std::uint8_t> first(65536 + 13), second(4U * 1024U * 1024U + 9);
    for (std::size_t i=0;i<first.size();++i) first[i]=static_cast<std::uint8_t>((i*7U+3U)%251U);
    for (std::size_t i=0;i<second.size();++i) second[i]=static_cast<std::uint8_t>((i*17U+11U)%251U);
    std::vector<TreeHashFileCrc> files;
    files.push_back(makeFile("资料/空.bin",{},65536));
    files.push_back(makeFile("nested/a.bin",first,65536));
    files.push_back(makeFile("z.bin",second,1024U*1024U));
    auto actual=treeHashFromVerifiedChunks(files,ChecksumBackend::Software);
    ASSERT_TRUE(actual.isOk())<<actual.status().message();
    EXPECT_EQ(actual.value(),independentReference({{"资料/空.bin",{}},{"nested/a.bin",first},{"z.bin",second}}));
}

TEST(TreeHashTest, EmptyTreeHasTheSameEmptyCrc) {
    auto actual=treeHashFromVerifiedChunks({},ChecksumBackend::Software);
    ASSERT_TRUE(actual.isOk())<<actual.status().message();
    EXPECT_EQ(actual.value(),"crc32c:00000000");
}

TEST(TreeHashTest, MissingOverlappingOrConflictingChunksAreRejected) {
    const std::vector<std::uint8_t> data(32,9);
    auto file=makeFile("x",data,16);
    file.chunks.pop_back();
    EXPECT_FALSE(treeHashFromVerifiedChunks({file},ChecksumBackend::Software).isOk());
    file=makeFile("x",data,16);
    file.chunks.push_back(file.chunks.front());
    EXPECT_FALSE(treeHashFromVerifiedChunks({file},ChecksumBackend::Software).isOk());
    file.chunks.back().crc32c^=1;
    EXPECT_FALSE(treeHashFromVerifiedChunks({file},ChecksumBackend::Software).isOk());
}

TEST(TreeHashTest, RejectsOutOfBoundsZeroLengthAndDuplicateFiles) {
    const std::vector<std::uint8_t> data(8, 3);
    auto file = makeFile("x", data, 8);
    file.chunks.front().offset = 1;
    EXPECT_FALSE(treeHashFromVerifiedChunks({file}, ChecksumBackend::Software).isOk());
    file = makeFile("x", data, 8);
    file.chunks.front().length = 0;
    EXPECT_FALSE(treeHashFromVerifiedChunks({file}, ChecksumBackend::Software).isOk());
    file = makeFile("x", data, 8);
    EXPECT_FALSE(treeHashFromVerifiedChunks({file, file}, ChecksumBackend::Software).isOk());
}
