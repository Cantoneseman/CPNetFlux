#include "cpnetflux/checkpoint/download_manifest.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "cpnetflux/checksum/crc32c.h"
#include "cpnetflux/storage/posix_file.h"

namespace cpnetflux::checkpoint {
namespace {

bool isHexDigit(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

std::uint8_t fromHex(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    return static_cast<std::uint8_t>(value - 'A' + 10);
}

std::string toHex(const std::string& text) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(text.size() * 2);
    for (unsigned char value : text) {
        encoded.push_back(kDigits[(value >> 4U) & 0x0FU]);
        encoded.push_back(kDigits[value & 0x0FU]);
    }
    return encoded;
}

std::string hex32(std::uint32_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string encoded(8, '0');
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        const std::size_t shift = 28U - index * 4U;
        encoded[index] = kDigits[(value >> shift) & 0x0FU];
    }
    return encoded;
}

common::Result<std::uint32_t> parseHex32(const std::string& text, const char* name) {
    if (text.size() != 8) {
        return common::Status::invalidArgument(std::string(name) + " must be 8 hex digits");
    }
    std::uint32_t value = 0;
    for (char digit : text) {
        if (!isHexDigit(digit)) {
            return common::Status::invalidArgument(std::string(name) + " contains invalid hex");
        }
        value = (value << 4U) | fromHex(digit);
    }
    return value;
}

common::Result<std::string> fromHexString(const std::string& text) {
    if (text.size() % 2 != 0) {
        return common::Status::invalidArgument("hex string must have even length");
    }

    std::string decoded;
    decoded.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        if (!isHexDigit(text[index]) || !isHexDigit(text[index + 1])) {
            return common::Status::invalidArgument("hex string contains invalid digit");
        }
        decoded.push_back(
            static_cast<char>((fromHex(text[index]) << 4U) | fromHex(text[index + 1])));
    }
    return decoded;
}

common::Result<std::uint64_t> parseU64(const std::string& text, const char* name) {
    if (text.empty()) {
        return common::Status::invalidArgument(std::string(name) + " must not be empty");
    }
    std::uint64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc() || result.ptr != end) {
        return common::Status::invalidArgument(std::string(name) + " must be a decimal integer");
    }
    return value;
}

common::Status requireKey(const std::unordered_map<std::string, std::string>& values,
                          const char* key) {
    if (values.find(key) == values.end()) {
        return common::Status::invalidArgument(std::string("download manifest missing key: ") +
                                               key);
    }
    return common::Status::ok();
}

std::vector<ChunkChecksumRecord> sortedRecords(const std::vector<ChunkChecksumRecord>& records) {
    std::vector<ChunkChecksumRecord> sorted = records;
    std::sort(sorted.begin(), sorted.end(),
              [](const ChunkChecksumRecord& left, const ChunkChecksumRecord& right) {
                  if (left.offset != right.offset) {
                      return left.offset < right.offset;
                  }
                  return left.chunkId < right.chunkId;
              });
    return sorted;
}

std::string serializeRanges(const std::vector<core::chunk::CompletedRange>& ranges) {
    std::ostringstream output;
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << ranges[index].begin << '-' << ranges[index].end;
    }
    return output.str();
}

std::string serializeVerifiedChunks(const std::vector<ChunkChecksumRecord>& records) {
    std::ostringstream output;
    const std::vector<ChunkChecksumRecord> sorted = sortedRecords(records);
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << sorted[index].chunkId << ':' << sorted[index].offset << ':'
               << sorted[index].length << ':'
               << checksum::checksumAlgorithmName(sorted[index].checksum.algorithm) << ':'
               << hex32(sorted[index].checksum.value);
    }
    return output.str();
}

common::Result<std::vector<core::chunk::CompletedRange>> parseRanges(const std::string& text,
                                                                     std::uint64_t totalSize) {
    std::vector<core::chunk::CompletedRange> ranges;
    core::chunk::RangeList merged;
    if (text.empty()) {
        return ranges;
    }

    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;
        const std::string item = text.substr(begin, end - begin);
        const std::size_t dash = item.find('-');
        if (dash == std::string::npos) {
            return common::Status::invalidArgument("download manifest range missing dash");
        }
        auto parsedBegin = parseU64(item.substr(0, dash), "range begin");
        if (!parsedBegin.isOk()) {
            return parsedBegin.status();
        }
        auto parsedEnd = parseU64(item.substr(dash + 1), "range end");
        if (!parsedEnd.isOk()) {
            return parsedEnd.status();
        }
        if (parsedEnd.value() <= parsedBegin.value()) {
            return common::Status::invalidArgument("download manifest range end must exceed begin");
        }
        const common::Status mergeStatus =
            merged.merge(parsedBegin.value(), parsedEnd.value() - parsedBegin.value(), totalSize);
        if (!mergeStatus.isOk()) {
            return mergeStatus;
        }
        begin = comma == std::string::npos ? text.size() : comma + 1;
    }
    return merged.ranges();
}

