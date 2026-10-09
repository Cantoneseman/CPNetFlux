#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cpnetflux::core::io::detail {

// Internal identity captured when a tree pipeline candidate claims a manifest row.
// This is process-local state and is never serialized or exposed in summaries.
struct TreePipelineCandidateIdentity {
    std::size_t manifestIndex = 0;
    std::string relativePath;
    bool upload = false;
    std::uint64_t slotGeneration = 0;
    std::size_t slotIndex = 0;
};

[[nodiscard]] inline bool treePipelineCandidateIdentityMatches(
    const TreePipelineCandidateIdentity& identity, std::size_t manifestIndex,
    const std::string& relativePath, bool upload, std::uint64_t slotGeneration,
    std::size_t slotIndex = 0) noexcept {
    return identity.manifestIndex == manifestIndex && identity.relativePath == relativePath &&
           identity.upload == upload && identity.slotGeneration == slotGeneration &&
           identity.slotIndex == slotIndex;
}

}  // namespace cpnetflux::core::io::detail
