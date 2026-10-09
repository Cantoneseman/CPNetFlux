#pragma once

#include <functional>
#include <vector>
#include "cpnetflux/core/io/persistent_data_session.h"
#include "cpnetflux/core/tree/tree_scan.h"

namespace cpnetflux::core::io {
struct PersistentTreeStats {
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t wireBytes = 0;  // Application frames, both directions; excludes TCP/IP.
    double completeWaitSeconds = 0;
};
using PersistentFileCallback = std::function<common::Status(
    const PersistentFileIdentity&, const common::Status&, bool complete)>;

[[nodiscard]] common::Result<std::vector<tree::TreeFileInfo>> scanPersistentTree(const std::string& root);

[[nodiscard]] common::Status sendPersistentTree(FramedDataSocket* socket,
    const std::string& root, const std::vector<PersistentFileIdentity>& files,
    PersistentTreeStats* stats, const PersistentFileCallback& callback = {});
[[nodiscard]] common::Status receivePersistentTree(FramedDataSocket* socket,
    const std::string& root, bool download, const std::string& remoteRoot,
    PersistentTreeStats* stats, const PersistentFileCallback& callback = {});
}  // namespace cpnetflux::core::io
