#pragma once

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

#include "cpnetflux/checkpoint/manifest_save_stats.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"

namespace cpnetflux::checkpoint::detail {

inline bool recordBefore(const ChunkChecksumRecord& left, const ChunkChecksumRecord& right) {
    return left.offset < right.offset ||
           (left.offset == right.offset && left.chunkId < right.chunkId);
}

// Borrow ordered input; keep one private sorted copy only for arbitrary-order callers.
class OrderedManifestRecords {
 public:
    OrderedManifestRecords(const std::vector<ChunkChecksumRecord>& records, ManifestSaveStats* stats)
        : original_(records) {
        OptionalManifestTimer preparation(stats ? &stats->recordPreparationSeconds : nullptr);
        if (stats) ++stats->recordPreparationCalls;
        if (!std::is_sorted(records.begin(), records.end(), recordBefore)) {
            copied_ = true;
            ordered_ = records;
            if (stats) {
                stats->recordCopyItems += records.size();
                ++stats->recordSortCalls;
            }
            std::sort(ordered_.begin(), ordered_.end(), recordBefore);
        }
    }
    const std::vector<ChunkChecksumRecord>& records() const noexcept {
        return copied_ ? ordered_ : original_;
    }
 private:
    const std::vector<ChunkChecksumRecord>& original_;
    std::vector<ChunkChecksumRecord> ordered_;
    bool copied_ = false;
};

inline common::Result<core::chunk::RangeList> completedFromOrderedRecords(
    const std::vector<ChunkChecksumRecord>& records, std::uint64_t totalSize, ManifestSaveStats* stats) {
    OptionalManifestTimer validation(stats ? &stats->rangeValidationSeconds : nullptr);
    core::chunk::RangeList completed;
    for (const auto& record : records) {
        const auto status = completed.insert(record.offset, record.length, totalSize);
        if (!status.isOk()) return status;
    }
    return completed;
}

inline void appendDecimal(std::string& output, std::uint64_t value) {
    char digits[20];
    const auto converted = std::to_chars(digits, digits + sizeof(digits), value);
    output.append(digits, static_cast<std::size_t>(converted.ptr - digits));
}

inline std::string serializeOrderedRecords(const std::vector<ChunkChecksumRecord>& records,
                                          ManifestSaveStats* stats) {
    OptionalManifestTimer formatting(stats ? &stats->recordFormatSeconds : nullptr);
    std::string output;
    // Covers common decimal widths; unusually wide values grow through normal string checks.
    if (records.size() <= output.max_size() / 48) output.reserve(records.size() * 48);
    constexpr char hexDigits[] = "0123456789abcdef";
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (index) output.push_back(',');
        const auto& record = records[index];
        appendDecimal(output, record.chunkId);
        output.push_back(':');
        appendDecimal(output, record.offset);
        output.push_back(':');
        appendDecimal(output, record.length);
        output.push_back(':');
        output.append(checksum::checksumAlgorithmName(record.checksum.algorithm));
        output.push_back(':');
        for (unsigned shift = 32; shift != 0; shift -= 4)
            output.push_back(hexDigits[(record.checksum.value >> (shift - 4)) & 0xFU]);
    }
    return output;
}
}  // namespace cpnetflux::checkpoint::detail