common::Result<std::vector<ChunkChecksumRecord>> parseVerifiedChunks(
    const std::string& text, std::uint64_t totalSize,
    checksum::ChecksumAlgorithm manifestAlgorithm) {
    std::vector<ChunkChecksumRecord> records;
    core::chunk::RangeList completed;
    if (text.empty()) {
        return records;
    }

    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;
        const std::string item = text.substr(begin, end - begin);
        std::array<std::string, 5> parts{};
        std::size_t partBegin = 0;
        for (std::size_t part = 0; part < parts.size(); ++part) {
            const std::size_t separator = item.find(':', partBegin);
            if (part + 1 == parts.size()) {
                parts[part] = item.substr(partBegin);
                if (separator != std::string::npos) {
                    return common::Status::invalidArgument("verified chunk has too many fields");
                }
            } else {
                if (separator == std::string::npos) {
                    return common::Status::invalidArgument("verified chunk missing field");
                }
                parts[part] = item.substr(partBegin, separator - partBegin);
                partBegin = separator + 1;
            }
        }

        auto chunkId = parseU64(parts[0], "chunk_id");
        auto offset = parseU64(parts[1], "chunk offset");
        auto length = parseU64(parts[2], "chunk length");
        auto algorithm = checksum::parseChecksumAlgorithm(parts[3]);
        auto value = parseHex32(parts[4], "checksum");
        if (!chunkId.isOk()) {
            return chunkId.status();
        }
        if (!offset.isOk()) {
            return offset.status();
        }
        if (!length.isOk()) {
            return length.status();
        }
        if (!algorithm.isOk()) {
            return algorithm.status();
        }
        if (!value.isOk()) {
            return value.status();
        }
        if (algorithm.value() != manifestAlgorithm) {
            return common::Status::invalidArgument("verified chunk checksum algorithm mismatch");
        }
        const common::Status insertStatus =
            completed.insert(offset.value(), length.value(), totalSize);
        if (!insertStatus.isOk()) {
            return insertStatus;
        }
        records.push_back(
            ChunkChecksumRecord{chunkId.value(), offset.value(), length.value(),
                                checksum::ChecksumValue{algorithm.value(), value.value()}});
        begin = comma == std::string::npos ? text.size() : comma + 1;
    }
    return sortedRecords(records);
}

common::Result<core::chunk::RangeList> completedFromVerified(
    const std::vector<ChunkChecksumRecord>& records, std::uint64_t totalSize) {
    core::chunk::RangeList completed;
    for (const ChunkChecksumRecord& record : sortedRecords(records)) {
        const common::Status insertStatus =
            completed.insert(record.offset, record.length, totalSize);
        if (!insertStatus.isOk()) {
            return insertStatus;
        }
    }
    return completed;
}

std::string buildManifestBody(const DownloadManifest& manifest,
                              const std::vector<core::chunk::CompletedRange>& ranges) {
    std::ostringstream output;
    output << "manifest_version=" << manifest.version << '\n';
    output << "transfer_id=" << manifest.transferId << '\n';
    output << "source_path_hex=" << toHex(manifest.sourcePath) << '\n';
    output << "target_path_hex=" << toHex(manifest.targetPath) << '\n';
    output << "temp_path_hex=" << toHex(manifest.tempPath) << '\n';
    output << "total_size=" << manifest.totalSize << '\n';
    output << "chunk_size=" << manifest.chunkSize << '\n';
    output << "checksum_algorithm=" << checksum::checksumAlgorithmName(manifest.checksumAlgorithm)
           << '\n';
    output << "created_at_unix_ns=" << manifest.createdAtUnixNanos << '\n';
    output << "updated_at_unix_ns=" << manifest.updatedAtUnixNanos << '\n';
    output << "state=" << manifestStateName(manifest.state) << '\n';
    output << "completed_ranges=" << serializeRanges(ranges) << '\n';
    output << "verified_chunks=" << serializeVerifiedChunks(manifest.verifiedChunks) << '\n';
    return output.str();
}

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

