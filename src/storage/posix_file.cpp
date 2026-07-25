#include "gridflux/storage/posix_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

namespace gridflux::storage {
namespace {

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

}  // namespace

PosixFile::PosixFile(core::io::UniqueFd fd) noexcept : fd_(std::move(fd)) {}

bool PosixFile::isValid() const noexcept { return fd_.isValid(); }

int PosixFile::fd() const noexcept { return fd_.get(); }

common::Result<PosixFile> PosixFile::openReadOnly(const std::string& path) {
    core::io::UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.isValid()) {
        return systemStatus("open read-only", errno);
    }
    return PosixFile(std::move(fd));
}

common::Result<PosixFile> PosixFile::openReadWrite(const std::string& path) {
    core::io::UniqueFd fd(::open(path.c_str(), O_RDWR | O_CLOEXEC));
    if (!fd.isValid()) {
        return systemStatus("open read-write", errno);
    }
    return PosixFile(std::move(fd));
}

common::Result<PosixFile> PosixFile::openWriteOnly(const std::string& path) {
    core::io::UniqueFd fd(::open(path.c_str(), O_WRONLY | O_CLOEXEC));
    if (!fd.isValid()) {
        return systemStatus("open write-only", errno);
    }
    return PosixFile(std::move(fd));
}

common::Result<PosixFile> PosixFile::openReadWriteExclusive(const std::string& path) {
    core::io::UniqueFd fd(::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644));
    if (!fd.isValid()) {
        return systemStatus("open read-write-exclusive", errno);
    }
    return PosixFile(std::move(fd));
}

common::Result<PosixFile> PosixFile::openWriteTruncate(const std::string& path) {
    core::io::UniqueFd fd(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
    if (!fd.isValid()) {
        return systemStatus("open write-truncate", errno);
    }
    return PosixFile(std::move(fd));
}

common::Result<PosixFile> PosixFile::openWriteExclusive(const std::string& path) {
    core::io::UniqueFd fd(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644));
    if (!fd.isValid()) {
        return systemStatus("open write-exclusive", errno);
    }
    return PosixFile(std::move(fd));
}

common::Result<bool> PosixFile::pathExists(const std::string& path) {
    struct stat statBuffer {};
    if (::stat(path.c_str(), &statBuffer) == 0) {
        return true;
    }
    if (errno == ENOENT) {
        return false;
    }
    return systemStatus("stat", errno);
}

common::Status PosixFile::removePath(const std::string& path) {
    if (::unlink(path.c_str()) == 0 || errno == ENOENT) {
        return common::Status::ok();
    }
    return systemStatus("unlink", errno);
}

common::Status PosixFile::renamePath(const std::string& from, const std::string& to) {
    if (::rename(from.c_str(), to.c_str()) != 0) {
        return systemStatus("rename", errno);
    }
    return common::Status::ok();
}

common::Status PosixFile::fsyncPath(const std::string& path) {
    auto file = openReadOnly(path);
    if (!file.isOk()) {
        return file.status();
    }
    return file.value().sync();
}

common::Status PosixFile::fsyncParentDirectory(const std::string& path) {
    const std::filesystem::path parent =
        std::filesystem::path(path).parent_path().empty()
            ? std::filesystem::path(".")
            : std::filesystem::path(path).parent_path();
    core::io::UniqueFd fd(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!fd.isValid()) {
        return systemStatus("open parent directory", errno);
    }
    if (::fsync(fd.get()) != 0) {
        return systemStatus("fsync parent directory", errno);
    }
    return common::Status::ok();
}

common::Result<std::uint64_t> PosixFile::fileSize() const {
    struct stat statBuffer {};
    if (::fstat(fd_.get(), &statBuffer) != 0) {
        return systemStatus("fstat", errno);
    }
    if (statBuffer.st_size < 0) {
        return common::Status::runtimeError("file size is negative");
    }
    return static_cast<std::uint64_t>(statBuffer.st_size);
}

common::Status PosixFile::sync() const {
    if (!isValid()) {
        return common::Status::invalidArgument("file is not open");
    }
    if (::fsync(fd_.get()) != 0) {
        return systemStatus("fsync", errno);
    }
    return common::Status::ok();
}

common::Status PosixFile::resize(std::uint64_t size) const {
    if (size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return common::Status::invalidArgument("file size exceeds off_t range");
    }
    if (::ftruncate(fd_.get(), static_cast<off_t>(size)) != 0) {
        return systemStatus("ftruncate", errno);
    }
    return common::Status::ok();
}

common::Status PosixFile::preallocate(std::uint64_t size) const {
    if (size == 0) {
        return common::Status::ok();
    }
    if (size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return common::Status::invalidArgument("preallocate size exceeds off_t range");
    }
    const int result = ::posix_fallocate(fd_.get(), 0, static_cast<off_t>(size));
    if (result != 0) {
        return systemStatus("posix_fallocate", result);
    }
    return common::Status::ok();
}

common::Result<std::size_t> PosixFile::readAt(std::uint64_t offset, std::uint8_t* data,
                                              std::size_t length) const {
    if (length == 0) {
        return std::size_t{0};
    }
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return common::Status::invalidArgument("read offset exceeds off_t range");
    }

    while (true) {
        const ssize_t bytes = ::pread(fd_.get(), data, length, static_cast<off_t>(offset));
        if (bytes >= 0) {
            return static_cast<std::size_t>(bytes);
        }
        if (errno == EINTR) {
            continue;
        }
        return systemStatus("pread", errno);
    }
}

common::Status PosixFile::readAtAll(std::uint64_t offset, std::uint8_t* data,
                                    std::size_t length) const {
    std::size_t completed = 0;
    while (completed < length) {
        auto result = readAt(offset + completed, data + completed, length - completed);
        if (!result.isOk()) {
            return result.status();
        }
        if (result.value() == 0) {
            return common::Status::runtimeError("unexpected EOF while reading file");
        }
        completed += result.value();
    }
    return common::Status::ok();
}

common::Status PosixFile::writeAtAll(std::uint64_t offset, const std::uint8_t* data,
                                     std::size_t length) const {
    if (length == 0) {
        return common::Status::ok();
    }
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return common::Status::invalidArgument("write offset exceeds off_t range");
    }

    std::size_t completed = 0;
    while (completed < length) {
        const std::uint64_t currentOffset = offset + completed;
        if (currentOffset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
            return common::Status::invalidArgument("write offset exceeds off_t range");
        }
        const ssize_t bytes = ::pwrite(fd_.get(), data + completed, length - completed,
                                       static_cast<off_t>(currentOffset));
        if (bytes > 0) {
            completed += static_cast<std::size_t>(bytes);
            continue;
        }
        if (bytes < 0 && errno == EINTR) {
            continue;
        }
        if (bytes < 0) {
            return systemStatus("pwrite", errno);
        }
        return common::Status::runtimeError("pwrite returned zero bytes");
    }
    return common::Status::ok();
}

}  // namespace gridflux::storage
