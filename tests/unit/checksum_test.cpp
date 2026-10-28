#include "cpnetflux/checksum/checksum.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "cpnetflux/checksum/crc32c.h"

namespace {

using Gf2Matrix = std::array<std::uint32_t, 32>;

std::uint32_t referenceMatrixTimes(const Gf2Matrix& matrix, std::uint32_t vector) {
    std::uint32_t sum = 0;
    std::size_t index = 0;
    while (vector != 0) {
        if ((vector & 1U) != 0U) sum ^= matrix[index];
        vector >>= 1U;
        ++index;
    }
    return sum;
}

void referenceMatrixSquare(Gf2Matrix& square, const Gf2Matrix& matrix) {
    for (std::size_t index = 0; index < square.size(); ++index)
        square[index] = referenceMatrixTimes(matrix, matrix[index]);
}

std::uint32_t referenceCrc32cCombine(std::uint32_t left, std::uint32_t right,
                                    std::uint64_t rightLength) {
    constexpr std::uint32_t polynomial = 0x82F63B78U;
    if (rightLength == 0) return left;
    Gf2Matrix odd{}, even{};
    odd[0] = polynomial;
    std::uint32_t row = 1;
    for (std::size_t index = 1; index < odd.size(); ++index) {
        odd[index] = row;
        row <<= 1U;
    }
    referenceMatrixSquare(even, odd);
    referenceMatrixSquare(odd, even);
    do {
        referenceMatrixSquare(even, odd);
        if ((rightLength & 1U) != 0U) left = referenceMatrixTimes(even, left);
        rightLength >>= 1U;
        if (rightLength == 0) break;
        referenceMatrixSquare(odd, even);
        if ((rightLength & 1U) != 0U) left = referenceMatrixTimes(odd, left);
        rightLength >>= 1U;
    } while (rightLength != 0);
    return left ^ right;
}

}  // namespace

