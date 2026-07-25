#pragma once

#include <string>
#include <string_view>

#include "gridflux/common/status.h"

namespace gridflux::storage {

enum class PreallocateMode {
    Off,
    Full,
};

[[nodiscard]] common::Result<PreallocateMode> parsePreallocateMode(std::string_view text);
[[nodiscard]] std::string preallocateModeName(PreallocateMode mode);

}  // namespace gridflux::storage
