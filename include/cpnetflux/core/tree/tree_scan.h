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

inline constexpr std::size_t kMaxTreeDirectories = 65536;
inline constexpr std::size_t kMaxTreeDirectoryPathBytes = 4096;
inline constexpr std::size_t kMaxTreeDirectoryBytes = 16 * 1024 * 1024;
[[nodiscard]] common::Result<std::vector<std::string>> scanLocalDirectories(const std::string& root);
// Open every component relative to a directory fd; never follow symlinks.
[[nodiscard]] common::Status ensureTreeDirectory(const std::string& root, const std::string& relativePath);

[[nodiscard]] common::Status validateTreeRelativePath(const std::string& relativePath);
[[nodiscard]] bool isInternalTransferSidecar(const std::string& path);
[[nodiscard]] common::Result<std::vector<TreeFileInfo>> scanLocalTree(const std::string& root,
                                                               bool includeTransferSidecars = false);

}  // namespace cpnetflux::core::tree