common::Status writeAll(int fd, const char* data, std::size_t length) {
    std::size_t completed = 0;
    while (completed < length) {
        const ssize_t written = ::write(fd, data + completed, length - completed);
        if (written > 0) {
            completed += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0) {
            return systemStatus("write download manifest", errno);
        }
        return common::Status::runtimeError("write download manifest returned zero bytes");
    }
    return common::Status::ok();
}

}  // namespace

std::string downloadManifestPathForOutput(const std::string& outputPath) {
    return outputPath + ".cpnetflux.download.manifest";
}

std::string downloadTempPathForOutput(const std::string& outputPath,
                                      const std::string& transferId) {
    return outputPath + ".part." + transferId;
}

common::Result<std::string> serializeDownloadManifest(const DownloadManifest& manifest) {
    if (manifest.version != kDownloadManifestVersion) {
        return common::Status::invalidArgument("unsupported download manifest version");
    }
    if (!isValidTransferId(manifest.transferId)) {
        return common::Status::invalidArgument("invalid transfer_id");
    }
    if (manifest.sourcePath.empty() || manifest.targetPath.empty() || manifest.tempPath.empty()) {
        return common::Status::invalidArgument("download manifest paths must not be empty");
    }
    if (manifest.chunkSize == 0) {
        return common::Status::invalidArgument(
            "download manifest chunk_size must be greater than zero");
    }

    auto completed = completedFromVerified(manifest.verifiedChunks, manifest.totalSize);
    if (!completed.isOk()) {
        return completed.status();
    }
    const std::string body = buildManifestBody(manifest, completed.value().ranges());
    const std::uint32_t bodyChecksum =
        checksum::crc32c(reinterpret_cast<const std::uint8_t*>(body.data()), body.size());
    return body + "manifest_body_crc32c=" + hex32(bodyChecksum) + '\n';
}

