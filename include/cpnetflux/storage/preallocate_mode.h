#pragma once

#include <string>
#include <string_view>

#include "cpnetflux/common/status.h"

namespace cpnetflux::storage {

enum class PreallocateMode {
    Off,
    Full,
};

[[nodiscard]] common::Result<PreallocateMode> parsePreallocateMode(std::string_view text);
[[nodiscard]] std::string preallocateModeName(PreallocateMode mode);

}  // namespace cpnetflux::storage
