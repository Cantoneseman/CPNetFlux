#include "cpnetflux/core/tree/tree_manifest.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/checkpoint/manifest_save_stats.h"
#include "cpnetflux/checksum/crc32c.h"
#include "cpnetflux/core/tree/tree_scan.h"

namespace cpnetflux::core::tree {
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

common::Result<std::int64_t> parseI64(const std::string& text, const char* name) {
    if (text.empty()) {
        return common::Status::invalidArgument(std::string(name) + " must not be empty");
    }
    std::int64_t value = 0;
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
        return common::Status::invalidArgument(std::string("tree manifest missing key: ") + key);
    }
    return common::Status::ok();
}

std::string serializeFiles(const std::vector<TreeFileRecord>& files, checkpoint::ManifestSaveStats* stats) {
    checkpoint::OptionalManifestTimer prepare(stats ? &stats->recordPreparationSeconds : nullptr);
    if (stats) {
        ++stats->recordPreparationCalls;
        ++stats->recordSortCalls;
        stats->recordCopyItems += files.size();
    }
    std::vector<TreeFileRecord> sorted = files;
    std::sort(sorted.begin(), sorted.end(),
              [](const TreeFileRecord& left, const TreeFileRecord& right) {
                  return left.relativePath < right.relativePath;
              });
    prepare.stop();
    checkpoint::OptionalManifestTimer format(stats ? &stats->recordFormatSeconds : nullptr);
    std::ostringstream output;
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << toHex(sorted[index].relativePath) << ':' << sorted[index].size << ':'
               << sorted[index].mtimeUnixSeconds << ':' << sorted[index].transferId << ':'
               << treeFileStatusName(sorted[index].status) << ':' << toHex(sorted[index].error);
    }
    return output.str();
}

common::Result<std::vector<TreeFileRecord>> parseFiles(const std::string& text) {
    std::vector<TreeFileRecord> records;
    if (text.empty()) {
        return records;
    }
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;
        const std::string item = text.substr(begin, end - begin);

        std::vector<std::string> parts;
        std::size_t partBegin = 0;
        while (true) {
            const std::size_t separator = item.find(':', partBegin);
            if (separator == std::string::npos) {
                parts.push_back(item.substr(partBegin));
                break;
            }
            parts.push_back(item.substr(partBegin, separator - partBegin));
            partBegin = separator + 1;
        }
        if (parts.size() != 6) {
            return common::Status::invalidArgument("tree file record must have 6 fields");
        }
        auto relative = fromHexString(parts[0]);
        if (!relative.isOk()) {
            return relative.status();
        }
        const common::Status pathStatus = validateTreeRelativePath(relative.value());
        if (!pathStatus.isOk()) {
            return pathStatus;
        }
        auto size = parseU64(parts[1], "tree file size");
        if (!size.isOk()) {
            return size.status();
        }
        auto mtime = parseI64(parts[2], "tree file mtime");
        if (!mtime.isOk()) {
            return mtime.status();
        }
        if (!checkpoint::isValidTransferId(parts[3])) {
            return common::Status::invalidArgument("tree file transfer_id is invalid");
        }
        auto status = parseTreeFileStatus(parts[4]);
        if (!status.isOk()) {
            return status.status();
        }
        auto error = fromHexString(parts[5]);
        if (!error.isOk()) {
            return error.status();
        }
        records.push_back(TreeFileRecord{relative.value(), size.value(), mtime.value(), parts[3],
                                         status.value(), error.value()});
        begin = comma == std::string::npos ? text.size() : comma + 1;
    }
    std::sort(records.begin(), records.end(),
              [](const TreeFileRecord& left, const TreeFileRecord& right) {
                  return left.relativePath < right.relativePath;
              });
    for (std::size_t index = 1; index < records.size(); ++index) {
        if (records[index - 1].relativePath == records[index].relativePath) {
            return common::Status::invalidArgument("tree manifest contains duplicate file");
        }
    }
    return records;
}

