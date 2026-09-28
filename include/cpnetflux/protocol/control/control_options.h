#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/status.h"
#include "cpnetflux/core/session/commit_sync_policy.h"
#include "cpnetflux/core/session/final_verify_policy.h"
#include "cpnetflux/core/session/manifest_flush_policy.h"
#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/protocol/control/control_auth.h"
#include "cpnetflux/storage/file_io.h"
#include "cpnetflux/storage/preallocate_mode.h"

namespace cpnetflux::protocol::control {

inline constexpr std::uint32_t kPassiveDataPortWindowSize = 512;
inline constexpr std::uint16_t kMaxPassiveDataPortBase =
    static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() -
                               kPassiveDataPortWindowSize + 1);

struct ControlServerOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2121;
    std::string root;
    std::uint16_t dataPortBase = 20200;
    std::uint32_t connections = 1;
    std::uint64_t chunkSize = 1048576;
    std::uint32_t bufferSize = 65536;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Auto;
    core::session::ManifestFlushPolicy manifestFlushPolicy =
        core::session::ManifestFlushPolicy::EveryNChunks;
    std::uint64_t manifestFlushIntervalChunks = 16;
    core::session::FinalVerifyPolicy finalVerifyPolicy = core::session::FinalVerifyPolicy::Full;
    core::session::CommitSyncPolicy commitSyncPolicy = core::session::CommitSyncPolicy::None;
    storage::PreallocateMode preallocateMode = storage::PreallocateMode::Off;
    storage::FileIoConfig fileIo;
    ControlAuthConfig auth;
    std::string user = "cpnetflux";
    std::string password = "cpnetflux";
    std::string eventLogPath;
    core::io::TlsConfig tls;
    core::io::DataTlsMode dataTlsMode = core::io::DataTlsMode::Off;
};

enum class ControlPathKind {
    ExistingFile,
    ExistingDirectory,
    StorTarget,
};

struct ResolvedControlPath {
    std::string fullPath;
    std::string virtualPath;
};

struct ControlListEntry {
    std::string name;
    bool isDirectory = false;
    std::uint64_t size = 0;
    std::int64_t mtimeUnixSeconds = 0;
};

[[nodiscard]] common::Result<ControlServerOptions> parseControlServerOptions(
    int argc, const char* const* argv);
[[nodiscard]] std::string controlServerUsage(const char* programName);
[[nodiscard]] common::Result<ResolvedControlPath> resolveControlPath(
    const std::string& root, const std::string& workingDirectory, const std::string& path,
    ControlPathKind kind, const std::string& commandName);
[[nodiscard]] common::Result<std::string> resolveVirtualPath(const std::string& workingDirectory,
                                                             const std::string& path,
                                                             bool allowEmptyPath);
[[nodiscard]] common::Result<std::string> resolveStorPath(const std::string& root,
                                                          const std::string& path);
[[nodiscard]] common::Result<std::string> resolveStorPath(const std::string& root,
                                                          const std::string& workingDirectory,
                                                          const std::string& path);
[[nodiscard]] common::Result<std::string> resolveRetrPath(const std::string& root,
                                                          const std::string& path);
[[nodiscard]] common::Result<std::string> resolveRetrPath(const std::string& root,
                                                          const std::string& workingDirectory,
                                                          const std::string& path);
[[nodiscard]] std::string formatMdtmTime(std::int64_t unixSeconds);
[[nodiscard]] std::string formatNlst(const std::vector<ControlListEntry>& entries);
[[nodiscard]] std::string formatList(const std::vector<ControlListEntry>& entries);

}  // namespace cpnetflux::protocol::control
