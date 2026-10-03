#pragma once

#include <cstdint>
#include <string>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/hot_path_compression.h"
#include "cpnetflux/core/io/socket_utils.h"
#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/core/io/transfer_runtime_metrics.h"
#include "cpnetflux/storage/file_io.h"

namespace cpnetflux::core::io {

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
    TransferRuntimeMetrics* runtimeMetrics = nullptr;
    HotPathCompressionOptions hotPathCompression;
    bool resume = false;
    std::string sourcePath;
};

common::Status runFramedFileSenderOnListener(const FileDownloadSenderOptions& options,
                                             UniqueFd listener, int controlFd = -1);

}  // namespace cpnetflux::core::io
