#include "gridflux/core/protocol/frame.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "gridflux/checksum/checksum.h"

TEST(FrameTest, EncodesHeaderInNetworkByteOrder) {
    gridflux::core::protocol::FrameHeader header;
    header.type = gridflux::core::protocol::FrameType::Data;
    header.streamId = 0x01020304U;
    header.chunkId = 0x0102030405060708ULL;
    header.offset = 0x1112131415161718ULL;
    header.payloadSize = 0x21222324U;
    header.totalSize = 0x3132333435363738ULL;

    const auto encoded = gridflux::core::protocol::encodeFrameHeader(header);

    EXPECT_EQ(encoded[0], 0x47);
    EXPECT_EQ(encoded[1], 0x46);
    EXPECT_EQ(encoded[2], 0x58);
    EXPECT_EQ(encoded[3], 0x31);
    EXPECT_EQ(encoded[4], 0x00);
    EXPECT_EQ(encoded[5], 0x01);
    EXPECT_EQ(encoded[6], 0x00);
    EXPECT_EQ(encoded[7], 0x40);
    EXPECT_EQ(encoded[12], 0x01);
    EXPECT_EQ(encoded[13], 0x02);
    EXPECT_EQ(encoded[14], 0x03);
    EXPECT_EQ(encoded[15], 0x04);
    EXPECT_EQ(encoded[16], 0x01);
    EXPECT_EQ(encoded[23], 0x08);
    EXPECT_EQ(encoded[40], 0x31);
    EXPECT_EQ(encoded[47], 0x38);
}

TEST(FrameTest, RoundTripsValidHeader) {
    gridflux::core::protocol::FrameHeader header;
    header.type = gridflux::core::protocol::FrameType::Data;
    header.streamId = 3;
    header.chunkId = 7;
    header.offset = 4096;
    header.payloadSize = 1024;
    header.totalSize = 8192;

    const auto decoded = gridflux::core::protocol::decodeFrameHeader(
        gridflux::core::protocol::encodeFrameHeader(header));

    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value().magic, gridflux::core::protocol::kFrameMagic);
    EXPECT_EQ(decoded.value().version, gridflux::core::protocol::kFrameVersion);
    EXPECT_EQ(decoded.value().headerSize, gridflux::core::protocol::kFrameHeaderSize);
    EXPECT_EQ(decoded.value().type, gridflux::core::protocol::FrameType::Data);
    EXPECT_EQ(decoded.value().streamId, 3U);
    EXPECT_EQ(decoded.value().chunkId, 7U);
    EXPECT_EQ(decoded.value().offset, 4096U);
    EXPECT_EQ(decoded.value().payloadSize, 1024U);
    EXPECT_EQ(decoded.value().statusCode, gridflux::core::protocol::FrameStatusCode::Ok);
    EXPECT_EQ(decoded.value().totalSize, 8192U);
}

TEST(FrameTest, RoundTripsErrorStatusHeader) {
    gridflux::core::protocol::FrameHeader header;
    header.type = gridflux::core::protocol::FrameType::Error;
    header.statusCode = gridflux::core::protocol::FrameStatusCode::MissingRange;
    header.totalSize = 8192;

    const auto decoded = gridflux::core::protocol::decodeFrameHeader(
        gridflux::core::protocol::encodeFrameHeader(header));

    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value().type, gridflux::core::protocol::FrameType::Error);
    EXPECT_EQ(decoded.value().statusCode, gridflux::core::protocol::FrameStatusCode::MissingRange);
    EXPECT_EQ(decoded.value().totalSize, 8192U);
}

