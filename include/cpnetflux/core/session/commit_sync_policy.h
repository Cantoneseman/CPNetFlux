#pragma once

#include <string>
#include <string_view>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::session {

enum class CommitSyncPolicy {
    None,
    FsyncFile,
    FsyncFileAndDir,
};

[[nodiscard]] common::Result<CommitSyncPolicy> parseCommitSyncPolicy(std::string_view text);
[[nodiscard]] std::string commitSyncPolicyName(CommitSyncPolicy policy);

}  // namespace cpnetflux::core::session
