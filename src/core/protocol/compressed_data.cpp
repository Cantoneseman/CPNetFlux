#include "cpnetflux/core/protocol/compressed_data.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>

#include <zlib.h>

#ifdef CPNETFLUX_HAVE_ZSTD
#include <zstd.h>
#endif
#ifdef CPNETFLUX_HAVE_LZ4
#include <lz4.h>
#endif

namespace cpnetflux::core::protocol {
namespace {

constexpr std::uint64_t kLogicalLengthMask = 0x00FFFFFFFFFFFFFFULL;

void appendU64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        const std::size_t shift = 56U - index * 8U;
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint64_t readU64(const std::uint8_t* data) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(data[index]);
    }
    return value;
}

common::Status zlibStatus(const char* operation, int rc) {
    return common::Status::runtimeError(std::string(operation) + " failed rc=" +
                                       std::to_string(rc));
}

common::Result<std::size_t> compressZlib(const std::uint8_t* data, std::size_t length, int level,
                                         std::uint8_t* output, std::size_t capacity) {
    if (length > static_cast<std::size_t>(std::numeric_limits<uLong>::max())) {
        return common::Status::invalidArgument("compressed DATA input exceeds zlib limit");
    }
    uLongf compressedLength = static_cast<uLongf>(capacity);
    const int zlibLevel = level == kDefaultCompressionLevel ? Z_BEST_SPEED : std::clamp(level, 1, 9);
    const int rc = compress2(output, &compressedLength, data, static_cast<uLong>(length), zlibLevel);
    if (rc != Z_OK) {
        return zlibStatus("compress2", rc);
    }
    return static_cast<std::size_t>(compressedLength);
}

common::Status decompressZlib(const std::uint8_t* input, std::size_t inputLength,
                              std::vector<std::uint8_t>& output) {
    if (output.size() > static_cast<std::size_t>(std::numeric_limits<uLongf>::max())) {
        return common::Status::invalidArgument("compressed DATA output exceeds zlib limit");
    }
    uLongf outputLength = static_cast<uLongf>(output.size());
    const int rc = uncompress(output.data(), &outputLength, input, static_cast<uLong>(inputLength));
    if (rc != Z_OK) {
        return zlibStatus("uncompress", rc);
    }
    if (outputLength != output.size()) {
        return common::Status::invalidArgument("compressed DATA decompressed length mismatch");
    }
    return common::Status::ok();
}

#ifdef CPNETFLUX_HAVE_ZSTD
// One context per thread: allocating a context per DATA frame is measurable.
struct ZstdCContextDeleter {
    void operator()(ZSTD_CCtx* context) const noexcept { ZSTD_freeCCtx(context); }
};
struct ZstdDContextDeleter {
    void operator()(ZSTD_DCtx* context) const noexcept { ZSTD_freeDCtx(context); }
};

common::Result<std::size_t> compressZstd(const std::uint8_t* data, std::size_t length, int level,
                                         std::uint8_t* output, std::size_t capacity) {
    thread_local std::unique_ptr<ZSTD_CCtx, ZstdCContextDeleter> context(ZSTD_createCCtx());
    if (!context) {
        return common::Status::runtimeError("zstd context allocation failed");
    }
    const int zstdLevel = level == kDefaultCompressionLevel ? 1 : std::clamp(level, 1, 19);
    const std::size_t size = ZSTD_compressCCtx(context.get(), output, capacity, data, length, zstdLevel);
    if (ZSTD_isError(size) != 0U) {
        return common::Status::runtimeError(std::string("zstd compress failed: ") +
                                             ZSTD_getErrorName(size));
    }
    return size;
}

common::Status decompressZstd(const std::uint8_t* input, std::size_t inputLength,
                              std::vector<std::uint8_t>& output) {
    thread_local std::unique_ptr<ZSTD_DCtx, ZstdDContextDeleter> context(ZSTD_createDCtx());
    if (!context) {
        return common::Status::runtimeError("zstd context allocation failed");
    }
    const std::size_t size =
        ZSTD_decompressDCtx(context.get(), output.data(), output.size(), input, inputLength);
    if (ZSTD_isError(size) != 0U || size != output.size()) {
        return common::Status::invalidArgument("zstd decompress failed");
    }
    return common::Status::ok();
}
#endif