std::string bodyWithoutChecksum(const TreeManifest& manifest, checkpoint::ManifestSaveStats* stats) {
    std::ostringstream output;
    output << "manifest_version=" << manifest.version << '\n';
    output << "mode=" << treeTransferModeName(manifest.mode) << '\n';
    output << "root_logical_path_hex=" << toHex(manifest.rootLogicalPath) << '\n';
    output << "checksum_algorithm="
           << checksum::checksumAlgorithmName(manifest.checksumAlgorithm) << '\n';
    output << "created_at_unix_ns=" << manifest.createdAtUnixNanos << '\n';
    output << "updated_at_unix_ns=" << manifest.updatedAtUnixNanos << '\n';
    output << "files=" << serializeFiles(manifest.files, stats) << '\n';
    return output.str();
}

template <class Integer>
void appendDecimal(std::string& out, Integer value) {
    char digits[21];
    const auto converted = std::to_chars(digits, digits + sizeof(digits), value);
    if (converted.ec != std::errc{}) throw std::runtime_error("decimal conversion failed");
    out.append(digits, static_cast<std::size_t>(converted.ptr - digits));
}

void appendHex(std::string& out, std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 15]);
    }
}

std::string bufferedBodyWithoutChecksum(const TreeManifest& m, checkpoint::ManifestSaveStats* stats) {
    checkpoint::OptionalManifestTimer prepare(stats ? &stats->recordPreparationSeconds : nullptr);
    if (stats) ++stats->recordPreparationCalls;
    const auto less = [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; };
    // Equal keys also take the legacy sort path: std::sort is not stable.
    const bool strict = std::adjacent_find(m.files.begin(), m.files.end(),
        [&](const auto& a, const auto& b) { return !less(a, b); }) == m.files.end();
    std::vector<TreeFileRecord> sorted;
    if (!strict) {
        if (stats) { ++stats->recordSortCalls; stats->recordCopyItems += m.files.size(); }
        sorted = m.files; std::sort(sorted.begin(), sorted.end(), less);
    }
    const auto& records = strict ? m.files : sorted;
    prepare.stop();
    std::string out;
    if (records.size() < (out.max_size() - 256) / 128) out.reserve(256 + records.size() * 128);
    out.append("manifest_version="); appendDecimal(out, m.version); out.push_back('\n');
    out.append("mode="); out.append(treeTransferModeName(m.mode)); out.push_back('\n');
    out.append("root_logical_path_hex="); appendHex(out, m.rootLogicalPath); out.push_back('\n');
    out.append("checksum_algorithm="); out.append(checksum::checksumAlgorithmName(m.checksumAlgorithm)); out.push_back('\n');
    out.append("created_at_unix_ns="); appendDecimal(out, m.createdAtUnixNanos); out.push_back('\n');
    out.append("updated_at_unix_ns="); appendDecimal(out, m.updatedAtUnixNanos); out.push_back('\n');
    out.append("files=");
    checkpoint::OptionalManifestTimer format(stats ? &stats->recordFormatSeconds : nullptr);
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (i) out.push_back(',');
        const auto& r = records[i];
        appendHex(out, r.relativePath); out.push_back(':');
        appendDecimal(out, r.size); out.push_back(':');
        appendDecimal(out, r.mtimeUnixSeconds); out.push_back(':');
        out.append(r.transferId); out.push_back(':');
        out.append(treeFileStatusName(r.status)); out.push_back(':'); appendHex(out, r.error);
    }
    out.push_back('\n');
    return out;
}

}  // namespace

std::string treeManifestPathForUpload(const std::string& sourceDir) {
    return sourceDir + ".cpnetflux.tree.upload.manifest";
}

std::string treeManifestPathForDownload(const std::string& destDir) {
    return destDir + ".cpnetflux.tree.download.manifest";
}

const char* treeTransferModeName(TreeTransferMode mode) noexcept {
    switch (mode) {
        case TreeTransferMode::Upload:
            return "upload";
        case TreeTransferMode::Download:
            return "download";
    }
    return "upload";
}

common::Result<TreeTransferMode> parseTreeTransferMode(const std::string& text) {
    if (text == "upload") {
        return TreeTransferMode::Upload;
    }
    if (text == "download") {
        return TreeTransferMode::Download;
    }
    return common::Status::invalidArgument("invalid tree transfer mode");
}

