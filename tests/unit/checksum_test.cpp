#include "gridflux/checksum/checksum.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "gridflux/checksum/crc32c.h"

TEST(ChecksumTest, Crc32cMatchesKnownVectors) {
    gridflux::checksum::ChecksumComputer empty(gridflux::checksum::ChecksumAlgorithm::Crc32c);
    EXPECT_EQ(empty.finalize().value, 0x00000000U);

    constexpr std::array<std::uint8_t, 9> input{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    gridflux::checksum::ChecksumComputer computer(gridflux::checksum::ChecksumAlgorithm::Crc32c);
    computer.update(input.data(), input.size());
    EXPECT_EQ(computer.finalize().value, 0xE3069283U);
}

TEST(ChecksumTest, Crc32cSupportsMultipleUpdates) {
    const std::vector<std::uint8_t> input{'g', 'r', 'i', 'd', 'f', 'l', 'u', 'x'};

    gridflux::checksum::ChecksumComputer oneShot(gridflux::checksum::ChecksumAlgorithm::Crc32c);
    oneShot.update(input.data(), input.size());

    gridflux::checksum::ChecksumComputer split(gridflux::checksum::ChecksumAlgorithm::Crc32c);
    split.update(input.data(), 3);
    split.update(input.data() + 3, input.size() - 3);

    EXPECT_EQ(split.finalize().value, oneShot.finalize().value);
}

TEST(ChecksumTest, Crc32cHandlesLargeChunkedInput) {
    std::vector<std::uint8_t> input(1024 * 1024);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = static_cast<std::uint8_t>(index % 251U);
    }

    gridflux::checksum::ChecksumComputer oneShot(gridflux::checksum::ChecksumAlgorithm::Crc32c);
    oneShot.update(input.data(), input.size());

    gridflux::checksum::ChecksumComputer chunked(gridflux::checksum::ChecksumAlgorithm::Crc32c);
    for (std::size_t offset = 0; offset < input.size(); offset += 7777) {
        const std::size_t size = std::min<std::size_t>(7777, input.size() - offset);
        chunked.update(input.data() + offset, size);
    }

    EXPECT_EQ(chunked.finalize().value, oneShot.finalize().value);
}

TEST(ChecksumTest, SoftwareHardwareAndAutoBackendsAgreeWhenAvailable) {
    const std::vector<std::uint8_t> input(1024 * 1024, 17);

    gridflux::checksum::ChecksumComputer software(gridflux::checksum::ChecksumAlgorithm::Crc32c,
                                                  gridflux::checksum::ChecksumBackend::Software);
    software.update(input.data(), input.size());

    auto autoBackend = gridflux::checksum::resolveChecksumBackend(
        gridflux::checksum::ChecksumAlgorithm::Crc32c, gridflux::checksum::ChecksumBackend::Auto);
    ASSERT_TRUE(autoBackend.isOk()) << autoBackend.status().message();

    gridflux::checksum::ChecksumComputer automatic(gridflux::checksum::ChecksumAlgorithm::Crc32c,
                                                   autoBackend.value());
    automatic.update(input.data(), input.size());
    EXPECT_EQ(automatic.finalize().value, software.finalize().value);

    auto hardwareBackend =
        gridflux::checksum::resolveChecksumBackend(gridflux::checksum::ChecksumAlgorithm::Crc32c,
                                                   gridflux::checksum::ChecksumBackend::Hardware);
    if (gridflux::checksum::crc32cHardwareAvailable()) {
        ASSERT_TRUE(hardwareBackend.isOk()) << hardwareBackend.status().message();
        gridflux::checksum::ChecksumComputer hardware(gridflux::checksum::ChecksumAlgorithm::Crc32c,
                                                      hardwareBackend.value());
        hardware.update(input.data(), input.size());
        EXPECT_EQ(hardware.finalize().value, software.finalize().value);
        EXPECT_EQ(autoBackend.value(), gridflux::checksum::ChecksumBackend::Hardware);
    } else {
        EXPECT_FALSE(hardwareBackend.isOk());
        EXPECT_EQ(autoBackend.value(), gridflux::checksum::ChecksumBackend::Software);
    }
}

TEST(ChecksumTest, NoneAlwaysFinalizesToZero) {
    const std::vector<std::uint8_t> input{'d', 'a', 't', 'a'};
    gridflux::checksum::ChecksumComputer computer(gridflux::checksum::ChecksumAlgorithm::None);

    computer.update(input.data(), input.size());

    EXPECT_EQ(computer.finalize().algorithm, gridflux::checksum::ChecksumAlgorithm::None);
    EXPECT_EQ(computer.finalize().value, 0U);
}

TEST(ChecksumTest, ParsesAlgorithms) {
    auto crc = gridflux::checksum::parseChecksumAlgorithm("crc32c");
    ASSERT_TRUE(crc.isOk()) << crc.status().message();
    EXPECT_EQ(crc.value(), gridflux::checksum::ChecksumAlgorithm::Crc32c);

    auto none = gridflux::checksum::parseChecksumAlgorithm("none");
    ASSERT_TRUE(none.isOk()) << none.status().message();
    EXPECT_EQ(none.value(), gridflux::checksum::ChecksumAlgorithm::None);

    EXPECT_FALSE(gridflux::checksum::parseChecksumAlgorithm("sha256").isOk());

    auto backend = gridflux::checksum::parseChecksumBackend("auto");
    ASSERT_TRUE(backend.isOk()) << backend.status().message();
    EXPECT_EQ(backend.value(), gridflux::checksum::ChecksumBackend::Auto);
    EXPECT_FALSE(gridflux::checksum::parseChecksumBackend("fast").isOk());
}
