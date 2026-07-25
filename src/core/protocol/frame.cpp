#include "gridflux/core/protocol/frame.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace gridflux::core::protocol {
namespace {

constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kHeaderSizeOffset = 6;
constexpr std::size_t kTypeOffset = 8;
constexpr std::size_t kFlagsOffset = 10;
constexpr std::size_t kStreamIdOffset = 12;
constexpr std::size_t kChunkIdOffset = 16;
constexpr std::size_t kOffsetOffset = 24;
constexpr std::size_t kPayloadSizeOffset = 32;
constexpr std::size_t kStatusCodeOffset = 36;
constexpr std::size_t kTotalSizeOffset = 40;
constexpr std::size_t kReserved1Offset = 48;
constexpr std::size_t kReserved2Offset = 56;

void writeU16(EncodedFrameHeader& encoded, std::size_t offset, std::uint16_t value) noexcept {
    encoded[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    encoded[offset + 1] = static_cast<std::uint8_t>(value & 0xFFU);
}

void writeU32(EncodedFrameHeader& encoded, std::size_t offset, std::uint32_t value) noexcept {
    encoded[offset] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    encoded[offset + 1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    encoded[offset + 2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    encoded[offset + 3] = static_cast<std::uint8_t>(value & 0xFFU);
}

void writeU64(EncodedFrameHeader& encoded, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < 8; ++index) {
        const std::size_t shift = 56U - index * 8U;
        encoded[offset + index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

std::uint16_t readU16(const EncodedFrameHeader& encoded, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(encoded[offset]) << 8U) |
                                      static_cast<std::uint16_t>(encoded[offset + 1]));
}

std::uint32_t readU32(const EncodedFrameHeader& encoded, std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(encoded[offset]) << 24U) |
           (static_cast<std::uint32_t>(encoded[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(encoded[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(encoded[offset + 3]);
}

std::uint64_t readU64(const EncodedFrameHeader& encoded, std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(encoded[offset + index]);
    }
    return value;
}

bool isKnownType(std::uint16_t type) noexcept {
    return type == static_cast<std::uint16_t>(FrameType::Data) ||
           type == static_cast<std::uint16_t>(FrameType::Fin) ||
           type == static_cast<std::uint16_t>(FrameType::Complete) ||
           type == static_cast<std::uint16_t>(FrameType::Error) ||
           type == static_cast<std::uint16_t>(FrameType::SessionInit) ||
           type == static_cast<std::uint16_t>(FrameType::ResumeResponse) ||
           type == static_cast<std::uint16_t>(FrameType::ChunkComplete);
}

bool isKnownStatus(std::uint32_t status) noexcept {
    return status <= static_cast<std::uint32_t>(FrameStatusCode::ManifestCorrupt);
}

void appendU16(std::vector<std::uint8_t>& encoded, std::uint16_t value) {
    encoded.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    encoded.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& encoded, std::uint32_t value) {
    encoded.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    encoded.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    encoded.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    encoded.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void appendU64(std::vector<std::uint8_t>& encoded, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        const std::size_t shift = 56U - index * 8U;
        encoded.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint16_t readPayloadU16(const std::uint8_t* data, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8U) |
                                      static_cast<std::uint16_t>(data[offset + 1]));
}

std::uint32_t readPayloadU32(const std::uint8_t* data, std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(data[offset]) << 24U) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

std::uint64_t readPayloadU64(const std::uint8_t* data, std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(data[offset + index]);
    }
    return value;
}

bool isKnownSessionMode(std::uint16_t mode) noexcept {
    return mode == static_cast<std::uint16_t>(SessionMode::New) ||
           mode == static_cast<std::uint16_t>(SessionMode::Resume);
}

bool isKnownChecksumAlgorithm(std::uint16_t algorithm) noexcept {
    return algorithm == static_cast<std::uint16_t>(checksum::ChecksumAlgorithm::None) ||
           algorithm == static_cast<std::uint16_t>(checksum::ChecksumAlgorithm::Crc32c);
}

}  // namespace

EncodedFrameHeader encodeFrameHeader(const FrameHeader& header) noexcept {
    EncodedFrameHeader encoded{};
    writeU32(encoded, kMagicOffset, header.magic);
    writeU16(encoded, kVersionOffset, header.version);
    writeU16(encoded, kHeaderSizeOffset, header.headerSize);
    writeU16(encoded, kTypeOffset, static_cast<std::uint16_t>(header.type));
    writeU16(encoded, kFlagsOffset, header.flags);
    writeU32(encoded, kStreamIdOffset, header.streamId);
    writeU64(encoded, kChunkIdOffset, header.chunkId);
    writeU64(encoded, kOffsetOffset, header.offset);
    writeU32(encoded, kPayloadSizeOffset, header.payloadSize);
    writeU32(encoded, kStatusCodeOffset, static_cast<std::uint32_t>(header.statusCode));
    writeU64(encoded, kTotalSizeOffset, header.totalSize);
    writeU64(encoded, kReserved1Offset, 0);
    writeU64(encoded, kReserved2Offset, 0);
    return encoded;
}

common::Result<FrameHeader> decodeFrameHeader(const EncodedFrameHeader& encoded) {
    FrameHeader header;
    header.magic = readU32(encoded, kMagicOffset);
    header.version = readU16(encoded, kVersionOffset);
    header.headerSize = readU16(encoded, kHeaderSizeOffset);

    const std::uint16_t type = readU16(encoded, kTypeOffset);
    if (!isKnownType(type)) {
        return common::Status::invalidArgument("invalid frame type");
    }
    header.type = static_cast<FrameType>(type);

    header.flags = readU16(encoded, kFlagsOffset);
    header.streamId = readU32(encoded, kStreamIdOffset);
    header.chunkId = readU64(encoded, kChunkIdOffset);
    header.offset = readU64(encoded, kOffsetOffset);
    header.payloadSize = readU32(encoded, kPayloadSizeOffset);
    const std::uint32_t status = readU32(encoded, kStatusCodeOffset);
    if (!isKnownStatus(status)) {
        return common::Status::invalidArgument("invalid frame status code");
    }
    header.statusCode = static_cast<FrameStatusCode>(status);
    header.totalSize = readU64(encoded, kTotalSizeOffset);

    if (readU64(encoded, kReserved1Offset) != 0 || readU64(encoded, kReserved2Offset) != 0) {
        return common::Status::invalidArgument("frame reserved fields must be zero");
    }

    return header;
}

common::Status validateFrameHeader(const FrameHeader& header, std::uint32_t maxPayloadSize) {
    if (header.magic != kFrameMagic) {
        return common::Status::invalidArgument("invalid frame magic");
    }
    if (header.version != kFrameVersion) {
        return common::Status::invalidArgument("invalid frame version");
    }
    if (header.headerSize != kFrameHeaderSize) {
        return common::Status::invalidArgument("invalid frame header size");
    }
    if (header.flags != 0) {
        return common::Status::invalidArgument("frame flags must be zero");
    }
    if (header.payloadSize > maxPayloadSize) {
        return common::Status::invalidArgument("frame payload exceeds buffer size");
    }
    if (header.type == FrameType::Complete) {
        if (header.payloadSize != 0) {
            return common::Status::invalidArgument("COMPLETE frame must not carry payload");
        }
        if (header.statusCode != FrameStatusCode::Ok) {
            return common::Status::invalidArgument("COMPLETE frame status must be OK");
        }
        return common::Status::ok();
    }
    if (header.type == FrameType::Error) {
        if (header.payloadSize != 0) {
            return common::Status::invalidArgument("ERROR frame must not carry payload");
        }
        if (header.statusCode == FrameStatusCode::Ok) {
            return common::Status::invalidArgument("ERROR frame status must not be OK");
        }
        return common::Status::ok();
    }
    if (header.type == FrameType::SessionInit || header.type == FrameType::ResumeResponse ||
        header.type == FrameType::ChunkComplete) {
        if (header.payloadSize == 0) {
            return common::Status::invalidArgument("control frame must carry payload");
        }
        if (header.statusCode != FrameStatusCode::Ok) {
            return common::Status::invalidArgument("control frame status must be OK");
        }
        return common::Status::ok();
    }
    if (header.statusCode != FrameStatusCode::Ok) {
        return common::Status::invalidArgument("DATA/FIN frame status must be OK");
    }
    if (header.type == FrameType::Fin) {
        if (header.payloadSize != 0) {
            return common::Status::invalidArgument("FIN frame must not carry payload");
        }
        return common::Status::ok();
    }
    if (header.type != FrameType::Data) {
        return common::Status::invalidArgument("invalid frame type");
    }
    if (header.payloadSize == 0) {
        return common::Status::invalidArgument("DATA frame payload must be greater than zero");
    }
    if (header.offset > header.totalSize ||
        static_cast<std::uint64_t>(header.payloadSize) > header.totalSize - header.offset) {
        return common::Status::invalidArgument("frame payload range exceeds total size");
    }

    return common::Status::ok();
}

common::Result<std::vector<std::uint8_t>> encodeSessionInitPayload(
    const SessionInitPayload& payload) {
    if (payload.transferId.empty() ||
        payload.transferId.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return common::Status::invalidArgument("invalid transfer_id length");
    }
    if (payload.sourcePath.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return common::Status::invalidArgument("invalid source_path length");
    }
    if (payload.chunkSize == 0) {
        return common::Status::invalidArgument("chunk_size must be greater than zero");
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(24 + payload.transferId.size() +
                    (payload.sourcePath.empty() ? 0 : 4 + payload.sourcePath.size()));
    appendU16(encoded, static_cast<std::uint16_t>(payload.mode));
    appendU16(encoded, static_cast<std::uint16_t>(payload.transferId.size()));
    appendU16(encoded, static_cast<std::uint16_t>(payload.checksumAlgorithm));
    appendU16(encoded, 0);
    appendU64(encoded, payload.totalSize);
    appendU64(encoded, payload.chunkSize);
    encoded.insert(encoded.end(), payload.transferId.begin(), payload.transferId.end());
    if (!payload.sourcePath.empty()) {
        appendU16(encoded, static_cast<std::uint16_t>(payload.sourcePath.size()));
        appendU16(encoded, 0);
        encoded.insert(encoded.end(), payload.sourcePath.begin(), payload.sourcePath.end());
    }
    return encoded;
}

common::Result<SessionInitPayload> decodeSessionInitPayload(const std::uint8_t* data,
                                                            std::size_t size) {
    if (size < 24) {
        return common::Status::invalidArgument("session init payload is too small");
    }

    const std::uint16_t mode = readPayloadU16(data, 0);
    if (!isKnownSessionMode(mode)) {
        return common::Status::invalidArgument("invalid session mode");
    }
    const std::uint16_t transferIdSize = readPayloadU16(data, 2);
    const std::uint16_t algorithm = readPayloadU16(data, 4);
    if (!isKnownChecksumAlgorithm(algorithm)) {
        return common::Status::invalidArgument("invalid checksum algorithm");
    }
    if (readPayloadU16(data, 6) != 0) {
        return common::Status::invalidArgument("session init reserved field must be zero");
    }
    if (transferIdSize == 0 || size < 24U + transferIdSize) {
        return common::Status::invalidArgument("invalid transfer_id payload length");
    }

    SessionInitPayload payload;
    payload.mode = static_cast<SessionMode>(mode);
    payload.checksumAlgorithm = static_cast<checksum::ChecksumAlgorithm>(algorithm);
    payload.totalSize = readPayloadU64(data, 8);
    payload.chunkSize = readPayloadU64(data, 16);
    payload.transferId.assign(reinterpret_cast<const char*>(data + 24), transferIdSize);
    const std::size_t extensionOffset = 24U + transferIdSize;
    if (size != extensionOffset) {
        if (size < extensionOffset + 4U) {
            return common::Status::invalidArgument("invalid source_path extension length");
        }
        const std::uint16_t sourcePathSize = readPayloadU16(data, extensionOffset);
        if (readPayloadU16(data, extensionOffset + 2U) != 0) {
            return common::Status::invalidArgument(
                "session init source_path reserved field must be zero");
        }
        if (size != extensionOffset + 4U + sourcePathSize) {
            return common::Status::invalidArgument("invalid source_path payload length");
        }
        payload.sourcePath.assign(reinterpret_cast<const char*>(data + extensionOffset + 4U),
                                  sourcePathSize);
    }
    if (payload.chunkSize == 0) {
        return common::Status::invalidArgument("chunk_size must be greater than zero");
    }
    return payload;
}

common::Result<std::vector<std::uint8_t>> encodeResumeResponsePayload(
    const ResumeResponsePayload& payload) {
    if (payload.missingRanges.size() > (std::numeric_limits<std::uint32_t>::max() - 8ULL) / 16ULL) {
        return common::Status::invalidArgument("too many missing ranges");
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(8 + payload.missingRanges.size() * 16);
    appendU32(encoded, static_cast<std::uint32_t>(payload.statusCode));
    appendU32(encoded, static_cast<std::uint32_t>(payload.missingRanges.size()));
    for (const core::chunk::CompletedRange& range : payload.missingRanges) {
        appendU64(encoded, range.begin);
        appendU64(encoded, range.end - range.begin);
    }
    return encoded;
}

common::Result<ResumeResponsePayload> decodeResumeResponsePayload(const std::uint8_t* data,
                                                                  std::size_t size) {
    if (size < 8 || (size - 8) % 16 != 0) {
        return common::Status::invalidArgument("invalid resume response payload size");
    }

    const std::uint32_t status = readPayloadU32(data, 0);
    if (!isKnownStatus(status)) {
        return common::Status::invalidArgument("invalid resume response status");
    }

    const std::uint32_t rangeCount = readPayloadU32(data, 4);
    if (size != 8ULL + static_cast<std::uint64_t>(rangeCount) * 16ULL) {
        return common::Status::invalidArgument("resume response range count mismatch");
    }

    ResumeResponsePayload payload;
    payload.statusCode = static_cast<FrameStatusCode>(status);
    payload.missingRanges.reserve(rangeCount);
    for (std::uint32_t index = 0; index < rangeCount; ++index) {
        const std::size_t offset = 8ULL + static_cast<std::size_t>(index) * 16ULL;
        const std::uint64_t begin = readPayloadU64(data, offset);
        const std::uint64_t length = readPayloadU64(data, offset + 8);
        if (length == 0) {
            return common::Status::invalidArgument(
                "missing range length must be greater than zero");
        }
        if (begin > std::numeric_limits<std::uint64_t>::max() - length) {
            return common::Status::invalidArgument("missing range overflows uint64");
        }
        payload.missingRanges.push_back(core::chunk::CompletedRange{begin, begin + length});
    }
    return payload;
}

common::Result<std::vector<std::uint8_t>> encodeChunkCompletePayload(
    const ChunkCompletePayload& payload) {
    if (payload.length == 0) {
        return common::Status::invalidArgument("chunk complete length must be greater than zero");
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(32);
    appendU64(encoded, payload.chunkId);
    appendU64(encoded, payload.offset);
    appendU64(encoded, payload.length);
    appendU16(encoded, static_cast<std::uint16_t>(payload.checksum.algorithm));
    appendU16(encoded, 0);
    appendU32(encoded, payload.checksum.value);
    return encoded;
}

common::Result<ChunkCompletePayload> decodeChunkCompletePayload(const std::uint8_t* data,
                                                                std::size_t size) {
    if (size != 32) {
        return common::Status::invalidArgument("invalid chunk complete payload size");
    }
    const std::uint16_t algorithm = readPayloadU16(data, 24);
    if (!isKnownChecksumAlgorithm(algorithm)) {
        return common::Status::invalidArgument("invalid chunk checksum algorithm");
    }
    if (readPayloadU16(data, 26) != 0) {
        return common::Status::invalidArgument("chunk complete reserved field must be zero");
    }

    ChunkCompletePayload payload;
    payload.chunkId = readPayloadU64(data, 0);
    payload.offset = readPayloadU64(data, 8);
    payload.length = readPayloadU64(data, 16);
    payload.checksum.algorithm = static_cast<checksum::ChecksumAlgorithm>(algorithm);
    payload.checksum.value = readPayloadU32(data, 28);
    if (payload.length == 0) {
        return common::Status::invalidArgument("chunk complete length must be greater than zero");
    }
    return payload;
}

}  // namespace gridflux::core::protocol
