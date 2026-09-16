#include "cpnetflux/checkpoint/manifest_store.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "cpnetflux/storage/posix_file.h"

namespace cpnetflux::checkpoint {
namespace {

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

common::Status writeAll(int fd, const char* data, std::size_t length) {
    std::size_t completed = 0;
    while (completed < length) {
        const ssize_t written = ::write(fd, data + completed, length - completed);
        if (written > 0) {
            completed += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0) {
            return systemStatus("write manifest", errno);
        }
        return common::Status::runtimeError("write manifest returned zero bytes");
    }
    return common::Status::ok();
}

}  // namespace

common::Status ManifestStore::saveAtomic(const std::string& path,
                                         const TransferManifest& manifest) {
    auto serialized = serializeTransferManifest(manifest);
    if (!serialized.isOk()) {
        return serialized.status();
    }

    const std::string tempPath = path + ".tmp." + std::to_string(::getpid());
    const int fd = ::open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return systemStatus("open manifest temp", errno);
    }

    const common::Status writeStatus =
        writeAll(fd, serialized.value().data(), serialized.value().size());
    if (!writeStatus.isOk()) {
        (void)::close(fd);
        (void)storage::PosixFile::removePath(tempPath);
        return writeStatus;
    }

    if (::close(fd) != 0) {
        const int closeError = errno;
        (void)storage::PosixFile::removePath(tempPath);
        return systemStatus("close manifest temp", closeError);
    }

    const common::Status renameStatus = storage::PosixFile::renamePath(tempPath, path);
    if (!renameStatus.isOk()) {
        (void)storage::PosixFile::removePath(tempPath);
        return renameStatus;
    }
    return common::Status::ok();
}

common::Result<TransferManifest> ManifestStore::load(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return common::Status::runtimeError("open manifest failed: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return common::Status::runtimeError("read manifest failed: " + path);
    }

    return parseTransferManifest(buffer.str());
}

}  // namespace cpnetflux::checkpoint
