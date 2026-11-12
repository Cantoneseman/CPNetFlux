#pragma once

#include <cstdint>

namespace cpnetflux::core::session {

// Optional caller-owned observation, used only while the session is serialized.
struct VerifiedChunkRecordStats {
    std::uint64_t recordIdQueries = 0;
    std::uint64_t recordIdCandidatesExamined = 0;
    std::uint64_t lookupCalls = 0;
    std::uint64_t candidateRecordsExamined = 0;
    double lookupSeconds = 0;
    double sortSeconds = 0;
};

}  // namespace cpnetflux::core::session
