#include "cpnetflux/core/session/final_verify_policy.h"

namespace cpnetflux::core::session {

common::Result<FinalVerifyPolicy> parseFinalVerifyPolicy(std::string_view text) {
    if (text == "full") {
        return FinalVerifyPolicy::Full;
    }
    if (text == "verified_chunks") {
        return FinalVerifyPolicy::VerifiedChunks;
    }
    return common::Status::invalidArgument("final verify policy must be full or verified_chunks");
}

std::string finalVerifyPolicyName(FinalVerifyPolicy policy) {
    switch (policy) {
        case FinalVerifyPolicy::Full:
            return "full";
        case FinalVerifyPolicy::VerifiedChunks:
            return "verified_chunks";
    }
    return "full";
}

bool canUseVerifiedChunksFinalVerify(FinalVerifyPolicy requested,
                                     checksum::ChecksumAlgorithm algorithm,
                                     std::uint64_t totalSize, std::uint64_t verifiedBytes,
                                     bool hasMissingRanges) noexcept {
    return requested == FinalVerifyPolicy::VerifiedChunks &&
           algorithm != checksum::ChecksumAlgorithm::None && !hasMissingRanges &&
           verifiedBytes == totalSize;
}

bool canCommitWithVerifiedChunksFinalVerify(FinalVerifyPolicy requested,
                                            checksum::ChecksumAlgorithm algorithm,
                                            std::uint64_t totalSize,
                                            std::uint64_t verifiedBytes,
                                            bool hasMissingRanges,
                                            bool manifestFlushOk) noexcept {
    return manifestFlushOk && canUseVerifiedChunksFinalVerify(requested, algorithm, totalSize,
                                                              verifiedBytes, hasMissingRanges);
}

}  // namespace cpnetflux::core::session
