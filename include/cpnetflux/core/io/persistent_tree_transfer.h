#pragma once

#include <functional>
#include <cstdint>
#include <vector>
#include <optional>
#include <iosfwd>
#include "cpnetflux/core/io/dynamic_file_queue.h"
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

struct PersistentFileTiming {
    PersistentFileIdentity file;
    std::uint32_t channel = 0;
    bool sender = false;
    std::optional<double> queueWaitSeconds, firstPayloadSeconds;
    double readSeconds = 0, writeSeconds = 0, payloadIoSeconds = 0;
    double fileResultSeconds = 0, manifestSeconds = 0, checksumSeconds = 0,
        finalizeSeconds = 0, wallSeconds = 0;
};
void appendPersistentFileTimings(std::ostream& out, const std::vector<PersistentFileTiming>& files);
struct PersistentTreeStats {
    bool phaseTiming = false;
    std::vector<PersistentFileTiming> fileTimings;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t wireBytes = 0;  // Application frames, both directions; excludes TCP/IP.
    double completeWaitSeconds = 0;
    double queueWaitSeconds = 0;
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
    std::uint32_t channelCount = 1, DynamicFileQueue* ready = nullptr);
[[nodiscard]] common::Status receivePersistentTree(FramedDataSocket* socket,
    const std::string& root, bool download, const std::string& remoteRoot,
    PersistentTreeStats* stats, const PersistentFileCallback& callback = {},
    std::uint32_t pendingWindow = 1, std::uint32_t channelIndex = 0,
    std::uint32_t channelCount = 1,
    std::uint32_t expectedFileCount = kUnknownPersistentFileCount, bool dynamic = false,
    bool resume = false);
}  // namespace cpnetflux::core::io
