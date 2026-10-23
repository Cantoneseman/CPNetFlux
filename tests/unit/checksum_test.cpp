#include "cpnetflux/checksum/checksum.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "cpnetflux/checksum/crc32c.h"

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
