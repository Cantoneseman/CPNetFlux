#pragma once

#include <cstdint>
#include <string>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/hot_path_compression.h"
#include "cpnetflux/core/session/commit_sync_policy.h"
#include "cpnetflux/core/session/final_verify_policy.h"
#include "cpnetflux/core/session/manifest_flush_policy.h"
#include "cpnetflux/core/session/transfer_session_config.h"
#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/core/io/transfer_runtime_metrics.h"
#include "cpnetflux/storage/file_io.h"
#include "cpnetflux/storage/preallocate_mode.h"

namespace cpnetflux::config {

struct FileDownloadOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9101;
    std::uint32_t connections = 1;
    std::uint32_t bufferSize = 65536;
    std::string path;
    std::string transferId;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto;
    core::session::ManifestFlushPolicy manifestFlushPolicy =
        core::session::ManifestFlushPolicy::EveryNChunks;
    std::uint64_t manifestFlushIntervalChunks =
        core::session::kDefaultManifestFlushIntervalChunks;
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
    bool resume = false;
    std::uint64_t maxChunks = 0;
};

common::Result<FileDownloadOptions> parseFileDownloadOptions(int argc, const char* const* argv);
std::string fileDownloadUsage(const char* programName);

}  // namespace cpnetflux::config
