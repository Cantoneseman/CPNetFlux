#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/chunk/range_list.h"

namespace cpnetflux::core::protocol {

inline constexpr std::uint32_t kFrameMagic = 0x47465831U;
inline constexpr std::uint16_t kFrameVersion = 1;
inline constexpr std::uint16_t kFrameHeaderSize = 64;
inline constexpr std::uint16_t kDataCompressed = 0x0001U;

enum class FrameType : std::uint16_t {
    Data = 1,
    Fin = 2,
    Complete = 3,
    Error = 4,
    SessionInit = 5,
    ResumeResponse = 6,
    ChunkComplete = 7,
};

enum class FrameStatusCode : std::uint32_t {
    Ok = 0,
    InvalidFrame = 1,
    WriteFailed = 2,
    SizeMismatch = 3,
    DuplicateRange = 4,
    RangeOutOfBounds = 5,
    MissingRange = 6,
    OutputExists = 7,
    InternalError = 8,
    ChecksumMismatch = 9,
    ChecksumUnsupported = 10,
    ManifestCorrupt = 11,
};

struct FrameHeader {
    std::uint32_t magic = kFrameMagic;
    std::uint16_t version = kFrameVersion;
    std::uint16_t headerSize = kFrameHeaderSize;
    FrameType type = FrameType::Data;
    std::uint16_t flags = 0;
    std::uint32_t streamId = 0;
    std::uint64_t chunkId = 0;
    std::uint64_t offset = 0;
    std::uint32_t payloadSize = 0;
    FrameStatusCode statusCode = FrameStatusCode::Ok;
    std::uint64_t totalSize = 0;
};

enum class SessionMode : std::uint16_t {
    New = 1,
    Resume = 2,
};

struct SessionInitPayload {
    SessionMode mode = SessionMode::New;
    std::string transferId;
    std::uint64_t totalSize = 0;
    std::uint64_t chunkSize = 0;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    std::string sourcePath;
};

struct ResumeResponsePayload {
    FrameStatusCode statusCode = FrameStatusCode::Ok;
    std::vector<core::chunk::CompletedRange> missingRanges;
};

struct ChunkCompletePayload {
    std::uint64_t chunkId = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    checksum::ChecksumValue checksum;
};

using EncodedFrameHeader = std::array<std::uint8_t, kFrameHeaderSize>;

EncodedFrameHeader encodeFrameHeader(const FrameHeader& header) noexcept;
common::Result<FrameHeader> decodeFrameHeader(const EncodedFrameHeader& encoded);
common::Status validateFrameHeader(const FrameHeader& header, std::uint32_t maxPayloadSize);
common::Status validateDataFramePayload(const FrameHeader& header, const std::uint8_t* payload,
                                        std::size_t payloadSize, std::uint32_t maxLogicalSize);

common::Result<std::vector<std::uint8_t>> encodeSessionInitPayload(
    const SessionInitPayload& payload);
common::Result<SessionInitPayload> decodeSessionInitPayload(const std::uint8_t* data,
                                                            std::size_t size);
common::Result<std::vector<std::uint8_t>> encodeResumeResponsePayload(
    const ResumeResponsePayload& payload);
common::Result<ResumeResponsePayload> decodeResumeResponsePayload(const std::uint8_t* data,
                                                                  std::size_t size);
common::Result<std::vector<std::uint8_t>> encodeChunkCompletePayload(
    const ChunkCompletePayload& payload);
common::Result<ChunkCompletePayload> decodeChunkCompletePayload(const std::uint8_t* data,
                                                                std::size_t size);

}  // namespace cpnetflux::core::protocol
