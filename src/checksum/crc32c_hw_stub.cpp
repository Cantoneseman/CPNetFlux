#include "cpnetflux/checksum/crc32c.h"

namespace cpnetflux::checksum {

bool crc32cHardwareAvailable() noexcept { return false; }

std::uint32_t crc32cUpdateHardware(std::uint32_t state, const std::uint8_t* data,
                                   std::size_t size) noexcept {
    return crc32cUpdateSoftware(state, data, size);
}

}  // namespace cpnetflux::checksum
