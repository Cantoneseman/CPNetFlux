#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"

namespace cpnetflux::core::session {

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

}  // namespace cpnetflux::core::session
