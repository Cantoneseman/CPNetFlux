#pragma once

#include <string>
#include <string_view>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::session {

enum class ManifestFlushPolicy {
    EveryNChunks,
    FinalOnly,
};

[[nodiscard]] common::Result<ManifestFlushPolicy> parseManifestFlushPolicy(
    std::string_view text);
[[nodiscard]] std::string manifestFlushPolicyName(ManifestFlushPolicy policy);

}  // namespace cpnetflux::core::session
