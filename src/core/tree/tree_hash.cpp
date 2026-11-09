#include "cpnetflux/core/tree/tree_hash.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

common::Result<TreeHashPathMode> parseTreeHashPathMode(std::string_view value) {
    if (value == "original") return TreeHashPathMode::Original;
    if (value == "cached") return TreeHashPathMode::Cached;
    return common::Status::invalidArgument("CPNETFLUX_SUMMARY_PATH must be original or cached");
}

const char* treeHashPathModeName(TreeHashPathMode mode) noexcept {
    switch (mode) {
        case TreeHashPathMode::Original: return "original";
        case TreeHashPathMode::Cached: return "cached";
    }
    return "unknown";
}

common::Result<std::string> treeHashFromFiles(
    const std::string& root, TreeHashPathMode mode, checksum::ChecksumBackend backend,
    checksum::ChecksumBackend* actualBackend) {
    if (mode != TreeHashPathMode::Original && mode != TreeHashPathMode::Cached)
        return common::Status::invalidArgument("invalid tree hash path mode");
    std::error_code error;
    const std::filesystem::path rootPath(root);
    if (!std::filesystem::exists(rootPath, error) || error ||
        !std::filesystem::is_directory(rootPath, error) || error)
        return common::Status::invalidArgument("tree hash root is not a directory");
    struct Entry {
        std::filesystem::path path;
        std::string relative;
    };
    std::vector<Entry> files;
    for (std::filesystem::recursive_directory_iterator iterator(
             rootPath, std::filesystem::directory_options::none, error);
         iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(error)) {
        if (error)
            return common::Status::systemError("tree hash scan failed: " + error.message(), error.value());
        const auto& entry = *iterator;
        if (entry.is_symlink(error))
            return common::Status::invalidArgument("tree hash rejects symlink");
        if (error)
            return common::Status::systemError("tree hash entry failed: " + error.message(), error.value());
        if (entry.is_regular_file(error)) {
            const auto relative = std::filesystem::relative(entry.path(), rootPath, error);
            if (error)
                return common::Status::systemError("tree hash relative failed: " + error.message(), error.value());
            const std::string name = relative.generic_string();
            if (name.find(".cpnetflux.") != std::string::npos || name.find(".part.") != std::string::npos)
                continue;
            files.push_back({entry.path(), mode == TreeHashPathMode::Cached ? name : std::string{}});
        } else if (!entry.is_directory(error)) {
            return common::Status::invalidArgument("tree hash rejects non-regular file");
        }
    }
    if (error)
        return common::Status::systemError("tree hash scan failed: " + error.message(), error.value());
    if (mode == TreeHashPathMode::Cached) {
        // Relative names were resolved and checked once during enumeration.
        std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
            return left.relative < right.relative;
        });
    } else {
        std::sort(files.begin(), files.end(), [&](const auto& left, const auto& right) {
            return std::filesystem::relative(left.path, rootPath).generic_string() <
                   std::filesystem::relative(right.path, rootPath).generic_string();
        });
    }
    checksum::ChecksumComputer computer(checksum::ChecksumAlgorithm::Crc32c, backend);
    if (actualBackend) *actualBackend = computer.backend();
    std::vector<std::uint8_t> buffer(1024 * 1024);
    for (const auto& entry : files) {
        std::string relativeText;
        if (mode == TreeHashPathMode::Cached) {
            relativeText = entry.relative;
        } else {
            const auto relative = std::filesystem::relative(entry.path, rootPath, error);
            if (error)
                return common::Status::systemError("tree hash relative failed: " + error.message(), error.value());
            relativeText = relative.generic_string();
        }
        const auto size = std::filesystem::file_size(entry.path, error);
        if (error)
            return common::Status::systemError("tree hash file size failed: " + error.message(), error.value());
        const std::string sizeText = std::to_string(size);
        computer.update(reinterpret_cast<const std::uint8_t*>(relativeText.data()), relativeText.size());
        const std::uint8_t zero = 0;
        computer.update(&zero, 1);
        computer.update(reinterpret_cast<const std::uint8_t*>(sizeText.data()), sizeText.size());
        computer.update(&zero, 1);
        std::ifstream input(entry.path, std::ios::binary);
        if (!input)
            return common::Status::runtimeError("tree hash failed to open file: " + entry.path.string());
        while (input) {
            input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto read = input.gcount();
            if (read > 0) computer.update(buffer.data(), static_cast<std::size_t>(read));
        }
        if (!input.eof())
            return common::Status::runtimeError("tree hash read failed: " + entry.path.string());
        computer.update(&zero, 1);
    }
    return "crc32c:" + hex32(computer.finalize().value);
}

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