common::Result<DownloadManifest> parseDownloadManifest(const std::string& text) {
    std::unordered_map<std::string, std::string> values;
    std::istringstream input(text);
    std::string line;
    std::string bodyText;
    std::string expectedBodyChecksum;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            return common::Status::invalidArgument("download manifest line missing equals");
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key.empty() || values.find(key) != values.end()) {
            return common::Status::invalidArgument(
                "download manifest contains duplicate or empty key");
        }
        values.emplace(key, value);
        if (key == "manifest_body_crc32c") {
            expectedBodyChecksum = value;
        } else {
            bodyText += line;
            bodyText.push_back('\n');
        }
    }

    for (const char* key : {"manifest_version", "transfer_id", "source_path_hex", "target_path_hex",
                            "temp_path_hex", "total_size", "chunk_size", "checksum_algorithm",
                            "created_at_unix_ns", "updated_at_unix_ns", "state", "completed_ranges",
                            "verified_chunks", "manifest_body_crc32c"}) {
        const common::Status status = requireKey(values, key);
        if (!status.isOk()) {
            return status;
        }
    }
    auto version = parseU64(values["manifest_version"], "manifest_version");
    if (!version.isOk()) {
        return version.status();
    }
    if (version.value() != kDownloadManifestVersion) {
        return common::Status::invalidArgument("unsupported download manifest version");
    }
    auto parsedExpected = parseHex32(expectedBodyChecksum, "manifest_body_crc32c");
    if (!parsedExpected.isOk()) {
        return parsedExpected.status();
    }
    const std::uint32_t actual =
        checksum::crc32c(reinterpret_cast<const std::uint8_t*>(bodyText.data()), bodyText.size());
    if (actual != parsedExpected.value()) {
        return common::Status::invalidArgument("download manifest body checksum mismatch");
    }

    DownloadManifest manifest;
    manifest.version = static_cast<std::uint32_t>(version.value());
    manifest.transferId = values["transfer_id"];
    if (!isValidTransferId(manifest.transferId)) {
        return common::Status::invalidArgument("invalid transfer_id");
    }
    auto sourcePath = fromHexString(values["source_path_hex"]);
    auto targetPath = fromHexString(values["target_path_hex"]);
    auto tempPath = fromHexString(values["temp_path_hex"]);
    if (!sourcePath.isOk()) {
        return sourcePath.status();
    }
    if (!targetPath.isOk()) {
        return targetPath.status();
    }
    if (!tempPath.isOk()) {
        return tempPath.status();
    }
    manifest.sourcePath = std::move(sourcePath.value());
    manifest.targetPath = std::move(targetPath.value());
    manifest.tempPath = std::move(tempPath.value());
    if (manifest.sourcePath.empty() || manifest.targetPath.empty() || manifest.tempPath.empty()) {
        return common::Status::invalidArgument("download manifest paths must not be empty");
    }

    auto totalSize = parseU64(values["total_size"], "total_size");
    auto chunkSize = parseU64(values["chunk_size"], "chunk_size");
    auto createdAt = parseU64(values["created_at_unix_ns"], "created_at_unix_ns");
    auto updatedAt = parseU64(values["updated_at_unix_ns"], "updated_at_unix_ns");
    auto state = parseManifestState(values["state"]);
    auto algorithm = checksum::parseChecksumAlgorithm(values["checksum_algorithm"]);
    if (!totalSize.isOk()) {
        return totalSize.status();
    }
    if (!chunkSize.isOk()) {
        return chunkSize.status();
    }
    if (!createdAt.isOk()) {
        return createdAt.status();
    }
    if (!updatedAt.isOk()) {
        return updatedAt.status();
    }
    if (!state.isOk()) {
        return state.status();
    }
    if (!algorithm.isOk()) {
        return algorithm.status();
    }
    if (chunkSize.value() == 0) {
        return common::Status::invalidArgument(
            "download manifest chunk_size must be greater than zero");
    }

    manifest.totalSize = totalSize.value();
    manifest.chunkSize = chunkSize.value();
    manifest.createdAtUnixNanos = createdAt.value();
    manifest.updatedAtUnixNanos = updatedAt.value();
    manifest.state = state.value();
    manifest.checksumAlgorithm = algorithm.value();

    auto ranges = parseRanges(values["completed_ranges"], manifest.totalSize);
    auto verified = parseVerifiedChunks(values["verified_chunks"], manifest.totalSize,
                                        manifest.checksumAlgorithm);
    if (!ranges.isOk()) {
        return ranges.status();
    }
    if (!verified.isOk()) {
        return verified.status();
    }
    manifest.completedRanges = std::move(ranges.value());
    manifest.verifiedChunks = std::move(verified.value());

    auto derived = completedFromVerified(manifest.verifiedChunks, manifest.totalSize);
    if (!derived.isOk()) {
        return derived.status();
    }
    if (derived.value().ranges().size() != manifest.completedRanges.size()) {
        return common::Status::invalidArgument(
            "download completed_ranges do not match verified chunks");
    }
    for (std::size_t index = 0; index < manifest.completedRanges.size(); ++index) {
        if (manifest.completedRanges[index].begin != derived.value().ranges()[index].begin ||
            manifest.completedRanges[index].end != derived.value().ranges()[index].end) {
            return common::Status::invalidArgument(
                "download completed_ranges do not match verified chunks");
        }
    }
    return manifest;
}

common::Status saveDownloadManifestAtomic(const std::string& path,
                                          const DownloadManifest& manifest) {
    auto serialized = serializeDownloadManifest(manifest);
    if (!serialized.isOk()) {
        return serialized.status();
    }

    const std::string tempPath = path + ".tmp." + std::to_string(::getpid());
    const int fd = ::open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return systemStatus("open download manifest temp", errno);
    }

    const common::Status writeStatus =
        writeAll(fd, serialized.value().data(), serialized.value().size());
    if (!writeStatus.isOk()) {
        (void)::close(fd);
        (void)storage::PosixFile::removePath(tempPath);
        return writeStatus;
    }
    if (::close(fd) != 0) {
        const int closeError = errno;
        (void)storage::PosixFile::removePath(tempPath);
        return systemStatus("close download manifest temp", closeError);
    }
    const common::Status renameStatus = storage::PosixFile::renamePath(tempPath, path);
    if (!renameStatus.isOk()) {
        (void)storage::PosixFile::removePath(tempPath);
        return renameStatus;
    }
    return common::Status::ok();
}

common::Result<DownloadManifest> loadDownloadManifest(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return common::Status::runtimeError("open download manifest failed: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return common::Status::runtimeError("read download manifest failed: " + path);
    }
    return parseDownloadManifest(buffer.str());
}

}  // namespace cpnetflux::checkpoint
