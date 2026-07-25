#include <nmmintrin.h>

#include <cstdint>
#include <cstring>

#include "gridflux/checksum/crc32c.h"

namespace gridflux::checksum {

bool crc32cHardwareAvailable() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("sse4.2");
#else
    return false;
#endif
}

std::uint32_t crc32cUpdateHardware(std::uint32_t state, const std::uint8_t* data,
                                   std::size_t size) noexcept {
    if (data == nullptr || size == 0) {
        return state;
    }
    if (!crc32cHardwareAvailable()) {
        return crc32cUpdateSoftware(state, data, size);
    }

#if defined(__x86_64__) || defined(_M_X64)
    std::uint64_t crc = state;
    while (size >= sizeof(std::uint64_t)) {
        std::uint64_t value = 0;
        std::memcpy(&value, data, sizeof(value));
        crc = _mm_crc32_u64(crc, value);
        data += sizeof(std::uint64_t);
        size -= sizeof(std::uint64_t);
    }
    std::uint32_t crc32 = static_cast<std::uint32_t>(crc);
#else
    std::uint32_t crc32 = state;
#endif

    while (size > 0) {
        crc32 = _mm_crc32_u8(crc32, *data);
        ++data;
        --size;
    }
    return crc32;
}

}  // namespace gridflux::checksum
