#include "cpnetflux/core/session/commit_sync_policy.h"

namespace cpnetflux::core::session {

common::Result<CommitSyncPolicy> parseCommitSyncPolicy(std::string_view text) {
    if (text == "none") {
        return CommitSyncPolicy::None;
    }
    if (text == "fsync_file") {
        return CommitSyncPolicy::FsyncFile;
    }
    if (text == "fsync_file_and_dir") {
        return CommitSyncPolicy::FsyncFileAndDir;
    }
    return common::Status::invalidArgument(
        "commit sync policy must be none, fsync_file, or fsync_file_and_dir");
}

std::string commitSyncPolicyName(CommitSyncPolicy policy) {
    switch (policy) {
        case CommitSyncPolicy::None:
            return "none";
        case CommitSyncPolicy::FsyncFile:
            return "fsync_file";
        case CommitSyncPolicy::FsyncFileAndDir:
            return "fsync_file_and_dir";
    }
    return "none";
}

}  // namespace cpnetflux::core::session
