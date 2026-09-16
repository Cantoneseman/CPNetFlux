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

TEST(CompressedDataTest, RejectsNullAndEmptyInputs) {
    EXPECT_FALSE(cpnetflux::core::protocol::encodeCompressedDataPayload(nullptr, 1, 1024).isOk());
    EXPECT_FALSE(cpnetflux::core::protocol::encodeCompressedDataPayload(nullptr, 0, 1024).isOk());
    EXPECT_FALSE(cpnetflux::core::protocol::decodeCompressedDataPayload(nullptr, 0, 0, 1024)
                     .isOk());
}
