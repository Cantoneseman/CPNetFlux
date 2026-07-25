#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "gridflux/common/status.h"
#include "gridflux/core/io/socket_utils.h"

namespace gridflux::storage {

class PosixFile {
   public:
    PosixFile() = default;
    explicit PosixFile(core::io::UniqueFd fd) noexcept;

    PosixFile(const PosixFile&) = delete;
    PosixFile& operator=(const PosixFile&) = delete;

    PosixFile(PosixFile&&) noexcept = default;
    PosixFile& operator=(PosixFile&&) noexcept = default;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] int fd() const noexcept;

    static common::Result<PosixFile> openReadOnly(const std::string& path);
    static common::Result<PosixFile> openReadWrite(const std::string& path);
    static common::Result<PosixFile> openWriteOnly(const std::string& path);
    static common::Result<PosixFile> openReadWriteExclusive(const std::string& path);
    static common::Result<PosixFile> openWriteTruncate(const std::string& path);
    static common::Result<PosixFile> openWriteExclusive(const std::string& path);
    static common::Result<bool> pathExists(const std::string& path);
    static common::Status removePath(const std::string& path);
    static common::Status renamePath(const std::string& from, const std::string& to);
    static common::Status fsyncPath(const std::string& path);
    static common::Status fsyncParentDirectory(const std::string& path);

    common::Result<std::uint64_t> fileSize() const;
    common::Status sync() const;
    common::Status resize(std::uint64_t size) const;
    common::Status preallocate(std::uint64_t size) const;
    common::Result<std::size_t> readAt(std::uint64_t offset, std::uint8_t* data,
                                       std::size_t length) const;
    common::Status readAtAll(std::uint64_t offset, std::uint8_t* data, std::size_t length) const;
    common::Status writeAtAll(std::uint64_t offset, const std::uint8_t* data,
                              std::size_t length) const;

   private:
    core::io::UniqueFd fd_;
};

}  // namespace gridflux::storage
