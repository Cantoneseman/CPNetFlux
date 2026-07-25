#pragma once

#include <cstdint>
#include <string>

#include "gridflux/checksum/checksum.h"
#include "gridflux/common/status.h"
#include "gridflux/core/io/socket_utils.h"
#include "gridflux/core/io/tls_socket.h"
#include "gridflux/storage/file_io.h"

namespace gridflux::core::io {

struct FileDownloadSenderOptions {
    std::string path;
    std::string transferId;
    std::uint32_t connections = 1;
    std::uint64_t chunkSize = 1048576;
    std::uint32_t bufferSize = 65536;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto;
    storage::FileIoConfig fileIo;
    TlsConfig dataTls;
    DataTlsMode dataTlsMode = DataTlsMode::Off;
    bool resume = false;
    std::string sourcePath;
};

common::Status runFramedFileSenderOnListener(const FileDownloadSenderOptions& options,
                                             UniqueFd listener);

}  // namespace gridflux::core::io
