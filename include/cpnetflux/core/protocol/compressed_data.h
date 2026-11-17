#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::protocol {

inline constexpr std::size_t kCompressedDataPrefixSize = 8;

// Stored in the most significant byte of the 8-byte logical-length prefix. Logical lengths are
// bounded by the frame payload limit, so zlib payloads (codec 0) stay byte-identical to the
// original format and older receivers reject the other codecs as an oversized length.
enum class CompressionCodec : std::uint8_t {
    Zlib = 0,
    Zstd = 1,
    Lz4 = 2,
};

inline constexpr std::size_t kCompressionCodecCount = 3;
inline constexpr int kDefaultCompressionLevel = -1;

[[nodiscard]] constexpr std::uint32_t compressionCodecBit(CompressionCodec codec) noexcept {
    return 1U << static_cast<std::uint32_t>(codec);
}

[[nodiscard]] const char* compressionCodecName(CompressionCodec codec) noexcept;
[[nodiscard]] std::optional<CompressionCodec> parseCompressionCodec(std::string_view name) noexcept;

// compressionCodecBit() set of codecs compiled into this binary; zlib is always present.
[[nodiscard]] std::uint32_t supportedCompressionCodecMask() noexcept;

// "zlib,zstd" style list of the codecs in mask, or "none" when it is empty.
[[nodiscard]] std::string describeCompressionCodecMask(std::uint32_t mask);

// compressionCodecBit() set named by a "zstd,lz4,zlib" list; unknown names are ignored.
[[nodiscard]] std::uint32_t parseCompressionCodecList(std::string_view list) noexcept;

// Level is codec specific (zlib 1-9, zstd 1-19, lz4 acceleration is fixed);
// kDefaultCompressionLevel keeps zlib at Z_BEST_SPEED and zstd at level 1.
[[nodiscard]] common::Result<std::vector<std::uint8_t>> encodeCompressedDataPayload(
    const std::uint8_t* data, std::size_t length, std::size_t maxPayloadBytes,
    CompressionCodec codec = CompressionCodec::Zlib, int level = kDefaultCompressionLevel);

[[nodiscard]] common::Result<std::vector<std::uint8_t>> decodeCompressedDataPayload(
    const std::uint8_t* payload, std::size_t payloadSize, std::uint64_t expectedRawLength,
    std::uint64_t maxRawLength);

[[nodiscard]] common::Result<std::uint64_t> compressedDataPayloadLogicalLength(
    const std::uint8_t* payload, std::size_t payloadSize);

[[nodiscard]] common::Result<CompressionCodec> compressedDataPayloadCodec(
    const std::uint8_t* payload, std::size_t payloadSize);

}  // namespace cpnetflux::core::protocol
