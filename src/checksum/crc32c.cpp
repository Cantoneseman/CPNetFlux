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

std::uint32_t crc32cFinalize(std::uint32_t state) noexcept { return state ^ 0xFFFFFFFFU; }

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size, ChecksumBackend backend) noexcept {
    return crc32cFinalize(crc32cUpdate(crc32cInitialState(), data, size, backend));
}

}  // namespace cpnetflux::checksum