TEST(FrameTest, ValidatesDataFinAndFinalStatusFrames) {
    gridflux::core::protocol::FrameHeader data;
    data.type = gridflux::core::protocol::FrameType::Data;
    data.offset = 10;
    data.payloadSize = 5;
    data.totalSize = 20;

    EXPECT_TRUE(gridflux::core::protocol::validateFrameHeader(data, 64).isOk());

    gridflux::core::protocol::FrameHeader fin;
    fin.type = gridflux::core::protocol::FrameType::Fin;
    fin.payloadSize = 0;

    EXPECT_TRUE(gridflux::core::protocol::validateFrameHeader(fin, 64).isOk());

    gridflux::core::protocol::FrameHeader complete;
    complete.type = gridflux::core::protocol::FrameType::Complete;
    complete.statusCode = gridflux::core::protocol::FrameStatusCode::Ok;
    EXPECT_TRUE(gridflux::core::protocol::validateFrameHeader(complete, 64).isOk());

    gridflux::core::protocol::FrameHeader error;
    error.type = gridflux::core::protocol::FrameType::Error;
    error.statusCode = gridflux::core::protocol::FrameStatusCode::WriteFailed;
    EXPECT_TRUE(gridflux::core::protocol::validateFrameHeader(error, 64).isOk());

    gridflux::core::protocol::FrameHeader sessionInit;
    sessionInit.type = gridflux::core::protocol::FrameType::SessionInit;
    sessionInit.payloadSize = 32;
    EXPECT_TRUE(gridflux::core::protocol::validateFrameHeader(sessionInit, 64).isOk());

    gridflux::core::protocol::FrameHeader resumeResponse;
    resumeResponse.type = gridflux::core::protocol::FrameType::ResumeResponse;
    resumeResponse.payloadSize = 8;
    EXPECT_TRUE(gridflux::core::protocol::validateFrameHeader(resumeResponse, 64).isOk());

    gridflux::core::protocol::FrameHeader chunkComplete;
    chunkComplete.type = gridflux::core::protocol::FrameType::ChunkComplete;
    chunkComplete.payloadSize = 32;
    EXPECT_TRUE(gridflux::core::protocol::validateFrameHeader(chunkComplete, 64).isOk());
}

TEST(FrameTest, RejectsInvalidHeaders) {
    gridflux::core::protocol::FrameHeader badMagic;
    badMagic.magic = 1;
    EXPECT_FALSE(gridflux::core::protocol::validateFrameHeader(badMagic, 64).isOk());

    gridflux::core::protocol::FrameHeader tooLarge;
    tooLarge.payloadSize = 65;
    tooLarge.totalSize = 65;
    EXPECT_FALSE(gridflux::core::protocol::validateFrameHeader(tooLarge, 64).isOk());

    gridflux::core::protocol::FrameHeader outOfRange;
    outOfRange.offset = 10;
    outOfRange.payloadSize = 11;
    outOfRange.totalSize = 20;
    EXPECT_FALSE(gridflux::core::protocol::validateFrameHeader(outOfRange, 64).isOk());

    gridflux::core::protocol::FrameHeader finWithPayload;
    finWithPayload.type = gridflux::core::protocol::FrameType::Fin;
    finWithPayload.payloadSize = 1;
    EXPECT_FALSE(gridflux::core::protocol::validateFrameHeader(finWithPayload, 64).isOk());

    gridflux::core::protocol::FrameHeader dataWithStatus;
    dataWithStatus.statusCode = gridflux::core::protocol::FrameStatusCode::WriteFailed;
    EXPECT_FALSE(gridflux::core::protocol::validateFrameHeader(dataWithStatus, 64).isOk());

    gridflux::core::protocol::FrameHeader completeWithError;
    completeWithError.type = gridflux::core::protocol::FrameType::Complete;
    completeWithError.statusCode = gridflux::core::protocol::FrameStatusCode::InternalError;
    EXPECT_FALSE(gridflux::core::protocol::validateFrameHeader(completeWithError, 64).isOk());

    gridflux::core::protocol::FrameHeader errorWithOk;
    errorWithOk.type = gridflux::core::protocol::FrameType::Error;
    errorWithOk.statusCode = gridflux::core::protocol::FrameStatusCode::Ok;
    EXPECT_FALSE(gridflux::core::protocol::validateFrameHeader(errorWithOk, 64).isOk());
}

TEST(FrameTest, RejectsUnknownTypeDuringDecode) {
    gridflux::core::protocol::FrameHeader header;
    auto encoded = gridflux::core::protocol::encodeFrameHeader(header);
    encoded[8] = 0x00;
    encoded[9] = 0x63;

    const auto decoded = gridflux::core::protocol::decodeFrameHeader(encoded);

    EXPECT_FALSE(decoded.isOk());
}

TEST(SessionControlTest, EncodesAndDecodesSessionInitPayload) {
    gridflux::core::protocol::SessionInitPayload payload;
    payload.mode = gridflux::core::protocol::SessionMode::Resume;
    payload.transferId = "phase2a-smoke";
    payload.totalSize = 4096;
    payload.chunkSize = 1024;
    payload.checksumAlgorithm = gridflux::checksum::ChecksumAlgorithm::Crc32c;

    const auto encoded = gridflux::core::protocol::encodeSessionInitPayload(payload);
    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();

    const auto decoded = gridflux::core::protocol::decodeSessionInitPayload(encoded.value().data(),
                                                                            encoded.value().size());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value().mode, gridflux::core::protocol::SessionMode::Resume);
    EXPECT_EQ(decoded.value().transferId, "phase2a-smoke");
    EXPECT_EQ(decoded.value().totalSize, 4096U);
    EXPECT_EQ(decoded.value().chunkSize, 1024U);
    EXPECT_EQ(decoded.value().checksumAlgorithm, gridflux::checksum::ChecksumAlgorithm::Crc32c);
    EXPECT_TRUE(decoded.value().sourcePath.empty());
}

