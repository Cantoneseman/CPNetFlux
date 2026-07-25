#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "gridflux/common/status.h"

namespace gridflux::checksum {

enum class ChecksumAlgorithm : std::uint16_t {
    None = 0,
    Crc32c = 1,
};

enum class ChecksumBackend : std::uint16_t {
    Auto = 0,
    Software = 1,
    Hardware = 2,
};

struct ChecksumValue {
    ChecksumAlgorithm algorithm = ChecksumAlgorithm::None;
    std::uint32_t value = 0;
};

class ChecksumComputer {
   public:
    explicit ChecksumComputer(ChecksumAlgorithm algorithm,
                              ChecksumBackend backend = ChecksumBackend::Auto) noexcept;

    void reset() noexcept;
    void update(const std::uint8_t* data, std::size_t size) noexcept;
    [[nodiscard]] ChecksumValue finalize() const noexcept;
    [[nodiscard]] ChecksumAlgorithm algorithm() const noexcept;
    [[nodiscard]] ChecksumBackend backend() const noexcept;

   private:
    ChecksumAlgorithm algorithm_ = ChecksumAlgorithm::None;
    ChecksumBackend backend_ = ChecksumBackend::Software;
    std::uint32_t state_ = 0;
};

[[nodiscard]] const char* checksumAlgorithmName(ChecksumAlgorithm algorithm) noexcept;
[[nodiscard]] common::Result<ChecksumAlgorithm> parseChecksumAlgorithm(std::string_view text);
[[nodiscard]] const char* checksumBackendName(ChecksumBackend backend) noexcept;
[[nodiscard]] common::Result<ChecksumBackend> parseChecksumBackend(std::string_view text);
[[nodiscard]] common::Result<ChecksumBackend> resolveChecksumBackend(ChecksumAlgorithm algorithm,
                                                                     ChecksumBackend requested);

}  // namespace gridflux::checksum
