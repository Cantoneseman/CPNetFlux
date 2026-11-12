#pragma once
#include <functional>
#include <vector>
#include "cpnetflux/core/io/persistent_data_session.h"
#include "cpnetflux/core/tree/tree_manifest.h"
namespace cpnetflux::core::io {
// An explicitly negotiated preamble on a V3 data channel. Never a FILE_BEGIN.
using PersistentDirectoryCallback = std::function<common::Status(const tree::TreeDirectoryRecord&, bool)>;
struct PersistentDirectoryTransfer {
    std::vector<tree::TreeDirectoryRecord> entries;
    PersistentDirectoryCallback callback;
};
[[nodiscard]] common::Status sendPersistentDirectories(FramedDataSocket*,
    const PersistentDirectoryTransfer&, std::uint32_t channel, std::uint32_t channels,
    std::uint64_t attempt, std::uint64_t* wireBytes);
[[nodiscard]] common::Status receivePersistentDirectories(FramedDataSocket*, const std::string& root,
    const PersistentDirectoryTransfer&, std::uint32_t channel, std::uint32_t channels,
    std::uint64_t* wireBytes);
} // namespace cpnetflux::core::io
