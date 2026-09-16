#pragma once

#include <cstdint>
#include <string>

namespace cpnetflux::core::io {

struct TransferRuntimeMetrics {
    double elapsedSeconds = 0.0;
    double cpuPercent = 0.0;
    double sendSeconds = 0.0;
    double recvSeconds = 0.0;
    double readSeconds = 0.0;
    double writeSeconds = 0.0;
    double checksumSeconds = 0.0;
    std::uint64_t logicalBytes = 0;
    std::uint64_t wireBytes = 0;
    std::uint64_t compressionAttempts = 0;
    std::uint64_t compressedFrames = 0;
    std::uint64_t rawFallbackFrames = 0;
    std::uint64_t compressionFailures = 0;
    std::uint64_t decompressionFailures = 0;
    std::uint64_t compressedLogicalBytes = 0;
    std::uint64_t compressedWireBytes = 0;
    std::string compressionFallbackReason;
};

}  // namespace cpnetflux::core::io
