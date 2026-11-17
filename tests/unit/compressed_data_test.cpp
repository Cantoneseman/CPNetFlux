#include "cpnetflux/core/protocol/compressed_data.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> compressibleData(std::size_t size) {
    const std::string pattern = "CPNetFlux-Phase3-lossless-compression\n";
    std::vector<std::uint8_t> data;
    data.reserve(size);
    while (data.size() < size) {
        const std::size_t remaining = size - data.size();
        data.insert(data.end(), pattern.begin(),
                    pattern.begin() + static_cast<std::ptrdiff_t>(
                                           std::min(remaining, pattern.size())));
    }
    return data;
}

}  // namespace

TEST(CompressedDataTest, RoundTripsWithNetworkOrderLengthPrefix) {
    const std::vector<std::uint8_t> raw = compressibleData(131072);
    const auto encoded = cpnetflux::core::protocol::encodeCompressedDataPayload(
        raw.data(), raw.size(), 65536);

    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();
    ASSERT_GT(encoded.value().size(),
              cpnetflux::core::protocol::kCompressedDataPrefixSize);
    EXPECT_EQ(encoded.value()[0], 0x00);
    EXPECT_EQ(encoded.value()[1], 0x00);
    EXPECT_EQ(encoded.value()[2], 0x00);
    EXPECT_EQ(encoded.value()[3], 0x00);
    EXPECT_EQ(encoded.value()[4], 0x00);
    EXPECT_EQ(encoded.value()[5], 0x02);
    EXPECT_EQ(encoded.value()[6], 0x00);
    EXPECT_EQ(encoded.value()[7], 0x00);

    const auto decoded = cpnetflux::core::protocol::decodeCompressedDataPayload(
        encoded.value().data(), encoded.value().size(), raw.size(), raw.size());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value(), raw);
}

TEST(CompressedDataTest, RejectsTruncatedMalformedAndLengthMismatchPayloads) {
    const std::vector<std::uint8_t> raw = compressibleData(4096);
    const auto encoded = cpnetflux::core::protocol::encodeCompressedDataPayload(
        raw.data(), raw.size(), 16384);
    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();

    EXPECT_FALSE(cpnetflux::core::protocol::compressedDataPayloadLogicalLength(
                     encoded.value().data(), cpnetflux::core::protocol::kCompressedDataPrefixSize)
                     .isOk());
    EXPECT_FALSE(cpnetflux::core::protocol::decodeCompressedDataPayload(
                     encoded.value().data(), encoded.value().size() - 1, raw.size(), raw.size())
                     .isOk());
    EXPECT_FALSE(cpnetflux::core::protocol::decodeCompressedDataPayload(
                     encoded.value().data(), encoded.value().size(), raw.size() - 1, raw.size())
                     .isOk());

    std::vector<std::uint8_t> corrupted = encoded.value();
    corrupted.back() ^= 0xFFU;
    EXPECT_FALSE(cpnetflux::core::protocol::decodeCompressedDataPayload(
                     corrupted.data(), corrupted.size(), raw.size(), raw.size())
                     .isOk());
}

TEST(CompressedDataTest, EnforcesOutputAndPayloadLimits) {
    const std::vector<std::uint8_t> raw = compressibleData(32768);
    const auto tooSmallOutput = cpnetflux::core::protocol::encodeCompressedDataPayload(
        raw.data(), raw.size(), 8);
    EXPECT_FALSE(tooSmallOutput.isOk());

    const auto encoded = cpnetflux::core::protocol::encodeCompressedDataPayload(
        raw.data(), raw.size(), 65536);
    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();
    EXPECT_FALSE(cpnetflux::core::protocol::decodeCompressedDataPayload(
                     encoded.value().data(), encoded.value().size(), raw.size(), 1024)
                     .isOk());
}

class CompressedDataCodecTest
    : public ::testing::TestWithParam<cpnetflux::core::protocol::CompressionCodec> {};

