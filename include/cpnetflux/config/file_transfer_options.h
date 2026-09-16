#pragma once

#include <cstdint>
#include <string>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/hot_path_compression.h"
#include "cpnetflux/core/session/commit_sync_policy.h"
#include "cpnetflux/core/session/final_verify_policy.h"
#include "cpnetflux/core/session/manifest_flush_policy.h"
#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/core/io/transfer_runtime_metrics.h"
#include "cpnetflux/storage/file_io.h"
#include "cpnetflux/storage/preallocate_mode.h"

namespace cpnetflux::config {

enum class FileTransferRole {
    Server,
    Client,
};

struct FileTransferOptions {
    std::string host;
    std::uint16_t port = 9100;
    std::uint32_t connections = 1;
    std::uint32_t bufferSize = 65536;
    std::uint64_t chunkSize = 1048576;
    std::uint32_t dataFinalStatusTimeoutSeconds = 60;
    std::uint64_t maxChunks = 0;
    std::uint64_t corruptChunk = 0;
    std::uint64_t duplicateCorruptChunk = 0;
    std::string path;
    std::string transferId;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto;
    core::session::ManifestFlushPolicy manifestFlushPolicy =
        core::session::ManifestFlushPolicy::EveryNChunks;
    std::uint64_t manifestFlushIntervalChunks = 16;
    core::session::FinalVerifyPolicy finalVerifyPolicy = core::session::FinalVerifyPolicy::Full;
    core::session::CommitSyncPolicy commitSyncPolicy = core::session::CommitSyncPolicy::None;
    storage::PreallocateMode preallocateMode = storage::PreallocateMode::Off;
    storage::FileIoConfig fileIo;
    core::io::TlsConfig dataTls;
    core::io::DataTlsMode dataTlsMode = core::io::DataTlsMode::Off;
    core::io::TransferRuntimeMetrics* runtimeMetrics = nullptr;
    core::io::HotPathCompressionOptions hotPathCompression;
    std::string eventLogPath;
    bool overwrite = false;
    bool keepPartial = false;
    bool resume = false;
    bool hasCorruptChunk = false;
    bool hasDuplicateCorruptChunk = false;
};

common::Result<FileTransferOptions> parseFileTransferOptions(int argc, const char* const* argv,
                                                             FileTransferRole role);
std::string fileTransferUsage(const char* programName, FileTransferRole role);

}  // namespace cpnetflux::config
