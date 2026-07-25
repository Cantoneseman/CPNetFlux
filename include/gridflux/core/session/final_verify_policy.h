#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "gridflux/checksum/checksum.h"
#include "gridflux/common/status.h"

namespace gridflux::core::session {

enum class FinalVerifyPolicy {
    Full,
    VerifiedChunks,
};

[[nodiscard]] common::Result<FinalVerifyPolicy> parseFinalVerifyPolicy(std::string_view text);
[[nodiscard]] std::string finalVerifyPolicyName(FinalVerifyPolicy policy);
[[nodiscard]] bool canUseVerifiedChunksFinalVerify(FinalVerifyPolicy requested,
                                                   checksum::ChecksumAlgorithm algorithm,
                                                   std::uint64_t totalSize,
                                                   std::uint64_t verifiedBytes,
                                                   bool hasMissingRanges) noexcept;
[[nodiscard]] bool canCommitWithVerifiedChunksFinalVerify(FinalVerifyPolicy requested,
                                                          checksum::ChecksumAlgorithm algorithm,
                                                          std::uint64_t totalSize,
                                                          std::uint64_t verifiedBytes,
                                                          bool hasMissingRanges,
                                                          bool manifestFlushOk) noexcept;

}  // namespace gridflux::core::session