TEST(ChecksumTest, Crc32cMatchesKnownVectors) {
    cpnetflux::checksum::ChecksumComputer empty(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    EXPECT_EQ(empty.finalize().value, 0x00000000U);

    constexpr std::array<std::uint8_t, 9> input{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    cpnetflux::checksum::ChecksumComputer computer(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    computer.update(input.data(), input.size());
    EXPECT_EQ(computer.finalize().value, 0xE3069283U);
}

TEST(ChecksumTest, Crc32cSupportsMultipleUpdates) {
    const std::vector<std::uint8_t> input{'g', 'r', 'i', 'd', 'f', 'l', 'u', 'x'};

    cpnetflux::checksum::ChecksumComputer oneShot(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    oneShot.update(input.data(), input.size());

    cpnetflux::checksum::ChecksumComputer split(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    split.update(input.data(), 3);
    split.update(input.data() + 3, input.size() - 3);

    EXPECT_EQ(split.finalize().value, oneShot.finalize().value);
}

TEST(ChecksumTest, Crc32cHandlesLargeChunkedInput) {
    std::vector<std::uint8_t> input(1024 * 1024);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = static_cast<std::uint8_t>(index % 251U);
    }

    cpnetflux::checksum::ChecksumComputer oneShot(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    oneShot.update(input.data(), input.size());

    cpnetflux::checksum::ChecksumComputer chunked(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    for (std::size_t offset = 0; offset < input.size(); offset += 7777) {
        const std::size_t size = std::min<std::size_t>(7777, input.size() - offset);
        chunked.update(input.data() + offset, size);
    }

    EXPECT_EQ(chunked.finalize().value, oneShot.finalize().value);
}

TEST(ChecksumTest, SoftwareHardwareAndAutoBackendsAgreeWhenAvailable) {
    const std::vector<std::uint8_t> input(1024 * 1024, 17);

    cpnetflux::checksum::ChecksumComputer software(cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                  cpnetflux::checksum::ChecksumBackend::Software);
    software.update(input.data(), input.size());

    auto autoBackend = cpnetflux::checksum::resolveChecksumBackend(
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c, cpnetflux::checksum::ChecksumBackend::Auto);
    ASSERT_TRUE(autoBackend.isOk()) << autoBackend.status().message();

    cpnetflux::checksum::ChecksumComputer automatic(cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                   autoBackend.value());
    automatic.update(input.data(), input.size());
    EXPECT_EQ(automatic.finalize().value, software.finalize().value);

    auto hardwareBackend =
        cpnetflux::checksum::resolveChecksumBackend(cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                   cpnetflux::checksum::ChecksumBackend::Hardware);
    if (cpnetflux::checksum::crc32cHardwareAvailable()) {
        ASSERT_TRUE(hardwareBackend.isOk()) << hardwareBackend.status().message();
        cpnetflux::checksum::ChecksumComputer hardware(cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                      hardwareBackend.value());
        hardware.update(input.data(), input.size());
        EXPECT_EQ(hardware.finalize().value, software.finalize().value);
        EXPECT_EQ(autoBackend.value(), cpnetflux::checksum::ChecksumBackend::Hardware);
    } else {
        EXPECT_FALSE(hardwareBackend.isOk());
        EXPECT_EQ(autoBackend.value(), cpnetflux::checksum::ChecksumBackend::Software);
    }
}

TEST(ChecksumTest, NoneAlwaysFinalizesToZero) {
    const std::vector<std::uint8_t> input{'d', 'a', 't', 'a'};
    cpnetflux::checksum::ChecksumComputer computer(cpnetflux::checksum::ChecksumAlgorithm::None);

    computer.update(input.data(), input.size());

    EXPECT_EQ(computer.finalize().algorithm, cpnetflux::checksum::ChecksumAlgorithm::None);
    EXPECT_EQ(computer.finalize().value, 0U);
}

TEST(ChecksumTest, ParsesAlgorithms) {
    auto crc = cpnetflux::checksum::parseChecksumAlgorithm("crc32c");
    ASSERT_TRUE(crc.isOk()) << crc.status().message();
    EXPECT_EQ(crc.value(), cpnetflux::checksum::ChecksumAlgorithm::Crc32c);

    auto none = cpnetflux::checksum::parseChecksumAlgorithm("none");
    ASSERT_TRUE(none.isOk()) << none.status().message();
    EXPECT_EQ(none.value(), cpnetflux::checksum::ChecksumAlgorithm::None);

    EXPECT_FALSE(cpnetflux::checksum::parseChecksumAlgorithm("sha256").isOk());

    auto backend = cpnetflux::checksum::parseChecksumBackend("auto");
    ASSERT_TRUE(backend.isOk()) << backend.status().message();
    EXPECT_EQ(backend.value(), cpnetflux::checksum::ChecksumBackend::Auto);
    EXPECT_FALSE(cpnetflux::checksum::parseChecksumBackend("fast").isOk());
}

TEST(ChecksumTest, Crc32cCombineMatchesWholeInputAtBoundaryLengths) {
    using cpnetflux::checksum::ChecksumBackend;
    using cpnetflux::checksum::ChecksumComputer;
    using cpnetflux::checksum::ChecksumAlgorithm;
    using cpnetflux::checksum::crc32cCombine;

    std::vector<std::uint8_t> input(4U * 1024U * 1024U + 37U);
    for (std::size_t index = 0; index < input.size(); ++index)
        input[index] = static_cast<std::uint8_t>((index * 31U + 17U) % 251U);

    const auto crcOf = [&](std::size_t offset, std::size_t length) {
        ChecksumComputer computer(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
        computer.update(input.data() + offset, length);
        return computer.finalize().value;
    };
    const auto whole = crcOf(0, input.size());
    const std::array<std::size_t, 13> splits{
        0, 1, 2, 7, 63, 64, 65535, 65536, 65537,
        4U * 1024U * 1024U - 1U, 4U * 1024U * 1024U,
        input.size() - 1U, input.size()};
    for (const auto split : splits) {
        ASSERT_LE(split, input.size());
        EXPECT_EQ(crc32cCombine(crcOf(0, split), crcOf(split, input.size() - split),
                                input.size() - split), whole)
            << "split=" << split;
    }
}


TEST(ChecksumTest, AccumulatorCombinesFinalizedFrameChecksums) {
    using cpnetflux::checksum::ChecksumAlgorithm;
    using cpnetflux::checksum::ChecksumBackend;
    using cpnetflux::checksum::ChecksumComputer;

    std::vector<std::uint8_t> input(131173);
    for (std::size_t index = 0; index < input.size(); ++index)
        input[index] = static_cast<std::uint8_t>((index * 17U + 9U) % 251U);
    ChecksumComputer expected(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
    expected.update(input.data(), input.size());
    ChecksumComputer combined(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);

    const std::array<std::size_t, 6> lengths{1, 63, 65536, 17, 65536, 20};
    std::size_t offset = 0;
    for (const auto length : lengths) {
        ChecksumComputer frame(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
        frame.update(input.data() + offset, length);
        ASSERT_TRUE(combined.combine(frame.finalize(), length));
        offset += length;
    }
    ASSERT_EQ(offset, input.size());
    EXPECT_EQ(combined.finalize().value, expected.finalize().value);
}

TEST(ChecksumTest, AccumulatorRejectsMismatchedChecksumWithoutMutation) {
    using cpnetflux::checksum::ChecksumAlgorithm;
    using cpnetflux::checksum::ChecksumComputer;
    using cpnetflux::checksum::ChecksumValue;

    const std::uint8_t bytes[]{'o', 'k'};
    ChecksumComputer computer(ChecksumAlgorithm::Crc32c);
    computer.update(bytes, sizeof(bytes));
    const auto before = computer.finalize();

    EXPECT_FALSE(computer.combine(ChecksumValue{ChecksumAlgorithm::None, 0}, 2));
    EXPECT_EQ(computer.finalize().value, before.value);
    ChecksumComputer none(ChecksumAlgorithm::None);
    EXPECT_TRUE(none.combine(ChecksumValue{ChecksumAlgorithm::None, 0}, 2));
    EXPECT_FALSE(none.combine(ChecksumValue{ChecksumAlgorithm::None, 1}, 2));
}
TEST(ChecksumTest, AccumulatorCombinesBetweenRawUpdatesAndResets) {
    using cpnetflux::checksum::ChecksumAlgorithm;
    using cpnetflux::checksum::ChecksumBackend;
    using cpnetflux::checksum::ChecksumComputer;

    std::vector<std::uint8_t> input(65539, 0x5a);
    ChecksumComputer expected(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
    expected.update(input.data(), input.size());
    for (const auto backend : {ChecksumBackend::Software, ChecksumBackend::Auto}) {
        ChecksumComputer combined(ChecksumAlgorithm::Crc32c, backend);
        combined.update(input.data(), 3);
        ChecksumComputer frame(ChecksumAlgorithm::Crc32c, backend);
        frame.update(input.data() + 3, 65532);
        ASSERT_TRUE(combined.combine(frame.finalize(), 65532));
        const auto beforeEmpty = combined.finalize().value;
        ChecksumComputer empty(ChecksumAlgorithm::Crc32c, backend);
        ASSERT_TRUE(combined.combine(empty.finalize(), 0));
        EXPECT_EQ(combined.finalize().value, beforeEmpty);
        combined.update(input.data() + 65535, 4);
        EXPECT_EQ(combined.finalize().value, expected.finalize().value);
        combined.reset();
        EXPECT_EQ(combined.finalize().value, 0U);
        ASSERT_TRUE(combined.combine(expected.finalize(), input.size()));
        EXPECT_EQ(combined.finalize().value, expected.finalize().value);
    }
}

TEST(ChecksumTest, Crc32cCombineSupportsMultipleUnevenSegments) {
    using cpnetflux::checksum::ChecksumBackend;
    using cpnetflux::checksum::ChecksumComputer;
    using cpnetflux::checksum::ChecksumAlgorithm;

    std::vector<std::uint8_t> input(131173);
    for (std::size_t index = 0; index < input.size(); ++index)
        input[index] = static_cast<std::uint8_t>((index * 13U + 5U) % 251U);
    ChecksumComputer whole(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
    whole.update(input.data(), input.size());

    std::uint32_t combined = 0;
    std::size_t offset = 0;
    const std::array<std::size_t, 6> lengths{1, 63, 65536, 17, 65536, 20};
    for (const auto length : lengths) {
        ChecksumComputer part(ChecksumAlgorithm::Crc32c, ChecksumBackend::Software);
        part.update(input.data() + offset, length);
        combined = cpnetflux::checksum::crc32cCombine(combined, part.finalize().value, length);
        offset += length;
    }
    ASSERT_EQ(offset, input.size());
    EXPECT_EQ(combined, whole.finalize().value);
}
TEST(ChecksumTest, Crc32cCombineMatchesReferenceAcross64BitLengths) {
    using cpnetflux::checksum::crc32cCombine;
    constexpr std::array<std::uint64_t, 10> lengths{
        0, 1, 2, 255, 65536, 4U * 1024U * 1024U, 1ULL << 32U,
        (1ULL << 32U) + 37U, 1ULL << 63U, std::numeric_limits<std::uint64_t>::max()};
    constexpr std::array<std::uint32_t, 3> crcs{0, 0xE3069283U, 0x12345678U};
    for (const auto length : lengths) {
        for (const auto left : crcs) {
            for (const auto right : crcs) {
                EXPECT_EQ(crc32cCombine(left, right, length),
                          referenceCrc32cCombine(left, right, length))
                    << "rightLength=" << length << " left=" << left << " right=" << right;
            }
        }
    }
}

TEST(ChecksumTest, Crc32cCombineRepeated4MiBLengthBenchmark) {
    using cpnetflux::checksum::crc32cCombine;
    constexpr std::uint64_t rightLength = 4U * 1024U * 1024U;
    constexpr std::uint32_t right = 0xA5C39E17U;
    constexpr int iterations = 1024;
    std::uint32_t value = 0x13579BDFU;
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < iterations; ++index)
        value = crc32cCombine(value, right, rightLength);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    std::uint32_t reference = 0x13579BDFU;
    for (int index = 0; index < iterations; ++index)
        reference = referenceCrc32cCombine(reference, right, rightLength);
    EXPECT_EQ(value, reference);
    EXPECT_GT(value, 0U);
    std::cout << "crc32c_combine_benchmark iterations=" << iterations
              << " right_length=" << rightLength
              << " elapsed_us=" << elapsed.count() << '\n';
}

TEST(ChecksumTest, Crc32cCombine4GiBFrameCountBenchmark) {
    using cpnetflux::checksum::crc32cCombine;
    constexpr std::uint64_t totalBytes = 4ULL * 1024U * 1024U * 1024U;
    constexpr std::uint64_t frameBytes = 65532;
    constexpr std::uint64_t fullFrames = totalBytes / frameBytes;
    constexpr std::uint64_t tailBytes = totalBytes % frameBytes;
    constexpr std::uint32_t right = 0xA5C39E17U;

    std::uint32_t value = 0x13579BDFU;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t frame = 0; frame < fullFrames; ++frame)
        value = crc32cCombine(value, right, frameBytes);
    if constexpr (tailBytes != 0)
        value = crc32cCombine(value, right, tailBytes);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);

    std::uint32_t reference = 0x13579BDFU;
    for (std::uint64_t frame = 0; frame < fullFrames; ++frame)
        reference = referenceCrc32cCombine(reference, right, frameBytes);
    if constexpr (tailBytes != 0)
        reference = referenceCrc32cCombine(reference, right, tailBytes);
    EXPECT_EQ(value, reference);
    std::cout << "crc32c_combine_4gib_benchmark frames="
              << fullFrames + (tailBytes != 0 ? 1 : 0)
              << " frame_bytes=" << frameBytes << " elapsed_us=" << elapsed.count() << '\n';
}
