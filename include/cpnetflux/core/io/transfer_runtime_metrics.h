#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "cpnetflux/core/io/compression_controller.h"
#include "cpnetflux/core/protocol/compressed_data.h"

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
    // Compressed frames by CompressionCodec.
    std::array<std::uint64_t, protocol::kCompressionCodecCount> codecFrames{};
    // Adaptive policy only: files by the controller's final CompressionReason, and its switches.
    std::array<std::uint64_t, kCompressionReasonCount> adaptiveFileReasons{};
    std::uint64_t compressionSwitches = 0;
};

}  // namespace cpnetflux::core::io
