#pragma once

#include <cstdint>

#include "cpnetflux/core/protocol/compressed_data.h"

namespace cpnetflux::core::io {

struct HotPathCompressionOptions {
    bool enabled = false;
    bool candidate = false;
    std::uint32_t maxPayloadBytes = 0;
    bool forceRaw = false;
    bool testForceCompressFailure = false;
    bool testCorruptCompressedPayload = false;
    protocol::CompressionCodec codec = protocol::CompressionCodec::Zlib;
    int level = protocol::kDefaultCompressionLevel;
    // Per-file CompressionController choosing among codecMask (compressionCodecBit set, 0 means
    // codec only) instead of compressing every block with codec.
    bool adaptive = false;
    std::uint32_t codecMask = 0;
    // Known link speed for the controller; 0 when unknown.
    double linkBytesPerSecond = 0.0;
};

}  // namespace cpnetflux::core::io
