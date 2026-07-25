#include "gridflux/core/tree/tree_scan.h"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace gridflux::core::tree {
namespace {

bool hasControlCharacter(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::iscntrl(ch) != 0;
    });
}

bool hasWindowsDrivePrefix(const std::string& value) {
    return value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) != 0 &&
           value[1] == ':';
}

common::Result<std::int64_t> mtimeOfPath(const std::filesystem::path& path) {
    struct stat statBuffer {};
    if (::stat(path.c_str(), &statBuffer) != 0) {
        return common::Status::systemError("stat: " + std::string(std::strerror(errno)), errno);
    }
    return static_cast<std::int64_t>(statBuffer.st_mtime);
}

}  // namespace

common::Status validateTreeRelativePath(const std::string& relativePath) {
    if (relativePath.empty()) {
        return common::Status::invalidArgument("tree relative path must not be empty");
    }
    if (relativePath.front() == '/' || relativePath.front() == '\\') {
        return common::Status::invalidArgument("tree relative path must be relative");
    }
    if (relativePath.find('\\') != std::string::npos) {
        return common::Status::invalidArgument("tree relative path must use forward slashes");
    }
    if (hasControlCharacter(relativePath)) {
        return common::Status::invalidArgument("tree relative path contains control character");
    }
    if (hasWindowsDrivePrefix(relativePath)) {
        return common::Status::invalidArgument("tree relative path must not use drive prefix");
    }

    const std::filesystem::path path(relativePath);
    for (const std::filesystem::path& part : path) {
        const std::string text = part.string();
        if (text.empty() || text == "." || text == "..") {
            return common::Status::invalidArgument("tree relative path contains invalid segment");
        }
    }
    return common::Status::ok();
}

common::Result<std::vector<TreeFileInfo>> scanLocalTree(const std::string& root) {
    std::error_code error;
    const std::filesystem::path rootPath(root);
    if (!std::filesystem::exists(rootPath, error) || error) {
        return common::Status::invalidArgument("tree root does not exist");
    }
    if (!std::filesystem::is_directory(rootPath, error) || error) {
        return common::Status::invalidArgument("tree root is not a directory");
    }

    std::vector<TreeFileInfo> files;
    std::filesystem::recursive_directory_iterator iterator(
        rootPath, std::filesystem::directory_options::none, error);
    if (error) {
        return common::Status::systemError("tree scan failed: " + error.message(),
                                           error.value());
    }
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            return common::Status::systemError("tree scan failed: " + error.message(),
                                               error.value());
        }
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_symlink(error)) {
            return common::Status::invalidArgument("tree scan rejects symlink: " +
                                                   entry.path().string());
        }
        if (error) {
            return common::Status::systemError("tree entry type failed: " + error.message(),
                                               error.value());
        }
        if (entry.is_directory(error)) {
            continue;
        }
        if (error) {
            return common::Status::systemError("tree entry type failed: " + error.message(),
                                               error.value());
        }
        if (!entry.is_regular_file(error)) {
            return common::Status::invalidArgument("tree scan rejects non-regular file: " +
                                                   entry.path().string());
        }
        if (error) {
            return common::Status::systemError("tree entry type failed: " + error.message(),
                                               error.value());
        }

        const std::filesystem::path relative = std::filesystem::relative(entry.path(), rootPath, error);
        if (error) {
            return common::Status::systemError("tree relative path failed: " + error.message(),
                                               error.value());
        }
        std::string relativeText = relative.generic_string();
        const common::Status pathStatus = validateTreeRelativePath(relativeText);
        if (!pathStatus.isOk()) {
            return pathStatus;
        }
        const std::uint64_t size = entry.file_size(error);
        if (error) {
            return common::Status::systemError("tree file size failed: " + error.message(),
                                               error.value());
        }
        auto mtime = mtimeOfPath(entry.path());
        if (!mtime.isOk()) {
            return mtime.status();
        }
        files.push_back(TreeFileInfo{relativeText, entry.path().string(), size, mtime.value()});
    }

    std::sort(files.begin(), files.end(), [](const TreeFileInfo& left, const TreeFileInfo& right) {
        return left.relativePath < right.relativePath;
    });
    return files;
}

}  // namespace gridflux::core::tree
