#include "cpnetflux/protocol/control/control_options.h"

#include <charconv>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

#include "cpnetflux/core/metrics/event_log.h"

namespace cpnetflux::protocol::control {
namespace {

constexpr std::uint32_t kMaxConnections = 64;
constexpr std::uint32_t kMaxBufferSize = 16 * 1024 * 1024;
constexpr std::uint64_t kMaxChunkSize = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxManifestFlushIntervalChunks = 65536;
constexpr std::uint64_t kMaxFileIoBufferSize = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxFileIoQueueDepth = 256;

common::Result<std::uint64_t> parseUnsigned(std::string_view value, std::string_view name) {
    if (value.empty()) {
        return common::Status::invalidArgument(std::string(name) + " must not be empty");
    }
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end) {
        return common::Status::invalidArgument(std::string(name) + " must be a decimal integer");
    }
    return parsed;
}

common::Status requireValue(int argc, int index, std::string_view option) {
    if (index + 1 >= argc) {
        return common::Status::invalidArgument(std::string(option) + " requires a value");
    }
    return common::Status::ok();
}

common::Status validateRoot(const std::string& root) {
    if (root.empty()) {
        return common::Status::invalidArgument("--root is required");
    }
    std::error_code error;
    if (!std::filesystem::exists(root, error) || error) {
        return common::Status::invalidArgument("--root must exist");
    }
    if (!std::filesystem::is_directory(root, error) || error) {
        return common::Status::invalidArgument("--root must be a directory");
    }
    return common::Status::ok();
}

common::Result<std::filesystem::path> canonicalRoot(const std::string& root) {
    std::error_code error;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return common::Status::invalidArgument("root cannot be canonicalized");
    }
    return canonical;
}

