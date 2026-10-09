#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::tree {

struct TreeFileInfo {
    std::string relativePath;
    std::string fullPath;
    std::uint64_t size = 0;
    std::int64_t mtimeUnixSeconds = 0;
};

[[nodiscard]] common::Status validateTreeRelativePath(const std::string& relativePath);
[[nodiscard]] bool isInternalTransferSidecar(const std::string& path);
[[nodiscard]] common::Result<std::vector<TreeFileInfo>> scanLocalTree(const std::string& root,
                                                               bool includeTransferSidecars = false);

}  // namespace cpnetflux::core::tree
