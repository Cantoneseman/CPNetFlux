#pragma once

#include <string>
#include <string_view>

#include "gridflux/common/status.h"

namespace gridflux::core::session {

enum class CommitSyncPolicy {
    None,
    FsyncFile,
    FsyncFileAndDir,
};

[[nodiscard]] common::Result<CommitSyncPolicy> parseCommitSyncPolicy(std::string_view text);
[[nodiscard]] std::string commitSyncPolicyName(CommitSyncPolicy policy);

}  // namespace gridflux::core::session