bool isInsideRoot(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path relative = std::filesystem::relative(path, root, error);
    if (error || relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

std::string virtualPathFromRelative(const std::filesystem::path& relative) {
    if (relative.empty() || relative == ".") {
        return "/";
    }
    std::string text = relative.generic_string();
    while (!text.empty() && text.back() == '/') {
        text.pop_back();
    }
    if (text.empty() || text == ".") {
        return "/";
    }
    return "/" + text;
}

}  // namespace

common::Result<ControlServerOptions> parseControlServerOptions(int argc, const char* const* argv) {
    ControlServerOptions options;
    bool hasRoot = false;
    bool hasFileIoQueueDepth = false;
    bool hasFileIoBatchSize = false;

    int index = 1;
    while (index < argc) {
        const std::string_view option(argv[index]);
        const common::Status valueStatus = requireValue(argc, index, option);
        if (!valueStatus.isOk()) {
            return valueStatus;
        }
        const std::string_view value(argv[index + 1]);

        if (option == "--host") {
            if (value.empty()) {
                return common::Status::invalidArgument("--host must not be empty");
            }
            options.host = std::string(value);
        } else if (option == "--port") {
            auto parsed = parseUnsigned(value, "--port");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > std::numeric_limits<std::uint16_t>::max()) {
                return common::Status::invalidArgument("--port must be in range 1..65535");
            }
            options.port = static_cast<std::uint16_t>(parsed.value());
        } else if (option == "--root") {
            if (value.empty()) {
                return common::Status::invalidArgument("--root must not be empty");
            }
            std::error_code error;
            options.root = std::filesystem::weakly_canonical(std::string(value), error).string();
            if (error) {
                return common::Status::invalidArgument("--root cannot be canonicalized");
            }
            hasRoot = true;
        } else if (option == "--data-port-base") {
            auto parsed = parseUnsigned(value, "--data-port-base");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxPassiveDataPortBase) {
                return common::Status::invalidArgument("--data-port-base must be in range 1.." +
                                                       std::to_string(kMaxPassiveDataPortBase));
            }
            options.dataPortBase = static_cast<std::uint16_t>(parsed.value());
        } else if (option == "--connections") {
            auto parsed = parseUnsigned(value, "--connections");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxConnections) {
                return common::Status::invalidArgument("--connections must be in range 1..64");
            }
            options.connections = static_cast<std::uint32_t>(parsed.value());
        } else if (option == "--chunk-size") {
            auto parsed = parseUnsigned(value, "--chunk-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxChunkSize) {
                return common::Status::invalidArgument(
                    "--chunk-size must be in range 1..1099511627776");
            }
            options.chunkSize = parsed.value();
        } else if (option == "--buffer-size") {
            auto parsed = parseUnsigned(value, "--buffer-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxBufferSize) {
                return common::Status::invalidArgument(
                    "--buffer-size must be in range 1..16777216");
            }
            options.bufferSize = static_cast<std::uint32_t>(parsed.value());
        } else if (option == "--checksum") {
            auto parsed = checksum::parseChecksumAlgorithm(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.checksumAlgorithm = parsed.value();
        } else if (option == "--checksum-backend") {
            auto parsed = checksum::parseChecksumBackend(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.checksumBackend = parsed.value();
        } else if (option == "--manifest-flush-interval-chunks") {
            auto parsed = parseUnsigned(value, "--manifest-flush-interval-chunks");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxManifestFlushIntervalChunks) {
                return common::Status::invalidArgument(
                    "--manifest-flush-interval-chunks must be in range 1..65536");
            }
            options.manifestFlushIntervalChunks = parsed.value();
        } else if (option == "--manifest-flush-policy") {
            auto parsed = core::session::parseManifestFlushPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.manifestFlushPolicy = parsed.value();
        } else if (option == "--final-verify-policy") {
            auto parsed = core::session::parseFinalVerifyPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.finalVerifyPolicy = parsed.value();
        } else if (option == "--commit-sync-policy") {
            auto parsed = core::session::parseCommitSyncPolicy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.commitSyncPolicy = parsed.value();
        } else if (option == "--preallocate") {
            auto parsed = storage::parsePreallocateMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.preallocateMode = parsed.value();
        } else if (option == "--file-io-backend") {
            auto parsed = storage::parseFileIoBackendKind(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.backend = parsed.value();
        } else if (option == "--file-io-buffer-size") {
            auto parsed = parseUnsigned(value, "--file-io-buffer-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() > kMaxFileIoBufferSize) {
                return common::Status::invalidArgument(
                    "--file-io-buffer-size must be in range 0..67108864");
            }
            options.fileIo.bufferSize = parsed.value();
        } else if (option == "--file-io-queue-depth") {
            auto parsed = parseUnsigned(value, "--file-io-queue-depth");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxFileIoQueueDepth) {
                return common::Status::invalidArgument(
                    "--file-io-queue-depth must be in range 1..256");
            }
            options.fileIo.queueDepth = parsed.value();
            hasFileIoQueueDepth = true;
        } else if (option == "--file-io-batch-size") {
            auto parsed = parseUnsigned(value, "--file-io-batch-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxFileIoQueueDepth) {
                return common::Status::invalidArgument(
                    "--file-io-batch-size must be in range 1..256");
            }
            options.fileIo.batchSize = parsed.value();
            hasFileIoBatchSize = true;
        } else if (option == "--file-io-advice") {
            auto parsed = storage::parseFileIoAdvice(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.advice = parsed.value();
        } else if (option == "--posix-write-strategy") {
            auto parsed = storage::parsePosixWriteStrategy(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.fileIo.posixWriteStrategy = parsed.value();
        } else if (option == "--auth-mode") {
            auto parsed = parseAuthMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.auth.mode = parsed.value();
        } else if (option == "--auth-token-file") {
            if (value.empty()) {
                return common::Status::invalidArgument("--auth-token-file must not be empty");
            }
            options.auth.tokenFile = std::string(value);
        } else if (option == "--event-log") {
            if (value.empty()) {
                return common::Status::invalidArgument("--event-log must not be empty");
            }
            options.eventLogPath = std::string(value);
        } else if (option == "--tls-mode") {
            auto parsed = core::io::parseTlsMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.tls.mode = parsed.value();
        } else if (option == "--tls-cert-file") {
            if (value.empty()) {
                return common::Status::invalidArgument("--tls-cert-file must not be empty");
            }
            options.tls.certFile = std::string(value);
        } else if (option == "--tls-key-file") {
            if (value.empty()) {
                return common::Status::invalidArgument("--tls-key-file must not be empty");
            }
            options.tls.keyFile = std::string(value);
        } else if (option == "--tls-ca-file") {
            if (value.empty()) {
                return common::Status::invalidArgument("--tls-ca-file must not be empty");
            }
            options.tls.caFile = std::string(value);
        } else if (option == "--data-tls-mode") {
            auto parsed = core::io::parseDataTlsMode(value);
            if (!parsed.isOk()) {
                return parsed.status();
            }
            options.dataTlsMode = parsed.value();
        } else if (option == "--user") {
            if (value.empty()) {
                return common::Status::invalidArgument("--user must not be empty");
            }
            options.user = std::string(value);
            options.auth.user = options.user;
        } else if (option == "--password") {
            if (value.empty()) {
                return common::Status::invalidArgument("--password must not be empty");
            }
            options.password = std::string(value);
            options.auth.password = options.password;
        } else {
            return common::Status::invalidArgument("unknown option: " + std::string(option));
        }
        index += 2;
    }

    if (!hasRoot) {
        return common::Status::invalidArgument("--root is required");
    }
    if (hasFileIoQueueDepth && !hasFileIoBatchSize) {
        options.fileIo.batchSize = options.fileIo.queueDepth;
    }
    const common::Status fileIoStatus = storage::validateFileIoConfig(options.fileIo);
    if (!fileIoStatus.isOk()) {
        return fileIoStatus;
    }
    const common::Status rootStatus = validateRoot(options.root);
    if (!rootStatus.isOk()) {
        return rootStatus;
    }
    if (options.auth.mode == AuthMode::Token) {
        auto token = loadTokenFile(options.auth.tokenFile);
        if (!token.isOk()) {
            return token.status();
        }
        options.auth.token = token.value();
        options.user = "token";
        options.password.clear();
    }
    const common::Status eventLogStatus =
        core::metrics::validateEventLogPath(options.eventLogPath);
    if (!eventLogStatus.isOk()) {
        return eventLogStatus;
    }
    const common::Status tlsStatus = core::io::validateTlsServerConfig(options.tls);
    if (!tlsStatus.isOk()) {
        return tlsStatus;
    }
    const common::Status dataTlsStatus =
        core::io::validateDataTlsServerConfig(options.tls.mode, options.dataTlsMode, options.tls);
    if (!dataTlsStatus.isOk()) {
        return dataTlsStatus;
    }
    return options;
}

std::string controlServerUsage(const char* programName) {
    std::ostringstream output;
    output << "Usage: " << programName
           << " --root <dir> [--host <ip>] [--port <port>] "
              "[--data-port-base <port>] [--connections <N>] "
              "[--chunk-size <N>] [--buffer-size <N>] "
              "[--checksum crc32c|none] [--checksum-backend auto|software|hardware] "
              "[--manifest-flush-policy every_n_chunks|final_only] "
              "[--manifest-flush-interval-chunks <N>] "
              "[--final-verify-policy full|verified_chunks] "
              "[--commit-sync-policy none|fsync_file|fsync_file_and_dir] "
              "[--preallocate off|full] "
              "[--file-io-backend posix|io_uring] [--file-io-buffer-size <bytes>] "
              "[--file-io-queue-depth <N>] [--file-io-batch-size <N>] "
              "[--file-io-advice off|sequential|noreuse|dontneed|sequential_dontneed] "
              "[--posix-write-strategy auto|direct|coalesced] "
              "[--auth-mode anonymous|token] [--auth-token-file <path>] "
              "[--user <name>] [--password <password>] [--event-log <path>] "
              "[--tls-mode off|explicit|required] [--tls-cert-file <path>] "
              "[--tls-key-file <path>] [--tls-ca-file <path>] "
              "[--data-tls-mode off|required]";
    return output.str();
}

common::Result<std::string> resolveStorPath(const std::string& root, const std::string& path) {
    return resolveStorPath(root, "/", path);
}

common::Result<std::string> resolveStorPath(const std::string& root,
                                            const std::string& workingDirectory,
                                            const std::string& path) {
    auto resolved =
        resolveControlPath(root, workingDirectory, path, ControlPathKind::StorTarget, "STOR");
    if (!resolved.isOk()) {
        return resolved.status();
    }
    return resolved.value().fullPath;
}

common::Result<std::string> resolveRetrPath(const std::string& root, const std::string& path) {
    return resolveRetrPath(root, "/", path);
}

common::Result<std::string> resolveRetrPath(const std::string& root,
                                            const std::string& workingDirectory,
                                            const std::string& path) {
    auto resolved =
        resolveControlPath(root, workingDirectory, path, ControlPathKind::ExistingFile, "RETR");
    if (!resolved.isOk()) {
        return resolved.status();
    }
    return resolved.value().fullPath;
}

common::Result<std::string> resolveVirtualPath(const std::string& workingDirectory,
                                               const std::string& path, bool allowEmptyPath) {
    if (path.empty() && !allowEmptyPath) {
        return common::Status::invalidArgument("path must not be empty");
    }

    const std::filesystem::path requested(path);
    if (!path.empty() && requested.is_absolute()) {
        return common::Status::invalidArgument("path must be relative");
    }

    std::string current = workingDirectory;
    if (current.empty()) {
        current = "/";
    }
    if (current.front() == '/') {
        current.erase(current.begin());
    }
    const std::filesystem::path base(current);
    std::filesystem::path combined = path.empty() ? base : (base / requested);
    combined = combined.lexically_normal();
    if (combined.empty() || combined == ".") {
        return std::string("/");
    }

    for (const std::filesystem::path& part : combined) {
        if (part == "..") {
            return common::Status::invalidArgument("path escapes root");
        }
    }
    return virtualPathFromRelative(combined);
}

common::Result<ResolvedControlPath> resolveControlPath(const std::string& root,
                                                       const std::string& workingDirectory,
                                                       const std::string& path,
                                                       ControlPathKind kind,
                                                       const std::string& commandName) {
    const bool allowEmptyPath = kind == ControlPathKind::ExistingDirectory;
    auto virtualPath = resolveVirtualPath(workingDirectory, path, allowEmptyPath);
    if (!virtualPath.isOk()) {
        return common::Status::invalidArgument(commandName + " " + virtualPath.status().message());
    }
    if (virtualPath.value() == "/" && kind != ControlPathKind::ExistingDirectory) {
        return common::Status::invalidArgument(commandName + " path must name a file");
    }

    auto rootPath = canonicalRoot(root);
    if (!rootPath.isOk()) {
        return rootPath.status();
    }

    std::filesystem::path relative;
    if (virtualPath.value() != "/") {
        relative = std::filesystem::path(virtualPath.value().substr(1));
    }
    const std::filesystem::path candidate = rootPath.value() / relative;

    std::error_code error;
    if (kind == ControlPathKind::StorTarget) {
        if (std::filesystem::exists(candidate, error) &&
            std::filesystem::is_directory(candidate, error)) {
            return common::Status::invalidArgument(commandName + " path points to a directory");
        }

        const std::filesystem::path parent = candidate.parent_path();
        if (!std::filesystem::exists(parent, error) || error) {
            std::filesystem::create_directories(parent, error);
            if (error) {
                return common::Status::invalidArgument(commandName +
                                                       " parent directory cannot be created");
            }
        }
        std::filesystem::path canonicalParent = std::filesystem::weakly_canonical(parent, error);
        if (error || !isInsideRoot(rootPath.value(), canonicalParent)) {
            return common::Status::invalidArgument(commandName + " path escapes root");
        }
        return ResolvedControlPath{candidate.string(), virtualPath.value()};
    }

    if (!std::filesystem::exists(candidate, error) || error) {
        return common::Status::invalidArgument(commandName + " path does not exist");
    }
    std::filesystem::path canonicalTarget = std::filesystem::weakly_canonical(candidate, error);
    if (error || !isInsideRoot(rootPath.value(), canonicalTarget)) {
        return common::Status::invalidArgument(commandName + " path escapes root");
    }
    if (kind == ControlPathKind::ExistingFile) {
        if (!std::filesystem::is_regular_file(canonicalTarget, error) || error) {
            return common::Status::invalidArgument(commandName + " path is not a regular file");
        }
    } else {
        if (!std::filesystem::is_directory(canonicalTarget, error) || error) {
            return common::Status::invalidArgument(commandName + " path is not a directory");
        }
    }
    return ResolvedControlPath{canonicalTarget.string(), virtualPath.value()};
}

std::string formatMdtmTime(std::int64_t unixSeconds) {
    std::time_t seconds = static_cast<std::time_t>(unixSeconds);
    std::tm timeValue{};
    (void)::gmtime_r(&seconds, &timeValue);
    char buffer[16];
    (void)std::strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", &timeValue);
    return std::string(buffer);
}

std::string formatNlst(const std::vector<ControlListEntry>& entries) {
    std::ostringstream output;
    for (const ControlListEntry& entry : entries) {
        output << entry.name << "\r\n";
    }
    return output.str();
}

std::string formatList(const std::vector<ControlListEntry>& entries) {
    std::ostringstream output;
    for (const ControlListEntry& entry : entries) {
        output << (entry.isDirectory ? 'd' : '-') << ' ' << entry.size << ' '
               << formatMdtmTime(entry.mtimeUnixSeconds) << ' ' << entry.name << "\r\n";
    }
    return output.str();
}

}  // namespace cpnetflux::protocol::control
