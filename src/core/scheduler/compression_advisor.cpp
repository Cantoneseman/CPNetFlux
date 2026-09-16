#include "cpnetflux/core/scheduler/compression_advisor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <vector>

#include <zlib.h>

namespace cpnetflux::core::scheduler {
namespace {

std::uint64_t deterministicSeed(const std::string& fileId, std::uint64_t fileSize,
                                 std::uint64_t offset) {
    std::uint64_t seed = 1469598103934665603ULL;
    for (unsigned char value : fileId) {
        seed ^= value;
        seed *= 1099511628211ULL;
    }
    seed ^= fileSize + 0x9e3779b97f4a7c15ULL;
    seed ^= offset + 0x632be59bd9b4e019ULL;
    return seed;
}

std::vector<std::uint64_t> sampleOffsets(std::uint64_t fileSize, std::uint64_t blockBytes,
                                        const std::string& fileId, std::uint64_t resumeGeneration) {
    std::vector<std::uint64_t> offsets;
    if (fileSize == 0) {
        return offsets;
    }
    const std::uint64_t block = std::min(blockBytes, fileSize);
    offsets.push_back(0);
    if (fileSize > block) {
        offsets.push_back(std::min(fileSize - block, fileSize / 2));
        offsets.push_back(fileSize - block);
        const std::uint64_t range = fileSize - block + 1;
        const std::uint64_t seed = deterministicSeed(fileId, fileSize, resumeGeneration);
        offsets.push_back(seed % range);
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    return offsets;
}

common::Result<std::vector<std::uint8_t>> readSampleBytes(const std::filesystem::path& path,
                                                         std::uint64_t offset,
                                                         std::uint64_t length) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return common::Status::runtimeError("sample file cannot be opened: " + path.string());
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        return common::Status::runtimeError("sample seek failed: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const std::streamsize got = input.gcount();
    if (got < 0) {
        return common::Status::runtimeError("sample read failed: " + path.string());
    }
    bytes.resize(static_cast<std::size_t>(got));
    return bytes;
}

common::Result<std::vector<std::uint8_t>> compressSample(const std::vector<std::uint8_t>& raw) {
    if (raw.empty()) {
        return std::vector<std::uint8_t>{};
    }
    const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(static_cast<std::size_t>(bound));
    uLongf outSize = bound;
    const int rc = compress2(compressed.data(), &outSize, raw.data(),
                             static_cast<uLong>(raw.size()), Z_BEST_SPEED);
    if (rc != Z_OK) {
        return common::Status::runtimeError("compress2 failed");
    }
    compressed.resize(static_cast<std::size_t>(outSize));
    return compressed;
}

}  // namespace

CompressionAdvisor::CompressionAdvisor(CompressionAdvisorConfig config) : config_(config) {}

common::Result<CompressionSampleRecord> CompressionAdvisor::analyzeFile(
    const std::filesystem::path& path, const std::string& fileId, std::uint64_t fileSize,
    std::uint64_t resumeGeneration, double cpuPercent, double linkReadyRatio,
    bool allowSampling) const {
    CompressionSampleRecord record;
    record.fileId = fileId;
    record.cpuPercent = cpuPercent;
    record.linkReadyRatio = linkReadyRatio;

    if (!allowSampling) {
        record.reason = "sample_unavailable";
        record.decision = compressionDispositionName(CompressionDisposition::Raw);
        return record;
    }
    if (fileSize == 0) {
        record.offset = 0;
        record.length = 0;
        record.decision = compressionDispositionName(CompressionDisposition::Raw);
        record.reason = "empty_file";
        return record;
    }

    const std::vector<std::uint64_t> offsets =
        sampleOffsets(fileSize, config_.sampleBlockBytes, fileId, resumeGeneration);
    std::vector<std::uint8_t> rawSample;
    std::uint64_t firstOffset = std::numeric_limits<std::uint64_t>::max();
    for (std::uint64_t offset : offsets) {
        const std::uint64_t length = std::min(config_.sampleBlockBytes, fileSize - offset);
        auto bytes = readSampleBytes(path, offset, length);
        if (!bytes.isOk()) {
            record.reason = "sample_unavailable";
            record.decision = compressionDispositionName(CompressionDisposition::Raw);
            return record;
        }
        if (firstOffset == std::numeric_limits<std::uint64_t>::max()) {
            firstOffset = offset;
        }
        rawSample.insert(rawSample.end(), bytes.value().begin(), bytes.value().end());
    }
    if (rawSample.empty()) {
        record.reason = "sample_unavailable";
        record.decision = compressionDispositionName(CompressionDisposition::Raw);
        return record;
    }

    auto compressed = compressSample(rawSample);
    if (!compressed.isOk()) {
        record.reason = "compression_failure";
        record.decision = compressionDispositionName(CompressionDisposition::Raw);
        return record;
    }

    record.offset = firstOffset == std::numeric_limits<std::uint64_t>::max() ? 0 : firstOffset;
    record.length = rawSample.size();
    record.sampleRawBytes = rawSample.size();
    record.sampleCompressedBytes = compressed.value().size();
    record.sampleRatio = rawSample.empty()
                             ? 1.0
                             : static_cast<double>(compressed.value().size()) /
                                   static_cast<double>(rawSample.size());
    const auto start = std::chrono::steady_clock::now();
    (void)compressSample(rawSample);
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    const double safeSeconds = seconds > 0.0 ? seconds : 1e-9;
    record.sampleCompGbps = static_cast<double>(rawSample.size()) * 8.0 / safeSeconds / 1e9;
    const CompressionDecision decision =
        decide(record.sampleRatio, record.sampleCompGbps, cpuPercent, linkReadyRatio);
    record.decision = compressionDispositionName(decision.disposition);
    record.reason = decision.reason;
    return record;
}

CompressionDecision CompressionAdvisor::decide(double sampleRatio, double sampleCompGbps,
                                                double cpuPercent, double linkReadyRatio) const {
    CompressionDecision decision;
    decision.sampleRatio = sampleRatio;
    decision.sampleCompGbps = sampleCompGbps;
    decision.cpuPercent = cpuPercent;
    decision.linkReadyRatio = linkReadyRatio;
    if (sampleRatio > config_.poorRatioThreshold) {
        decision.reason = "poor_ratio";
        return decision;
    }
    if (sampleCompGbps < config_.minCompressGbps) {
        decision.reason = "slow_compression";
        return decision;
    }
    if (cpuPercent > config_.cpuHighThreshold) {
        decision.reason = "cpu_pressure";
        return decision;
    }
    if (linkReadyRatio < config_.lowReadyRatio) {
        decision.reason = "link_starvation";
        return decision;
    }
    decision.disposition = CompressionDisposition::CompressionCandidate;
    decision.reason = "worthwhile";
    return decision;
}

const CompressionAdvisorConfig& CompressionAdvisor::config() const noexcept { return config_; }

}  // namespace cpnetflux::core::scheduler
