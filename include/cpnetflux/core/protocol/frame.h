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
inline constexpr std::uint16_t kRangeSkipped = 0x0002U;
inline constexpr std::uint16_t kFileResume = 0x0004U;
inline constexpr std::uint16_t kDataChecksum = 0x0008U;

enum class FrameType : std::uint16_t {
    Data = 1,
    Fin = 2,
    Complete = 3,
    Error = 4,
    ResumeResponse = 6,
    FileBegin = 8,
    FileEnd = 9,
    FileResult = 10,
    DirectoryEnd = 11,
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
    std::uint64_t rangeId = 0;
    std::uint64_t attempt = 0;
    FrameStatusCode statusCode = FrameStatusCode::Ok;
    std::uint64_t totalSize = 0;
};

enum class SessionMode : std::uint16_t {
    New = 1,
};

struct FileBeginMetadata {
    SessionMode mode = SessionMode::New;
    std::string transferId;
    std::uint64_t totalSize = 0;
    std::uint64_t chunkSize = 0;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    std::string sourcePath;
    std::uint64_t rangeId = 0;
    std::uint64_t rangeOffset = 0;
    std::uint64_t rangeLength = 0;
    std::uint64_t rangeCount = 0;
    std::uint64_t attempt = 0;
};

struct ResumeResponsePayload {
    FrameStatusCode statusCode = FrameStatusCode::Ok;
    std::vector<core::chunk::CompletedRange> missingRanges;
};

using EncodedFrameHeader = std::array<std::uint8_t, kFrameHeaderSize>;

EncodedFrameHeader encodeFrameHeader(const FrameHeader& header) noexcept;
common::Result<FrameHeader> decodeFrameHeader(const EncodedFrameHeader& encoded);
common::Status validateFrameHeader(const FrameHeader& header, std::uint32_t maxPayloadSize);
common::Status validateDataFramePayload(const FrameHeader& header, const std::uint8_t* payload,
                                        std::size_t payloadSize, std::uint32_t maxLogicalSize);

common::Result<std::vector<std::uint8_t>> encodeFileBeginMetadata(
    const FileBeginMetadata& payload);
common::Result<FileBeginMetadata> decodeFileBeginMetadata(const std::uint8_t* data,
                                                            std::size_t size);
common::Result<std::vector<std::uint8_t>> encodeResumeResponsePayload(
    const ResumeResponsePayload& payload);
common::Result<ResumeResponsePayload> decodeResumeResponsePayload(const std::uint8_t* data,
                                                                  std::size_t size);

}  // namespace cpnetflux::core::protocol
