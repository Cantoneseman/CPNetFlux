#include "gridflux/core/session/manifest_flush_policy.h"

namespace gridflux::core::session {

common::Result<ManifestFlushPolicy> parseManifestFlushPolicy(std::string_view text) {
    if (text == "every_n_chunks") {
        return ManifestFlushPolicy::EveryNChunks;
    }
    if (text == "final_only") {
        return ManifestFlushPolicy::FinalOnly;
    }
    return common::Status::invalidArgument(
        "manifest flush policy must be every_n_chunks or final_only");
}

std::string manifestFlushPolicyName(ManifestFlushPolicy policy) {
    switch (policy) {
        case ManifestFlushPolicy::EveryNChunks:
            return "every_n_chunks";
        case ManifestFlushPolicy::FinalOnly:
            return "final_only";
    }
    return "every_n_chunks";
}

}  // namespace gridflux::core::session
