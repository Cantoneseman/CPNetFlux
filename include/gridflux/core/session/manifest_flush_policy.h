#pragma once

#include <string>
#include <string_view>

#include "gridflux/common/status.h"

namespace gridflux::core::session {

enum class ManifestFlushPolicy {
    EveryNChunks,
    FinalOnly,
};

[[nodiscard]] common::Result<ManifestFlushPolicy> parseManifestFlushPolicy(
    std::string_view text);
[[nodiscard]] std::string manifestFlushPolicyName(ManifestFlushPolicy policy);

}  // namespace gridflux::core::session