const char* treeFileStatusName(TreeFileStatus status) noexcept {
    switch (status) {
        case TreeFileStatus::Pending:
            return "pending";
        case TreeFileStatus::Transferring:
            return "transferring";
        case TreeFileStatus::Completed:
            return "completed";
        case TreeFileStatus::Failed:
            return "failed";
        case TreeFileStatus::Changed:
            return "changed";
    }
    return "pending";
}

common::Result<TreeFileStatus> parseTreeFileStatus(const std::string& text) {
    if (text == "pending") {
        return TreeFileStatus::Pending;
    }
    if (text == "transferring") {
        return TreeFileStatus::Transferring;
    }
    if (text == "completed") {
        return TreeFileStatus::Completed;
    }
    if (text == "failed") {
        return TreeFileStatus::Failed;
    }
    if (text == "changed") {
        return TreeFileStatus::Changed;
    }
    return common::Status::invalidArgument("invalid tree file status");
}

bool isTreeTransferComplete(const TreeManifest& manifest) {
    return std::all_of(manifest.files.begin(), manifest.files.end(), [](const TreeFileRecord& file) {
        return file.status == TreeFileStatus::Completed;
    });
}

const char* treeManifestSerializerName(TreeManifestSerializer mode) noexcept {
    return mode == TreeManifestSerializer::Buffered ? "buffered" : "original";
}

common::Result<TreeManifestSerializer> parseTreeManifestSerializer(std::string_view text) {
    if (text == "original") return TreeManifestSerializer::Original;
    if (text == "buffered") return TreeManifestSerializer::Buffered;
    return common::Status::invalidArgument("CPNETFLUX_TREE_MANIFEST_SERIALIZER must be original or buffered");
}

common::Result<std::string> serializeTreeManifest(const TreeManifest& manifest,
                                                TreeManifestSerializer serializer,
                                                checkpoint::ManifestSaveStats* stats) {
    if (stats) ++stats->serializeCalls;
    checkpoint::OptionalManifestTimer total(stats ? &stats->serializeSeconds : nullptr);
    if (manifest.version != kTreeManifestSchemaVersion) {
        return common::Status::invalidArgument("unsupported tree manifest version");
    }
    if (manifest.rootLogicalPath.empty()) {
        return common::Status::invalidArgument("tree manifest root path must not be empty");
    }
    for (const TreeFileRecord& file : manifest.files) {
        const common::Status pathStatus = validateTreeRelativePath(file.relativePath);
        if (!pathStatus.isOk()) {
            return pathStatus;
        }
        if (!checkpoint::isValidTransferId(file.transferId)) {
            return common::Status::invalidArgument("tree file transfer_id is invalid");
        }
    }
    std::string body = serializer == TreeManifestSerializer::Buffered
        ? bufferedBodyWithoutChecksum(manifest, stats) : bodyWithoutChecksum(manifest, stats);
    checkpoint::OptionalManifestTimer crcTimer(stats ? &stats->bodyCrcSeconds : nullptr);
    const std::uint32_t crc =
        checksum::crc32c(reinterpret_cast<const std::uint8_t*>(body.data()), body.size());
    crcTimer.stop();
    body += "manifest_body_crc32c=" + hex32(crc) + "\n";
    return body;
}

