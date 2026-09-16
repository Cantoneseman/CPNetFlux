#pragma once

#include <cstddef>
#include <cstdint>

#include "cpnetflux/checksum/checksum.h"

namespace cpnetflux::checksum {

[[nodiscard]] std::uint32_t crc32cInitialState() noexcept;
[[nodiscard]] bool crc32cHardwareAvailable() noexcept;
[[nodiscard]] std::uint32_t crc32cUpdateSoftware(std::uint32_t state, const std::uint8_t* data,
                                                 std::size_t size) noexcept;
[[nodiscard]] std::uint32_t crc32cUpdateHardware(std::uint32_t state, const std::uint8_t* data,
                                                 std::size_t size) noexcept;
[[nodiscard]] std::uint32_t crc32cUpdate(std::uint32_t state, const std::uint8_t* data,
                                         std::size_t size,
                                         ChecksumBackend backend = ChecksumBackend::Auto) noexcept;
[[nodiscard]] std::uint32_t crc32cFinalize(std::uint32_t state) noexcept;
[[nodiscard]] std::uint32_t crc32c(const std::uint8_t* data, std::size_t size,
                                   ChecksumBackend backend = ChecksumBackend::Auto) noexcept;

}  // namespace cpnetflux::checksum