#ifdef CPNETFLUX_HAVE_LZ4
common::Result<std::size_t> compressLz4(const std::uint8_t* data, std::size_t length,
                                        std::uint8_t* output, std::size_t capacity) {
    if (length > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE)) {
        return common::Status::invalidArgument("compressed DATA input exceeds lz4 limit");
    }
    thread_local std::vector<char> state(static_cast<std::size_t>(LZ4_sizeofState()));
    const int size = LZ4_compress_fast_extState(
        state.data(), reinterpret_cast<const char*>(data), reinterpret_cast<char*>(output),
        static_cast<int>(length), static_cast<int>(std::min<std::size_t>(capacity, INT32_MAX)), 1);
    if (size <= 0) {
        return common::Status::runtimeError("lz4 compress failed");
    }
    return static_cast<std::size_t>(size);
}

common::Status decompressLz4(const std::uint8_t* input, std::size_t inputLength,
                             std::vector<std::uint8_t>& output) {
    if (inputLength > static_cast<std::size_t>(INT32_MAX) ||
        output.size() > static_cast<std::size_t>(INT32_MAX)) {
        return common::Status::invalidArgument("compressed DATA exceeds lz4 limit");
    }
    const int size = LZ4_decompress_safe(reinterpret_cast<const char*>(input),
                                         reinterpret_cast<char*>(output.data()),
                                         static_cast<int>(inputLength),
                                         static_cast<int>(output.size()));
    if (size < 0 || static_cast<std::size_t>(size) != output.size()) {
        return common::Status::invalidArgument("lz4 decompress failed");
    }
    return common::Status::ok();
}
#endif

std::size_t compressBoundFor(CompressionCodec codec, std::size_t length) noexcept {
    switch (codec) {
        case CompressionCodec::Zlib:
            return static_cast<std::size_t>(compressBound(static_cast<uLong>(length)));
        case CompressionCodec::Zstd:
#ifdef CPNETFLUX_HAVE_ZSTD
            return ZSTD_compressBound(length);
#else
            break;
#endif
        case CompressionCodec::Lz4:
#ifdef CPNETFLUX_HAVE_LZ4
            return static_cast<std::size_t>(LZ4_compressBound(static_cast<int>(length)));
#else
            break;
#endif
    }
    return 0;
}

common::Status unsupportedCodec(CompressionCodec codec) {
    return common::Status::invalidArgument(std::string("compression codec not built in: ") +
                                           compressionCodecName(codec));
}

}  // namespace

const char* compressionCodecName(CompressionCodec codec) noexcept {
    switch (codec) {
        case CompressionCodec::Zlib:
            return "zlib";
        case CompressionCodec::Zstd:
            return "zstd";
        case CompressionCodec::Lz4:
            return "lz4";
    }
    return "unknown";
}

std::optional<CompressionCodec> parseCompressionCodec(std::string_view name) noexcept {
    for (std::size_t index = 0; index < kCompressionCodecCount; ++index) {
        const auto codec = static_cast<CompressionCodec>(index);
        if (name == compressionCodecName(codec)) {
            return codec;
        }
    }
    return std::nullopt;
}

std::uint32_t supportedCompressionCodecMask() noexcept {
    std::uint32_t mask = compressionCodecBit(CompressionCodec::Zlib);
#ifdef CPNETFLUX_HAVE_ZSTD
    mask |= compressionCodecBit(CompressionCodec::Zstd);
#endif
#ifdef CPNETFLUX_HAVE_LZ4
    mask |= compressionCodecBit(CompressionCodec::Lz4);
#endif
    return mask;
}

