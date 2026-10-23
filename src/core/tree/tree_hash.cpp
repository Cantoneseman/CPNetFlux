#include "cpnetflux/core/tree/tree_hash.h"

#include <algorithm>
#include <limits>
#include <string_view>

#include "cpnetflux/checksum/crc32c.h"

namespace cpnetflux::core::tree {
namespace {

std::uint32_t crcText(std::string_view text, checksum::ChecksumBackend backend,
                      checksum::ChecksumBackend* actualBackend) {
    checksum::ChecksumComputer computer(checksum::ChecksumAlgorithm::Crc32c, backend);
    if (actualBackend) *actualBackend = computer.backend();
    computer.update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    return computer.finalize().value;
}

std::string hex32(std::uint32_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(8, '0');
    for (int index = 0; index < 8; ++index)
        result[7 - index] = digits[(value >> (index * 4)) & 0xfU];
    return result;
}

}  // namespace

common::Result<std::string> treeHashFromVerifiedChunks(
    std::vector<TreeHashFileCrc> files, checksum::ChecksumBackend prefixBackend,
    checksum::ChecksumBackend* actualBackend) {
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.relativePath < right.relativePath;
    });
    std::uint32_t treeCrc = 0;
    const std::uint8_t zero = 0;
    const std::uint32_t zeroCrc = crcText(std::string_view(reinterpret_cast<const char*>(&zero), 1),
                                          prefixBackend, actualBackend);
    std::string previousPath;
    bool havePrevious = false;
    for (auto& file : files) {
        if (file.relativePath.empty() || file.relativePath.find('\\') != std::string::npos ||
            file.relativePath.find('\0') != std::string::npos)
            return common::Status::invalidArgument("tree hash has an invalid relative path");
        if (havePrevious && file.relativePath == previousPath)
            return common::Status::invalidArgument("tree hash has duplicate relative paths");
        previousPath = file.relativePath;
        havePrevious = true;
        std::sort(file.chunks.begin(), file.chunks.end(), [](const auto& left, const auto& right) {
            return left.offset < right.offset;
        });
        std::uint64_t next = 0;
        std::uint32_t contentCrc = 0;
        for (const auto& chunk : file.chunks) {
            if (chunk.length == 0 || chunk.offset != next || chunk.offset > file.size ||
                chunk.length > file.size - chunk.offset)
                return common::Status::invalidArgument("tree hash chunks do not exactly cover file");
            contentCrc = checksum::crc32cCombine(contentCrc, chunk.crc32c, chunk.length);
            next += chunk.length;
        }
        if (next != file.size)
            return common::Status::invalidArgument("tree hash chunks do not cover the complete file");
        const std::string sizeText = std::to_string(file.size);
        std::uint32_t prefixCrc = crcText(file.relativePath, prefixBackend, actualBackend);
        prefixCrc = checksum::crc32cCombine(prefixCrc, zeroCrc, 1);
        prefixCrc = checksum::crc32cCombine(prefixCrc, crcText(sizeText, prefixBackend, actualBackend), sizeText.size());
        prefixCrc = checksum::crc32cCombine(prefixCrc, zeroCrc, 1);
        treeCrc = checksum::crc32cCombine(treeCrc, prefixCrc,
            static_cast<std::uint64_t>(file.relativePath.size()) + 1 + sizeText.size() + 1);
        treeCrc = checksum::crc32cCombine(treeCrc, contentCrc, file.size);
        treeCrc = checksum::crc32cCombine(treeCrc, zeroCrc, 1);
    }
    return "crc32c:" + hex32(treeCrc);
}

}  // namespace cpnetflux::core::tree
