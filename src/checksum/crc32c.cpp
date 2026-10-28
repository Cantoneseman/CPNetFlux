#include "cpnetflux/checksum/crc32c.h"

#include <array>

namespace cpnetflux::checksum {
namespace {

constexpr std::uint32_t kCrc32cPolynomial = 0x82F63B78U;

constexpr std::array<std::uint32_t, 256> makeTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < table.size(); ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) {
            if ((value & 1U) != 0U) {
                value = (value >> 1U) ^ kCrc32cPolynomial;
            } else {
                value >>= 1U;
            }
        }
        table[index] = value;
    }
    return table;
}

constexpr auto kCrc32cTable = makeTable();

using Gf2Matrix = std::array<std::uint32_t, 32>;

std::uint32_t matrixTimes(const Gf2Matrix& matrix, std::uint32_t vector) noexcept {
    std::uint32_t sum = 0;
    std::size_t index = 0;
    while (vector != 0) {
        if ((vector & 1U) != 0U) sum ^= matrix[index];
        vector >>= 1U;
        ++index;
    }
    return sum;
}

void matrixSquare(Gf2Matrix& square, const Gf2Matrix& matrix) noexcept {
    for (std::size_t index = 0; index < square.size(); ++index)
        square[index] = matrixTimes(matrix, matrix[index]);
}

constexpr std::size_t kCrc32cCombinePowerCount = 64;
using Crc32cBytePowers = std::array<Gf2Matrix, kCrc32cCombinePowerCount>;

Crc32cBytePowers makeCrc32cBytePowers() noexcept {
    Crc32cBytePowers powers{};
    Gf2Matrix odd{}, even{};
    odd[0] = kCrc32cPolynomial;
    std::uint32_t row = 1;
    for (std::size_t index = 1; index < odd.size(); ++index) {
        odd[index] = row;
        row <<= 1U;
    }
    // These squares reach the operator for appending one zero byte.
    matrixSquare(even, odd);
    matrixSquare(odd, even);
    matrixSquare(even, odd);
    powers[0] = even;
    for (std::size_t index = 1; index < powers.size(); ++index)
        matrixSquare(powers[index], powers[index - 1]);
    return powers;
}

const Crc32cBytePowers& crc32cBytePowers() noexcept {
    static const auto powers = makeCrc32cBytePowers();
    return powers;
}
}  // namespace

std::uint32_t crc32cInitialState() noexcept { return 0xFFFFFFFFU; }

std::uint32_t crc32cUpdateSoftware(std::uint32_t state, const std::uint8_t* data,
                                   std::size_t size) noexcept {
    if (data == nullptr || size == 0) {
        return state;
    }
    std::uint32_t value = state;
    for (std::size_t index = 0; index < size; ++index) {
        value = kCrc32cTable[(value ^ data[index]) & 0xFFU] ^ (value >> 8U);
    }
    return value;
}

std::uint32_t crc32cUpdate(std::uint32_t state, const std::uint8_t* data, std::size_t size,
                           ChecksumBackend backend) noexcept {
    switch (backend) {
        case ChecksumBackend::Hardware:
            return crc32cUpdateHardware(state, data, size);
        case ChecksumBackend::Auto:
            if (crc32cHardwareAvailable()) {
                return crc32cUpdateHardware(state, data, size);
            }
            return crc32cUpdateSoftware(state, data, size);
        case ChecksumBackend::Software:
            return crc32cUpdateSoftware(state, data, size);
    }
    return crc32cUpdateSoftware(state, data, size);
}


std::uint32_t crc32cCombine(std::uint32_t left, std::uint32_t right,
                            std::uint64_t rightLength) noexcept {
    if (rightLength == 0) return left;

    const auto& powers = crc32cBytePowers();
    std::size_t power = 0;
    while (rightLength != 0) {
        if ((rightLength & 1U) != 0U) left = matrixTimes(powers[power], left);
        rightLength >>= 1U;
        ++power;
    }
    return left ^ right;
}
std::uint32_t crc32cFinalize(std::uint32_t state) noexcept { return state ^ 0xFFFFFFFFU; }

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size, ChecksumBackend backend) noexcept {
    return crc32cFinalize(crc32cUpdate(crc32cInitialState(), data, size, backend));
}

}  // namespace cpnetflux::checksum
