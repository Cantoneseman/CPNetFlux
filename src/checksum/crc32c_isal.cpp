#include "cpnetflux/checksum/crc32c.h"

#include <algorithm>
#include <climits>

#if CPNETFLUX_HAS_ISAL
#include <isa-l/crc.h>
#endif

namespace cpnetflux::checksum {

std::uint32_t crc32cUpdateIsal(std::uint32_t state, const std::uint8_t* data,
                               std::size_t size) noexcept {
#if CPNETFLUX_HAS_ISAL
    while (size != 0) {
        const auto chunk = std::min<std::size_t>(size, INT_MAX);
        state = crc32_iscsi(const_cast<unsigned char*>(data), static_cast<int>(chunk), state);
        data += chunk;
        size -= chunk;
    }
    return state;
#else
    return crc32cUpdateSoftware(state, data, size);
#endif
}

}  // namespace cpnetflux::checksum
