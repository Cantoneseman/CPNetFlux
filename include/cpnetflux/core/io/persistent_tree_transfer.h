#pragma once

#include <functional>
#include <cstdint>
#include <vector>
#include "cpnetflux/core/io/persistent_data_session.h"
#include "cpnetflux/core/tree/tree_scan.h"

namespace cpnetflux::core::io {
inline constexpr std::uint32_t kPersistentTreeMaxChannels = 8;
inline constexpr std::uint32_t kUnknownPersistentFileCount = UINT32_MAX;

[[nodiscard]] inline bool persistentFileAssignedToChannel(
    std::uint32_t fileId, std::uint32_t channelIndex, std::uint32_t channelCount) noexcept {
    return fileId != 0 && channelCount != 0 && channelIndex < channelCount &&
        (fileId - 1) % channelCount == channelIndex;
}

struct PersistentTreeStats {
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t wireBytes = 0;  // Application frames, both directions; excludes TCP/IP.
    double completeWaitSeconds = 0;
    std::uint32_t pendingWindow = 1;
    std::uint32_t pendingHighWatermark = 0;
};
using PersistentFileCallback = std::function<common::Status(
    const PersistentFileIdentity&, const common::Status&, bool complete)>;

[[nodiscard]] common::Result<std::vector<tree::TreeFileInfo>> scanPersistentTree(const std::string& root);

[[nodiscard]] common::Status sendPersistentTree(FramedDataSocket* socket,
    const std::string& root, const std::vector<PersistentFileIdentity>& files,
    PersistentTreeStats* stats, const PersistentFileCallback& callback = {},
    std::uint32_t pendingWindow = 1, std::uint32_t channelIndex = 0,
    std::uint32_t channelCount = 1);
[[nodiscard]] common::Status receivePersistentTree(FramedDataSocket* socket,
    const std::string& root, bool download, const std::string& remoteRoot,
    PersistentTreeStats* stats, const PersistentFileCallback& callback = {},
    std::uint32_t pendingWindow = 1, std::uint32_t channelIndex = 0,
    std::uint32_t channelCount = 1,
    std::uint32_t expectedFileCount = kUnknownPersistentFileCount);
}  // namespace cpnetflux::core::io
