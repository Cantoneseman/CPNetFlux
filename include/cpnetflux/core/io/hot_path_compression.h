#pragma once

#include <cstdint>

namespace cpnetflux::core::io {

struct HotPathCompressionOptions {
    bool enabled = false;
    bool candidate = false;
    std::uint32_t maxPayloadBytes = 0;
    bool forceRaw = false;
    bool testForceCompressFailure = false;
    bool testCorruptCompressedPayload = false;
};

}  // namespace cpnetflux::core::io