std::uint32_t parseCompressionCodecList(std::string_view list) noexcept {
    std::uint32_t mask = 0;
    while (!list.empty()) {
        const auto comma = list.find(',');
        if (const auto codec = parseCompressionCodec(list.substr(0, comma))) {
            mask |= compressionCodecBit(*codec);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        list.remove_prefix(comma + 1);
    }
    return mask;
}

std::string describeCompressionCodecMask(std::uint32_t mask) {
    std::string output;
    for (std::size_t index = 0; index < kCompressionCodecCount; ++index) {
        const auto codec = static_cast<CompressionCodec>(index);
        if ((mask & compressionCodecBit(codec)) == 0) {
            continue;
        }
        if (!output.empty()) {
            output += ',';
        }
        output += compressionCodecName(codec);
    }
    return output.empty() ? "none" : output;
}

common::Result<std::vector<std::uint8_t>> encodeCompressedDataPayload(
    const std::uint8_t* data, std::size_t length, std::size_t maxPayloadBytes,
    CompressionCodec codec, int level) {
    if (data == nullptr && length != 0) {
        return common::Status::invalidArgument("compressed DATA input is null");
    }
    if (length == 0) {
        return common::Status::invalidArgument("compressed DATA input is empty");
    }
    if (static_cast<std::uint64_t>(length) > kLogicalLengthMask) {
        return common::Status::invalidArgument("compressed DATA input is too large");
    }
    if (maxPayloadBytes < kCompressedDataPrefixSize) {
        return common::Status::invalidArgument("compressed DATA max payload is too small");
    }
    if ((supportedCompressionCodecMask() & compressionCodecBit(codec)) == 0) {
        return unsupportedCodec(codec);
    }

    const std::size_t bound = compressBoundFor(codec, length);
    std::vector<std::uint8_t> output;
    output.reserve(kCompressedDataPrefixSize + bound);
    appendU64(output, (static_cast<std::uint64_t>(codec) << 56U) |
                          static_cast<std::uint64_t>(length));
    output.resize(kCompressedDataPrefixSize + bound);

    std::uint8_t* target = output.data() + kCompressedDataPrefixSize;
    common::Result<std::size_t> compressed = std::size_t{0};
    switch (codec) {
        case CompressionCodec::Zlib:
            compressed = compressZlib(data, length, level, target, bound);
            break;
        case CompressionCodec::Zstd:
#ifdef CPNETFLUX_HAVE_ZSTD
            compressed = compressZstd(data, length, level, target, bound);
#endif
            break;
        case CompressionCodec::Lz4:
#ifdef CPNETFLUX_HAVE_LZ4
            compressed = compressLz4(data, length, target, bound);
#endif
            break;
    }
    if (!compressed.isOk()) {
        return compressed.status();
    }
    const std::size_t totalLength = kCompressedDataPrefixSize + compressed.value();
    if (totalLength > maxPayloadBytes) {
        return common::Status::invalidArgument("compressed DATA output exceeds payload limit");
    }
    output.resize(totalLength);
    return output;
}

common::Result<std::uint64_t> compressedDataPayloadLogicalLength(const std::uint8_t* payload,
                                                                 std::size_t payloadSize) {
    if (payload == nullptr) {
        return common::Status::invalidArgument("compressed DATA payload is null");
    }
    if (payloadSize <= kCompressedDataPrefixSize) {
        return common::Status::invalidArgument("compressed DATA payload is too small");
    }
    auto codec = compressedDataPayloadCodec(payload, payloadSize);
    if (!codec.isOk()) {
        return codec.status();
    }
    return readU64(payload) & kLogicalLengthMask;
}

common::Result<CompressionCodec> compressedDataPayloadCodec(const std::uint8_t* payload,
                                                            std::size_t payloadSize) {
    if (payload == nullptr) {
        return common::Status::invalidArgument("compressed DATA payload is null");
    }
    if (payloadSize <= kCompressedDataPrefixSize) {
        return common::Status::invalidArgument("compressed DATA payload is too small");
    }
    if (payload[0] >= kCompressionCodecCount) {
        return common::Status::invalidArgument("compressed DATA codec is unknown");
    }
    return static_cast<CompressionCodec>(payload[0]);
}

common::Result<std::vector<std::uint8_t>> decodeCompressedDataPayload(
    const std::uint8_t* payload, std::size_t payloadSize, std::uint64_t expectedRawLength,
    std::uint64_t maxRawLength) {
    auto logicalLength = compressedDataPayloadLogicalLength(payload, payloadSize);
    if (!logicalLength.isOk()) {
        return logicalLength.status();
    }
    if (logicalLength.value() != expectedRawLength) {
        return common::Status::invalidArgument("compressed DATA logical length mismatch");
    }
    if (logicalLength.value() > maxRawLength) {
        return common::Status::invalidArgument("compressed DATA output exceeds limit");
    }
    const auto codec = static_cast<CompressionCodec>(payload[0]);
    if ((supportedCompressionCodecMask() & compressionCodecBit(codec)) == 0) {
        return unsupportedCodec(codec);
    }

    std::vector<std::uint8_t> output(static_cast<std::size_t>(logicalLength.value()));
    const std::uint8_t* input = payload + kCompressedDataPrefixSize;
    const std::size_t inputLength = payloadSize - kCompressedDataPrefixSize;
    common::Status status = common::Status::ok();
    switch (codec) {
        case CompressionCodec::Zlib:
            status = decompressZlib(input, inputLength, output);
            break;
        case CompressionCodec::Zstd:
#ifdef CPNETFLUX_HAVE_ZSTD
            status = decompressZstd(input, inputLength, output);
#endif
            break;
        case CompressionCodec::Lz4:
#ifdef CPNETFLUX_HAVE_LZ4
            status = decompressLz4(input, inputLength, output);
#endif
            break;
    }
    if (!status.isOk()) {
        return status;
    }
    return output;
}

}  // namespace cpnetflux::core::protocol
