#include "cpnetflux/core/protocol/compressed_data.h"

#include <limits>
#include <string>

#include <zlib.h>

namespace cpnetflux::core::protocol {
namespace {

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

}  // namespace

common::Result<std::vector<std::uint8_t>> encodeCompressedDataPayload(
    const std::uint8_t* data, std::size_t length, std::size_t maxPayloadBytes) {
    if (data == nullptr && length != 0) {
        return common::Status::invalidArgument("compressed DATA input is null");
    }
    if (length == 0) {
        return common::Status::invalidArgument("compressed DATA input is empty");
    }
    if (length > static_cast<std::size_t>(std::numeric_limits<uLong>::max())) {
        return common::Status::invalidArgument("compressed DATA input exceeds zlib limit");
    }
    if (maxPayloadBytes < kCompressedDataPrefixSize) {
        return common::Status::invalidArgument("compressed DATA max payload is too small");
    }

    const uLong sourceLength = static_cast<uLong>(length);
    const uLongf bound = compressBound(sourceLength);

    std::vector<std::uint8_t> output;
    output.reserve(kCompressedDataPrefixSize + static_cast<std::size_t>(bound));
    appendU64(output, static_cast<std::uint64_t>(length));
    output.resize(kCompressedDataPrefixSize + static_cast<std::size_t>(bound));

    uLongf compressedLength = bound;
    const int rc = compress2(output.data() + kCompressedDataPrefixSize, &compressedLength, data,
                             sourceLength, Z_BEST_SPEED);
    if (rc != Z_OK) {
        return zlibStatus("compress2", rc);
    }
    const std::size_t totalLength = kCompressedDataPrefixSize +
                                    static_cast<std::size_t>(compressedLength);
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
    return readU64(payload);
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
    if (logicalLength.value() > static_cast<std::uint64_t>(std::numeric_limits<uLongf>::max())) {
        return common::Status::invalidArgument("compressed DATA output exceeds zlib limit");
    }

    std::vector<std::uint8_t> output(static_cast<std::size_t>(logicalLength.value()));
    uLongf outputLength = static_cast<uLongf>(logicalLength.value());
    const int rc = uncompress(output.data(), &outputLength, payload + kCompressedDataPrefixSize,
                              static_cast<uLong>(payloadSize - kCompressedDataPrefixSize));
    if (rc != Z_OK) {
        return zlibStatus("uncompress", rc);
    }
    if (outputLength != logicalLength.value()) {
        return common::Status::invalidArgument("compressed DATA decompressed length mismatch");
    }
    return output;
}

}  // namespace cpnetflux::core::protocol