TEST(SessionControlTest, EncodesAndDecodesSessionInitSourcePathExtension) {
    gridflux::core::protocol::SessionInitPayload payload;
    payload.mode = gridflux::core::protocol::SessionMode::Resume;
    payload.transferId = "phase3c-download";
    payload.sourcePath = "nested/source.bin";
    payload.totalSize = 8192;
    payload.chunkSize = 1024;
    payload.checksumAlgorithm = gridflux::checksum::ChecksumAlgorithm::Crc32c;

    const auto encoded = gridflux::core::protocol::encodeSessionInitPayload(payload);
    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();

    const auto decoded = gridflux::core::protocol::decodeSessionInitPayload(encoded.value().data(),
                                                                            encoded.value().size());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value().mode, gridflux::core::protocol::SessionMode::Resume);
    EXPECT_EQ(decoded.value().transferId, "phase3c-download");
    EXPECT_EQ(decoded.value().sourcePath, "nested/source.bin");
    EXPECT_EQ(decoded.value().totalSize, 8192U);
    EXPECT_EQ(decoded.value().chunkSize, 1024U);
}

TEST(SessionControlTest, EncodesAndDecodesResumeResponsePayload) {
    gridflux::core::protocol::ResumeResponsePayload payload;
    payload.statusCode = gridflux::core::protocol::FrameStatusCode::Ok;
    payload.missingRanges = {{0, 1024}, {2048, 4096}};

    const auto encoded = gridflux::core::protocol::encodeResumeResponsePayload(payload);
    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();

    const auto decoded = gridflux::core::protocol::decodeResumeResponsePayload(
        encoded.value().data(), encoded.value().size());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value().statusCode, gridflux::core::protocol::FrameStatusCode::Ok);
    ASSERT_EQ(decoded.value().missingRanges.size(), 2U);
    EXPECT_EQ(decoded.value().missingRanges[1].begin, 2048U);
    EXPECT_EQ(decoded.value().missingRanges[1].end, 4096U);
}

TEST(SessionControlTest, EncodesAndDecodesChunkCompletePayload) {
    gridflux::core::protocol::ChunkCompletePayload payload;
    payload.chunkId = 7;
    payload.offset = 1048576;
    payload.length = 1048576;
    payload.checksum = gridflux::checksum::ChecksumValue{
        gridflux::checksum::ChecksumAlgorithm::Crc32c,
        0xe3069283U,
    };

    const auto encoded = gridflux::core::protocol::encodeChunkCompletePayload(payload);
    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();
    ASSERT_EQ(encoded.value().size(), 32U);

    const auto decoded = gridflux::core::protocol::decodeChunkCompletePayload(
        encoded.value().data(), encoded.value().size());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value().chunkId, 7U);
    EXPECT_EQ(decoded.value().offset, 1048576U);
    EXPECT_EQ(decoded.value().length, 1048576U);
    EXPECT_EQ(decoded.value().checksum.algorithm, gridflux::checksum::ChecksumAlgorithm::Crc32c);
    EXPECT_EQ(decoded.value().checksum.value, 0xe3069283U);
}

TEST(SessionControlTest, RejectsInvalidControlPayloads) {
    EXPECT_FALSE(gridflux::core::protocol::decodeSessionInitPayload(nullptr, 0).isOk());

    std::vector<std::uint8_t> invalidResumePayload(8);
    invalidResumePayload[7] = 1;
    EXPECT_FALSE(gridflux::core::protocol::decodeResumeResponsePayload(invalidResumePayload.data(),
                                                                       invalidResumePayload.size())
                     .isOk());

    std::vector<std::uint8_t> invalidChunkComplete(32);
    invalidChunkComplete[25] = 99;
    EXPECT_FALSE(gridflux::core::protocol::decodeChunkCompletePayload(invalidChunkComplete.data(),
                                                                      invalidChunkComplete.size())
                     .isOk());
}