TEST_P(CompressedDataCodecTest, RoundTripsAndTagsCodecInPrefix) {
    using namespace cpnetflux::core::protocol;
    const CompressionCodec codec = GetParam();
    if ((supportedCompressionCodecMask() & compressionCodecBit(codec)) == 0) {
        GTEST_SKIP() << compressionCodecName(codec) << " is not built in";
    }
    const std::vector<std::uint8_t> raw = compressibleData(262144);
    const auto encoded = encodeCompressedDataPayload(raw.data(), raw.size(), raw.size(), codec);
    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();
    EXPECT_LT(encoded.value().size(), raw.size());
    EXPECT_EQ(encoded.value()[0], static_cast<std::uint8_t>(codec));

    const auto parsedCodec = compressedDataPayloadCodec(encoded.value().data(), encoded.value().size());
    ASSERT_TRUE(parsedCodec.isOk());
    EXPECT_EQ(parsedCodec.value(), codec);
    const auto length =
        compressedDataPayloadLogicalLength(encoded.value().data(), encoded.value().size());
    ASSERT_TRUE(length.isOk());
    EXPECT_EQ(length.value(), raw.size());

    const auto decoded =
        decodeCompressedDataPayload(encoded.value().data(), encoded.value().size(), raw.size(), raw.size());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value(), raw);

    std::vector<std::uint8_t> corrupted = encoded.value();
    corrupted[corrupted.size() / 2] ^= 0xFFU;
    corrupted.back() ^= 0xFFU;
    const auto corruptDecoded =
        decodeCompressedDataPayload(corrupted.data(), corrupted.size(), raw.size(), raw.size());
    EXPECT_TRUE(!corruptDecoded.isOk() || corruptDecoded.value() != raw);
}

INSTANTIATE_TEST_SUITE_P(AllCodecs, CompressedDataCodecTest,
                         ::testing::Values(cpnetflux::core::protocol::CompressionCodec::Zlib,
                                           cpnetflux::core::protocol::CompressionCodec::Zstd,
                                           cpnetflux::core::protocol::CompressionCodec::Lz4));

TEST(CompressedDataTest, RejectsUnknownCodecTag) {
    using namespace cpnetflux::core::protocol;
    const std::vector<std::uint8_t> raw = compressibleData(4096);
    auto encoded = encodeCompressedDataPayload(raw.data(), raw.size(), 16384);
    ASSERT_TRUE(encoded.isOk());
    encoded.value()[0] = 0x7F;
    EXPECT_FALSE(compressedDataPayloadCodec(encoded.value().data(), encoded.value().size()).isOk());
    EXPECT_FALSE(compressedDataPayloadLogicalLength(encoded.value().data(), encoded.value().size()).isOk());
    EXPECT_FALSE(decodeCompressedDataPayload(encoded.value().data(), encoded.value().size(), raw.size(),
                                             raw.size())
                     .isOk());
}

TEST(CompressedDataTest, ParsesCodecNames) {
    using namespace cpnetflux::core::protocol;
    EXPECT_EQ(parseCompressionCodec("zlib"), CompressionCodec::Zlib);
    EXPECT_EQ(parseCompressionCodec("zstd"), CompressionCodec::Zstd);
    EXPECT_EQ(parseCompressionCodec("lz4"), CompressionCodec::Lz4);
    EXPECT_FALSE(parseCompressionCodec("brotli").has_value());
    EXPECT_NE(supportedCompressionCodecMask() & compressionCodecBit(CompressionCodec::Zlib), 0U);
}

TEST(CompressedDataTest, RejectsNullAndEmptyInputs) {
    EXPECT_FALSE(cpnetflux::core::protocol::encodeCompressedDataPayload(nullptr, 1, 1024).isOk());
    EXPECT_FALSE(cpnetflux::core::protocol::encodeCompressedDataPayload(nullptr, 0, 1024).isOk());
    EXPECT_FALSE(cpnetflux::core::protocol::decodeCompressedDataPayload(nullptr, 0, 0, 1024)
                     .isOk());
}
