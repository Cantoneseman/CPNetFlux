#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::protocol {

inline constexpr std::size_t kCompressedDataPrefixSize = 8;

[[nodiscard]] common::Result<std::vector<std::uint8_t>> encodeCompressedDataPayload(
    const std::uint8_t* data, std::size_t length, std::size_t maxPayloadBytes);

[[nodiscard]] common::Result<std::vector<std::uint8_t>> decodeCompressedDataPayload(
    const std::uint8_t* payload, std::size_t payloadSize, std::uint64_t expectedRawLength,
    std::uint64_t maxRawLength);

[[nodiscard]] common::Result<std::uint64_t> compressedDataPayloadLogicalLength(
    const std::uint8_t* payload, std::size_t payloadSize);

}  // namespace cpnetflux::core::protocol