common::Result<TreeManifest> parseTreeManifest(const std::string& text) {
    std::unordered_map<std::string, std::string> values;
    std::istringstream input(text);
    std::string line;
    std::string body;
    std::string expectedCrc;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            return common::Status::invalidArgument("tree manifest line missing '='");
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "manifest_body_crc32c") {
            expectedCrc = value;
        } else {
            body += line + "\n";
            values.emplace(key, value);
        }
    }
    for (const char* key : {"manifest_version", "mode", "root_logical_path_hex",
                            "checksum_algorithm", "created_at_unix_ns",
                            "updated_at_unix_ns", "files"}) {
        const common::Status status = requireKey(values, key);
        if (!status.isOk()) {
            return status;
        }
    }
    if (expectedCrc.empty()) {
        return common::Status::invalidArgument("tree manifest missing body checksum");
    }
    auto parsedCrc = parseHex32(expectedCrc, "tree manifest body checksum");
    if (!parsedCrc.isOk()) {
        return parsedCrc.status();
    }
    const std::uint32_t actualCrc =
        checksum::crc32c(reinterpret_cast<const std::uint8_t*>(body.data()), body.size());
    if (parsedCrc.value() != actualCrc) {
        return common::Status::invalidArgument("tree manifest body checksum mismatch");
    }

    TreeManifest manifest;
    auto version = parseU64(values["manifest_version"], "tree manifest version");
    if (!version.isOk()) {
        return version.status();
    }
    if (version.value() != kTreeManifestSchemaVersion) {
        return common::Status::invalidArgument("unsupported tree manifest version");
    }
    manifest.version = static_cast<std::uint32_t>(version.value());
    auto mode = parseTreeTransferMode(values["mode"]);
    if (!mode.isOk()) {
        return mode.status();
    }
    manifest.mode = mode.value();
    auto root = fromHexString(values["root_logical_path_hex"]);
    if (!root.isOk()) {
        return root.status();
    }
    manifest.rootLogicalPath = root.value();
    auto algorithm = checksum::parseChecksumAlgorithm(values["checksum_algorithm"]);
    if (!algorithm.isOk()) {
        return algorithm.status();
    }
    manifest.checksumAlgorithm = algorithm.value();
    auto created = parseU64(values["created_at_unix_ns"], "created_at_unix_ns");
    if (!created.isOk()) {
        return created.status();
    }
    auto updated = parseU64(values["updated_at_unix_ns"], "updated_at_unix_ns");
    if (!updated.isOk()) {
        return updated.status();
    }
    manifest.createdAtUnixNanos = created.value();
    manifest.updatedAtUnixNanos = updated.value();
    auto files = parseFiles(values["files"]);
    if (!files.isOk()) {
        return files.status();
    }
    manifest.files = files.value();
    return manifest;
}

common::Status saveTreeManifestAtomic(const std::string& path, const TreeManifest& manifest,
                                      std::uint64_t* serializedBytes,
                                      TreeManifestSerializer serializer,
                                      checkpoint::ManifestSaveStats* stats) {
    checkpoint::OptionalManifestTimer total(stats ? &stats->saveSeconds : nullptr);
    checkpoint::ManifestSaveAttempt attempt(stats);
    auto serialized = serializeTreeManifest(manifest, serializer, stats);
    if (!serialized.isOk()) {
        return serialized.status();
    }
    if (serializedBytes != nullptr) *serializedBytes = serialized.value().size();
    if (stats) stats->serializedBytes += serialized.value().size();
    const std::string tempPath = path + ".tmp." + std::to_string(::getpid());
    {
        checkpoint::OptionalManifestTimer write(stats ? &stats->tempWriteSeconds : nullptr);
        if (stats) ++stats->tempWriteCalls;
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            return common::Status::systemError("open tree manifest temp failed: " +
                                                   std::string(std::strerror(errno)),
                                               errno);
        }
        output << serialized.value();
        output.flush();
        if (!output) {
            return common::Status::systemError("write tree manifest temp failed: " +
                                                   std::string(std::strerror(errno)),
                                               errno);
        }
    }
    checkpoint::OptionalManifestTimer rename(stats ? &stats->renameSeconds : nullptr);
    if (stats) { ++stats->renameCalls; ++stats->publishCalls; }
    if (::rename(tempPath.c_str(), path.c_str()) != 0) {
        const int errorNumber = errno;
        (void)::unlink(tempPath.c_str());
        return common::Status::systemError("rename tree manifest failed: " +
                                               std::string(std::strerror(errorNumber)),
                                           errorNumber);
    }
    attempt.succeed();
    if (stats) ++stats->publishSuccesses;
    return common::Status::ok();
}

common::Result<TreeManifest> loadTreeManifest(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return common::Status::systemError("open tree manifest failed: " +
                                               std::string(std::strerror(errno)),
                                           errno);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return common::Status::systemError("read tree manifest failed: " +
                                               std::string(std::strerror(errno)),
                                           errno);
    }
    return parseTreeManifest(buffer.str());
}

}  // namespace cpnetflux::core::tree
