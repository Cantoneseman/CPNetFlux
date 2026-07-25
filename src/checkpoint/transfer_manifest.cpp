#include "gridflux/checkpoint/transfer_manifest.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "gridflux/checksum/crc32c.h"

namespace gridflux::checkpoint {
namespace {

constexpr std::size_t kMaxTransferIdLength = 128;

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
        const std::uint8_t high = fromHex(text[index]);
        const std::uint8_t low = fromHex(text[index + 1]);
        decoded.push_back(static_cast<char>((high << 4U) | low));
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
        return common::Status::invalidArgument(std::string("manifest missing key: ") + key);
    }
    return common::Status::ok();
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
            return common::Status::invalidArgument("manifest range missing dash");
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
            return common::Status::invalidArgument("manifest range end must exceed begin");
        }
        const common::Status mergeStatus =
            merged.merge(parsedBegin.value(), parsedEnd.value() - parsedBegin.value(), totalSize);
        if (!mergeStatus.isOk()) {
            return mergeStatus;
        }
        begin = comma == std::string::npos ? text.size() : comma + 1;
    }

    ranges = merged.ranges();
    return ranges;
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
        if (!chunkId.isOk()) {
            return chunkId.status();
        }
        auto offset = parseU64(parts[1], "chunk offset");
        if (!offset.isOk()) {
            return offset.status();
        }
        auto length = parseU64(parts[2], "chunk length");
        if (!length.isOk()) {
            return length.status();
        }
        auto algorithm = checksum::parseChecksumAlgorithm(parts[3]);
        if (!algorithm.isOk()) {
            return algorithm.status();
        }
        if (algorithm.value() != manifestAlgorithm) {
            return common::Status::invalidArgument("verified chunk checksum algorithm mismatch");
        }
        auto value = parseHex32(parts[4], "checksum");
        if (!value.isOk()) {
            return value.status();
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

std::string buildManifestBody(const TransferManifest& manifest,
                              const std::vector<core::chunk::CompletedRange>& ranges) {
    std::ostringstream output;
    output << "manifest_version=" << manifest.version << '\n';
    output << "transfer_id=" << manifest.transferId << '\n';
    output << "output_path_hex=" << toHex(manifest.outputPath) << '\n';
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

}  // namespace

bool isValidTransferId(const std::string& transferId) noexcept {
    if (transferId.empty() || transferId.size() > kMaxTransferIdLength) {
        return false;
    }
    return std::all_of(transferId.begin(), transferId.end(), [](char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
               (value >= '0' && value <= '9') || value == '.' || value == '_' || value == '-';
    });
}

std::string manifestPathForOutput(const std::string& outputPath) {
    return outputPath + ".gridflux.manifest";
}

std::string tempPathForOutput(const std::string& outputPath, const std::string& transferId) {
    return outputPath + ".part." + transferId;
}

std::uint64_t nowUnixNanos() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

const char* manifestStateName(ManifestState state) noexcept {
    switch (state) {
        case ManifestState::Created:
            return "created";
        case ManifestState::Transferring:
            return "transferring";
        case ManifestState::Failed:
            return "failed";
        case ManifestState::Committed:
            return "committed";
    }
    return "failed";
}

common::Result<ManifestState> parseManifestState(const std::string& text) {
    if (text == "created") {
        return ManifestState::Created;
    }
    if (text == "transferring") {
        return ManifestState::Transferring;
    }
    if (text == "failed") {
        return ManifestState::Failed;
    }
    if (text == "committed") {
        return ManifestState::Committed;
    }
    return common::Status::invalidArgument("invalid manifest state");
}

common::Result<std::string> serializeTransferManifest(const TransferManifest& manifest) {
    if (manifest.version != kTransferManifestVersion) {
        return common::Status::invalidArgument("serializer only writes manifest version 2");
    }
    if (!isValidTransferId(manifest.transferId)) {
        return common::Status::invalidArgument("invalid transfer_id");
    }
    if (manifest.outputPath.empty() || manifest.tempPath.empty()) {
        return common::Status::invalidArgument("manifest paths must not be empty");
    }
    if (manifest.chunkSize == 0) {
        return common::Status::invalidArgument("manifest chunk_size must be greater than zero");
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

common::Result<TransferManifest> parseTransferManifest(const std::string& text) {
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
            return common::Status::invalidArgument("manifest line missing equals");
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key.empty() || values.find(key) != values.end()) {
            return common::Status::invalidArgument("manifest contains duplicate or empty key");
        }
        values.emplace(key, value);
        if (key == "manifest_body_crc32c") {
            expectedBodyChecksum = value;
        } else {
            bodyText += line;
            bodyText.push_back('\n');
        }
    }

    const common::Status hasVersion = requireKey(values, "manifest_version");
    if (!hasVersion.isOk()) {
        return hasVersion;
    }
    auto version = parseU64(values["manifest_version"], "manifest_version");
    if (!version.isOk()) {
        return version.status();
    }
    if (version.value() != kTransferManifestVersionV1 &&
        version.value() != kTransferManifestVersion) {
        return common::Status::invalidArgument("unsupported manifest version");
    }

    const bool isV2 = version.value() == kTransferManifestVersion;
    for (const char* key :
         {"transfer_id", "output_path_hex", "temp_path_hex", "total_size", "chunk_size",
          "created_at_unix_ns", "updated_at_unix_ns", "state", "completed_ranges"}) {
        const common::Status status = requireKey(values, key);
        if (!status.isOk()) {
            return status;
        }
    }
    if (isV2) {
        for (const char* key : {"checksum_algorithm", "verified_chunks", "manifest_body_crc32c"}) {
            const common::Status status = requireKey(values, key);
            if (!status.isOk()) {
                return status;
            }
        }
        auto parsedExpected = parseHex32(expectedBodyChecksum, "manifest_body_crc32c");
        if (!parsedExpected.isOk()) {
            return parsedExpected.status();
        }
        const std::uint32_t actual = checksum::crc32c(
            reinterpret_cast<const std::uint8_t*>(bodyText.data()), bodyText.size());
        if (actual != parsedExpected.value()) {
            return common::Status::invalidArgument("manifest body checksum mismatch");
        }
    }

    TransferManifest manifest;
    manifest.version = static_cast<std::uint32_t>(version.value());
    manifest.transferId = values["transfer_id"];
    if (!isValidTransferId(manifest.transferId)) {
        return common::Status::invalidArgument("invalid transfer_id");
    }

    auto outputPath = fromHexString(values["output_path_hex"]);
    if (!outputPath.isOk()) {
        return outputPath.status();
    }
    auto tempPath = fromHexString(values["temp_path_hex"]);
    if (!tempPath.isOk()) {
        return tempPath.status();
    }
    manifest.outputPath = std::move(outputPath.value());
    manifest.tempPath = std::move(tempPath.value());
    if (manifest.outputPath.empty() || manifest.tempPath.empty()) {
        return common::Status::invalidArgument("manifest paths must not be empty");
    }

    auto totalSize = parseU64(values["total_size"], "total_size");
    if (!totalSize.isOk()) {
        return totalSize.status();
    }
    auto chunkSize = parseU64(values["chunk_size"], "chunk_size");
    if (!chunkSize.isOk()) {
        return chunkSize.status();
    }
    auto createdAt = parseU64(values["created_at_unix_ns"], "created_at_unix_ns");
    if (!createdAt.isOk()) {
        return createdAt.status();
    }
    auto updatedAt = parseU64(values["updated_at_unix_ns"], "updated_at_unix_ns");
    if (!updatedAt.isOk()) {
        return updatedAt.status();
    }
    auto state = parseManifestState(values["state"]);
    if (!state.isOk()) {
        return state.status();
    }
    if (chunkSize.value() == 0) {
        return common::Status::invalidArgument("manifest chunk_size must be greater than zero");
    }

    manifest.totalSize = totalSize.value();
    manifest.chunkSize = chunkSize.value();
    manifest.createdAtUnixNanos = createdAt.value();
    manifest.updatedAtUnixNanos = updatedAt.value();
    manifest.state = state.value();

    auto ranges = parseRanges(values["completed_ranges"], manifest.totalSize);
    if (!ranges.isOk()) {
        return ranges.status();
    }
    manifest.completedRanges = std::move(ranges.value());

    if (!isV2) {
        manifest.checksumAlgorithm = checksum::ChecksumAlgorithm::None;
        manifest.verifiedChunks.clear();
        return manifest;
    }

    auto algorithm = checksum::parseChecksumAlgorithm(values["checksum_algorithm"]);
    if (!algorithm.isOk()) {
        return algorithm.status();
    }
    manifest.checksumAlgorithm = algorithm.value();

    auto verified = parseVerifiedChunks(values["verified_chunks"], manifest.totalSize,
                                        manifest.checksumAlgorithm);
    if (!verified.isOk()) {
        return verified.status();
    }
    manifest.verifiedChunks = std::move(verified.value());

    auto derived = completedFromVerified(manifest.verifiedChunks, manifest.totalSize);
    if (!derived.isOk()) {
        return derived.status();
    }
    if (derived.value().ranges().size() != manifest.completedRanges.size()) {
        return common::Status::invalidArgument("completed_ranges do not match verified chunks");
    }
    for (std::size_t index = 0; index < manifest.completedRanges.size(); ++index) {
        if (manifest.completedRanges[index].begin != derived.value().ranges()[index].begin ||
            manifest.completedRanges[index].end != derived.value().ranges()[index].end) {
            return common::Status::invalidArgument("completed_ranges do not match verified chunks");
        }
    }

    return manifest;
}

}  // namespace gridflux::checkpoint
