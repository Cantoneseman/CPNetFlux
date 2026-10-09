#include "cpnetflux/core/io/tree_transfer_client.h"
#include "cpnetflux/core/io/tree_pipeline_identity.h"

#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <deque>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <memory>
#include <functional>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/config/file_download_options.h"
#include "cpnetflux/config/file_transfer_options.h"
#include "cpnetflux/core/io/file_download_client.h"
#include "cpnetflux/core/io/file_transfer_client.h"
#include "cpnetflux/core/metrics/error_code.h"
#include "cpnetflux/core/metrics/event_log.h"
#include "cpnetflux/core/io/socket_utils.h"
#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/core/io/tree_pipeline_control_io.h"
#include "cpnetflux/core/scheduler/scheduler.h"
#include "cpnetflux/core/tree/tree_manifest.h"
#include "cpnetflux/core/tree/tree_scan.h"
#include "cpnetflux/protocol/control/control_auth.h"

namespace cpnetflux::core::io {
namespace {

struct ControlReply {
    int code = 0;
    std::vector<std::string> lines;
};

constexpr auto kPipelineControlResponseTimeout = std::chrono::seconds(30);

class ControlClient {
   public:
    [[nodiscard]] common::Status connectTo(const std::string& host, std::uint16_t port,
                                           const TlsConfig& tls) {
        if (cancelled_.load(std::memory_order_acquire)) {
            return common::Status::runtimeError("tree pipeline candidate cancelled");
        }
        // DNS/connect/TLS handshake retain their existing blocking behavior.
        // Cancellation can shutdown the socket after publication below.
        host_ = host;
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* results = nullptr;
        const std::string portText = std::to_string(port);
        const int gaiStatus = ::getaddrinfo(host.c_str(), portText.c_str(), &hints, &results);
        if (gaiStatus != 0) {
            return common::Status::runtimeError(std::string("getaddrinfo: ") +
                                                gai_strerror(gaiStatus));
        }
        int lastError = 0;
        for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
            UniqueFd candidate(
                ::socket(item->ai_family, item->ai_socktype | SOCK_CLOEXEC, item->ai_protocol));
            if (!candidate.isValid()) {
                lastError = errno;
                continue;
            }
            if (::connect(candidate.get(), item->ai_addr, item->ai_addrlen) == 0) {
                fd_ = std::move(candidate);
                break;
            }
            lastError = errno;
        }
        ::freeaddrinfo(results);
        if (!fd_.isValid()) {
            return common::Status::systemError("connect: " + std::string(std::strerror(lastError)),
                                               lastError);
        }
        if (tls.mode == TlsMode::Required) {
            auto context = TlsClientContext::create(tls);
            if (!context.isOk()) {
                return context.status();
            }
            auto connection = context.value().connect(std::move(fd_), host_);
            if (!connection.isOk()) {
                return connection.status();
            }
            std::lock_guard<std::mutex> lock(cancelMutex_);
            control_ = std::move(connection.value());
        } else {
            std::lock_guard<std::mutex> lock(cancelMutex_);
            control_ = TlsConnection::plain(std::move(fd_));
        }
        if (cancelled_.load(std::memory_order_acquire)) {
            cancel();
            return common::Status::runtimeError("tree pipeline candidate cancelled");
        }
        auto greeting = readReply();
        if (!greeting.isOk()) {
            return greeting.status();
        }
        if (greeting.value().code != 220) {
            return common::Status::runtimeError("unexpected control greeting");
        }
        return common::Status::ok();
    }

    void cancel() noexcept {
        cancelled_.store(true, std::memory_order_release);
        // Serialize publication with shutdown; never close a fd from this thread.
        // The owner destroys the connection only after preparation has joined.
        std::lock_guard<std::mutex> lock(cancelMutex_);
        if (control_.valid()) {
            (void)::shutdown(control_.fd(), SHUT_RDWR);
        }
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(cancelMutex_);
        control_ = TlsConnection{};
        fd_.reset();
        buffer_.clear();
        parallelism_ = 0;
        responseTimeout_.reset();
        terminalReplies_.clear();
        cancelled_.store(false, std::memory_order_release);
    }

    [[nodiscard]] common::Status login(const std::string& user, const std::string& password,
                                       std::uint32_t connections) {
        auto userReply = command("USER " + user);
        if (!userReply.isOk() || userReply.value().code != 331) {
            return userReply.isOk() ? common::Status::runtimeError("USER rejected")
                                    : userReply.status();
        }
        auto passReply = command("PASS " + password);
        if (!passReply.isOk() || passReply.value().code != 230) {
            return passReply.isOk() ? common::Status::runtimeError("PASS rejected")
                                    : passReply.status();
        }
        auto typeReply = command("TYPE I");
        if (!typeReply.isOk() || typeReply.value().code != 200) {
            return typeReply.isOk() ? common::Status::runtimeError("TYPE I rejected")
                                    : typeReply.status();
        }
        return setParallelism(connections);
    }

    [[nodiscard]] common::Status enablePipeline() {
        auto reply = command("OPTS PIPELINE=1");
        if (!reply.isOk()) {
            return reply.status();
        }
        return detail::validatePipelineEnableReply(reply.value().code);
    }

    [[nodiscard]] common::Status setParallelism(std::uint32_t connections) {
        if (parallelism_ == connections) {
            return common::Status::ok();
        }
        auto optsReply = command("OPTS PARALLELISM=" + std::to_string(connections));
        if (!optsReply.isOk() || optsReply.value().code != 200) {
            return optsReply.isOk() ? common::Status::runtimeError("OPTS PARALLELISM rejected")
                                    : optsReply.status();
        }
        parallelism_ = connections;
        return common::Status::ok();
    }

    [[nodiscard]] std::uint32_t parallelism() const noexcept { return parallelism_; }

    void setResponseTimeout(std::chrono::milliseconds timeout) noexcept {
        responseTimeout_ = timeout;
    }

    struct PassiveTransferStart {
        std::uint16_t dataPort = 0;
        std::string transferId;
    };

    [[nodiscard]] common::Result<std::uint16_t> epsv() {
        auto reply = command("EPSV");
        if (!reply.isOk()) {
            return reply.status();
        }
        return parsePassivePort(reply.value());
    }

    // Send EPSV and the transfer command in one control write window. The
    // server processes them in order, so this removes one WAN RTT without
    // changing the reply order or transfer semantics.
    [[nodiscard]] common::Result<PassiveTransferStart> startTransferWithPassive(
        const std::string& verb, const std::string& path) {
        if (cancelled_.load(std::memory_order_acquire)) {
            return common::Status::runtimeError("tree pipeline candidate cancelled");
        }
        const std::string commands = "EPSV\r\n" + verb + " " + path + "\r\n";
        const common::Status sent = control_.writeAll(commands.data(), commands.size());
        if (!sent.isOk()) {
            return sent;
        }
        auto epsvReply = readCommandReply();
        if (!epsvReply.isOk()) {
            return epsvReply.status();
        }
        auto dataPort = parsePassivePort(epsvReply.value());
        if (!dataPort.isOk()) {
            return dataPort.status();
        }
        auto transferReply = readCommandReply();
        if (!transferReply.isOk()) {
            return transferReply.status();
        }
        if (transferReply.value().code != 150) {
            return common::Status::runtimeError(verb + " rejected: " + joined(transferReply.value()));
        }
        static const std::regex pattern(R"(transfer_id=GFID:([A-Za-z0-9._-]+))");
        std::smatch match;
        const std::string text = joined(transferReply.value());
        if (!std::regex_search(text, match, pattern)) {
            return common::Status::runtimeError("failed to parse transfer_id");
        }
        return PassiveTransferStart{dataPort.value(), match[1].str()};
    }

    [[nodiscard]] common::Status rest(const std::string& transferId) {
        auto reply = command("REST GFID:" + transferId);
        if (!reply.isOk()) {
            return reply.status();
        }
        if (reply.value().code != 350) {
            return common::Status::runtimeError("REST GFID rejected");
        }
        return common::Status::ok();
    }

    [[nodiscard]] common::Result<std::string> startTransfer(const std::string& verb,
                                                            const std::string& path) {
        auto reply = command(verb + " " + path);
        if (!reply.isOk()) {
            return reply.status();
        }
        if (reply.value().code != 150) {
            return common::Status::runtimeError(verb + " rejected: " + joined(reply.value()));
        }
        static const std::regex pattern(R"(transfer_id=GFID:([A-Za-z0-9._-]+))");
        std::smatch match;
        const std::string text = joined(reply.value());
        if (!std::regex_search(text, match, pattern)) {
            return common::Status::runtimeError("failed to parse transfer_id");
        }
        return match[1].str();
    }

    [[nodiscard]] common::Status waitTransferComplete(
        const std::string& expectedTransferId = {}) {
        auto cached = terminalReplies_.find(expectedTransferId);
        if (!expectedTransferId.empty() && cached != terminalReplies_.end()) {
            const ControlReply reply = std::move(cached->second);
            terminalReplies_.erase(cached);
            return validateTransferTerminal(reply, expectedTransferId);
        }
        while (true) {
            auto reply = readReply();
            if (!reply.isOk()) {
                return reply.status();
            }
            const auto terminal = parseTerminal(reply.value());
            if (!terminal.has_value()) {
                return common::Status::runtimeError(
                    "unexpected control reply while waiting for transfer completion: " +
                    joined(reply.value()));
            }
            if (expectedTransferId.empty() || terminal->transferId == expectedTransferId) {
                return validateTransferTerminal(reply.value(), expectedTransferId);
            }
            terminalReplies_.insert_or_assign(terminal->transferId, std::move(reply.value()));
        }
    }

    [[nodiscard]] common::Result<std::vector<std::string>> nlst(const std::string& path) {
        auto port = epsv();
        if (!port.isOk()) {
            return port.status();
        }
        auto openReply = sendOnly("NLST " + path);
        if (!openReply.isOk()) {
            return openReply;
        }
        auto opening = readReply();
        if (!opening.isOk()) {
            return opening.status();
        }
        if (opening.value().code != 150) {
            return common::Status::runtimeError("NLST rejected");
        }
        auto payload = readAsciiData(port.value());
        if (!payload.isOk()) {
            return payload.status();
        }
        auto complete = readReply();
        if (!complete.isOk()) {
            return complete.status();
        }
        if (complete.value().code != 226) {
            return common::Status::runtimeError("NLST did not complete");
        }
        std::vector<std::string> names;
        std::istringstream input(payload.value());
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                names.push_back(line);
            }
        }
        return names;
    }

    [[nodiscard]] common::Result<std::uint64_t> size(const std::string& path) {
        auto reply = command("SIZE " + path);
        if (!reply.isOk()) {
            return reply.status();
        }
        if (reply.value().code != 213 || reply.value().lines.empty()) {
            return common::Status::runtimeError("SIZE rejected");
        }
        const std::string line = reply.value().lines.front();
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) {
            return common::Status::runtimeError("SIZE response missing value");
        }
        return static_cast<std::uint64_t>(std::stoull(line.substr(space + 1)));
    }

    [[nodiscard]] common::Result<std::int64_t> mdtm(const std::string& path) {
        auto reply = command("MDTM " + path);
        if (!reply.isOk()) {
            return reply.status();
        }
        if (reply.value().code != 213 || reply.value().lines.empty()) {
            return common::Status::runtimeError("MDTM rejected");
        }
        const std::string line = reply.value().lines.front();
        const std::size_t space = line.find(' ');
        if (space == std::string::npos || line.size() < space + 1 + 14) {
            return common::Status::runtimeError("MDTM response missing timestamp");
        }
        const std::string text = line.substr(space + 1, 14);
        if (!std::all_of(text.begin(), text.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) {
            return common::Status::runtimeError("MDTM response has invalid timestamp");
        }
        std::tm tm{};
        tm.tm_year = std::stoi(text.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(text.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(text.substr(6, 2));
        tm.tm_hour = std::stoi(text.substr(8, 2));
        tm.tm_min = std::stoi(text.substr(10, 2));
        tm.tm_sec = std::stoi(text.substr(12, 2));
        tm.tm_isdst = 0;
        const std::time_t value = ::timegm(&tm);
        if (value < 0) {
            return common::Status::runtimeError("MDTM timestamp is out of range");
        }
        return static_cast<std::int64_t>(value);
    }

   private:
    static common::Result<std::uint16_t> parsePassivePort(const ControlReply& reply) {
        if (reply.code != 229) {
            return common::Status::runtimeError("EPSV rejected");
        }
        static const std::regex pattern(R"(\(\|\|\|([0-9]+)\|\))");
        std::smatch match;
        const std::string text = joined(reply);
        if (!std::regex_search(text, match, pattern)) {
            return common::Status::runtimeError("failed to parse EPSV port");
        }
        const auto port = static_cast<unsigned long>(std::stoul(match[1].str()));
        if (port == 0 || port > 65535) {
            return common::Status::runtimeError("EPSV port out of range");
        }
        return static_cast<std::uint16_t>(port);
    }

    [[nodiscard]] common::Status sendOnly(const std::string& commandText) {
        if (cancelled_.load(std::memory_order_acquire)) {
            return common::Status::runtimeError("tree pipeline candidate cancelled");
        }
        const std::string text = commandText + "\r\n";
        return control_.writeAll(text.data(), text.size());
    }

    [[nodiscard]] common::Result<ControlReply> command(const std::string& commandText) {
        const common::Status sendStatus = sendOnly(commandText);
        if (!sendStatus.isOk()) {
            return sendStatus;
        }
        return readCommandReply();
    }

    static std::optional<detail::TreePipelineTerminalReply> parseTerminal(
        const ControlReply& reply) {
        return detail::parseTreePipelineTerminalReply(reply.code, joined(reply));
    }

    static common::Status validateTransferTerminal(const ControlReply& reply,
                                                   const std::string& expectedTransferId) {
        const auto terminal = parseTerminal(reply);
        if (!terminal.has_value()) {
            return common::Status::runtimeError("transfer terminal reply has no transfer_id");
        }
        if (!expectedTransferId.empty() && terminal->transferId != expectedTransferId) {
            return common::Status::runtimeError("transfer terminal reply has mismatched transfer_id");
        }
        if (terminal->code != 226) {
            return common::Status::runtimeError("transfer failed: " + joined(reply));
        }
        return common::Status::ok();
    }

    [[nodiscard]] common::Result<ControlReply> readCommandReply() {
        while (true) {
            auto reply = readReply();
            if (!reply.isOk()) {
                return reply.status();
            }
            const auto terminal = parseTerminal(reply.value());
            if (!terminal.has_value()) {
                return reply;
            }
            terminalReplies_.insert_or_assign(terminal->transferId, std::move(reply.value()));
        }
    }

    [[nodiscard]] common::Result<ControlReply> readReply() {
        detail::ControlReadDeadline deadline;
        if (responseTimeout_.has_value()) {
            deadline = std::chrono::steady_clock::now() + *responseTimeout_;
        }
        auto first = readLine(deadline);
        if (!first.isOk()) {
            return first.status();
        }
        ControlReply reply;
        reply.lines.push_back(first.value());
        if (const auto code = detail::parseControlReplyCode(first.value()); code.has_value()) {
            reply.code = *code;
        }
        if (first.value().size() >= 4 && first.value()[3] == '-') {
            const std::string expected = first.value().substr(0, 3) + " ";
            while (true) {
                auto line = readLine(deadline);
                if (!line.isOk()) {
                    return line.status();
                }
                reply.lines.push_back(line.value());
                if (line.value().starts_with(expected)) {
                    break;
                }
            }
        }
        return reply;
    }

    [[nodiscard]] common::Result<std::string> readLine(
        detail::ControlReadDeadline deadline = std::nullopt) {
        return detail::readTreePipelineControlLine(&control_, &buffer_, &cancelled_, deadline);
    }

    [[nodiscard]] common::Result<std::string> readAsciiData(std::uint16_t port) const {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* results = nullptr;
        const std::string portText = std::to_string(port);
        const int gaiStatus = ::getaddrinfo(host_.c_str(), portText.c_str(), &hints, &results);
        if (gaiStatus != 0) {
            return common::Status::runtimeError(std::string("getaddrinfo data: ") +
                                                gai_strerror(gaiStatus));
        }
        UniqueFd dataFd;
        int lastError = 0;
        for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
            UniqueFd candidate(
                ::socket(item->ai_family, item->ai_socktype | SOCK_CLOEXEC, item->ai_protocol));
            if (!candidate.isValid()) {
                lastError = errno;
                continue;
            }
            if (::connect(candidate.get(), item->ai_addr, item->ai_addrlen) == 0) {
                dataFd = std::move(candidate);
                break;
            }
            lastError = errno;
        }
        ::freeaddrinfo(results);
        if (!dataFd.isValid()) {
            return common::Status::systemError("connect data: " + std::string(std::strerror(lastError)),
                                               lastError);
        }
        std::string payload;
        char chunk[4096];
        while (true) {
            const ssize_t received = ::recv(dataFd.get(), chunk, sizeof(chunk), 0);
            if (received > 0) {
                payload.append(chunk, static_cast<std::size_t>(received));
                continue;
            }
            if (received == 0) {
                return payload;
            }
            if (errno == EINTR) {
                continue;
            }
            return common::Status::systemError("recv data: " + std::string(std::strerror(errno)),
                                               errno);
        }
    }

    static std::string joined(const ControlReply& reply) {
        std::ostringstream output;
        for (const std::string& line : reply.lines) {
            output << line << '\n';
        }
        return output.str();
    }

    UniqueFd fd_;
    TlsConnection control_;
    std::string host_;
    std::string buffer_;
    std::uint32_t parallelism_ = 0;
    std::mutex cancelMutex_;
    std::optional<std::chrono::milliseconds> responseTimeout_;
    std::unordered_map<std::string, ControlReply> terminalReplies_;
    std::atomic<bool> cancelled_{false};
};

std::string generateTransferId() {
    constexpr char kDigits[] = "0123456789abcdef";
    std::random_device random;
    std::string id(32, '0');
    for (char& value : id) {
        value = kDigits[random() & 0x0F];
    }
    return id;
}

std::string joinRemotePath(const std::string& root, const std::string& relative) {
    if (root == "/") {
        return relative;
    }
    if (root.empty()) {
        return relative;
    }
    return root + "/" + relative;
}

common::Result<std::string> remoteRelativePath(const std::string& root, const std::string& path) {
    if (root == "/" || root.empty()) {
        const common::Status status = core::tree::validateTreeRelativePath(path);
        if (!status.isOk()) {
            return status;
        }
        return path;
    }
    if (path == root) {
        return common::Status::invalidArgument("remote file path equals tree root");
    }
    const std::string prefix = root + "/";
    if (!path.starts_with(prefix)) {
        return common::Status::invalidArgument("remote file path is outside tree root");
    }
    const std::string relative = path.substr(prefix.size());
    const common::Status status = core::tree::validateTreeRelativePath(relative);
    if (!status.isOk()) {
        return status;
    }
    return relative;
}

common::Status saveManifest(core::tree::TreeManifest* manifest, const std::string& path) {
    manifest->updatedAtUnixNanos = checkpoint::nowUnixNanos();
    return core::tree::saveTreeManifestAtomic(path, *manifest);
}

common::Status ensureControlReady(ControlClient* client, const config::TreeTransferOptions& options) {
    const common::Status connectStatus = client->connectTo(options.host, options.port, options.tls);
    if (!connectStatus.isOk()) {
        return connectStatus;
    }
    if (options.authMode == "token") {
        auto token = protocol::control::loadTokenFile(options.authTokenFile);
        if (!token.isOk()) {
            return token.status();
        }
        return client->login("token", token.value(), options.connections);
    }
    return client->login(options.user, options.password, options.connections);
}

common::Status validateCompletedUploadFile(ControlClient* client, const std::string& remotePath,
                                           const core::tree::TreeFileRecord& record) {
    auto size = client->size(remotePath);
    if (!size.isOk()) {
        return size.status();
    }
    if (size.value() != record.size) {
        return common::Status::invalidArgument("completed upload file changed: " + record.relativePath);
    }
    return common::Status::ok();
}

common::Status validateCompletedDownloadFile(const std::string& localRoot,
                                             const core::tree::TreeFileRecord& record) {
    std::error_code error;
    const std::filesystem::path path = std::filesystem::path(localRoot) / record.relativePath;
    if (!std::filesystem::exists(path, error) || error) {
        return common::Status::invalidArgument("completed download file missing: " +
                                               record.relativePath);
    }
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return common::Status::invalidArgument("completed download path is not a file: " +
                                               record.relativePath);
    }
    const std::uint64_t size = std::filesystem::file_size(path, error);
    if (error || size != record.size) {
        return common::Status::invalidArgument("completed download file changed: " +
                                               record.relativePath);
    }
    return common::Status::ok();
}

common::Status createParentDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return common::Status::systemError("create parent directory failed: " + error.message(),
                                           error.value());
    }
    return common::Status::ok();
}

struct FileMetadata {
    std::uint64_t size = 0;
    std::int64_t mtimeUnixSeconds = 0;
};

common::Result<FileMetadata> statRegularFile(const std::filesystem::path& path) {
    struct stat statBuffer {};
    if (::stat(path.c_str(), &statBuffer) != 0) {
        return common::Status::systemError("stat: " + std::string(std::strerror(errno)), errno);
    }
    if (!S_ISREG(statBuffer.st_mode)) {
        return common::Status::invalidArgument("tree path is not a regular file: " + path.string());
    }
    return FileMetadata{static_cast<std::uint64_t>(statBuffer.st_size),
                        static_cast<std::int64_t>(statBuffer.st_mtime)};
}

std::string changedMessage(const char* prefix, const core::tree::TreeFileRecord& record,
                           const FileMetadata& current) {
    std::ostringstream output;
    output << prefix << ": " << record.relativePath << " manifest_size=" << record.size
           << " manifest_mtime=" << record.mtimeUnixSeconds << " current_size=" << current.size
           << " current_mtime=" << current.mtimeUnixSeconds;
    return output.str();
}

std::string changedMissingMessage(const char* prefix, const core::tree::TreeFileRecord& record,
                                  const std::string& detail) {
    std::ostringstream output;
    output << prefix << ": " << record.relativePath << " manifest_size=" << record.size
           << " manifest_mtime=" << record.mtimeUnixSeconds
           << " current_size=missing current_mtime=missing detail=" << detail;
    return output.str();
}

bool metadataMatches(const core::tree::TreeFileRecord& record, const FileMetadata& current) {
    return record.size == current.size && record.mtimeUnixSeconds == current.mtimeUnixSeconds;
}

common::Status setRegularFileMtime(const std::filesystem::path& path, std::int64_t mtimeUnixSeconds) {
    timespec times[2]{};
    times[0].tv_sec = static_cast<time_t>(mtimeUnixSeconds);
    times[1].tv_sec = static_cast<time_t>(mtimeUnixSeconds);
    if (::utimensat(AT_FDCWD, path.c_str(), times, 0) != 0) {
        return common::Status::systemError("utimensat: " + std::string(std::strerror(errno)),
                                           errno);
    }
    return common::Status::ok();
}

std::uint64_t totalBytes(const core::tree::TreeManifest& manifest) {
    std::uint64_t total = 0;
    for (const auto& file : manifest.files) {
        total += file.size;
    }
    return total;
}

std::filesystem::path absoluteLexicalPath(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = path;
    }
    return absolute.lexically_normal();
}

bool pathInsideOrEqualPath(const std::filesystem::path& candidate,
                           const std::filesystem::path& root) {
    const std::filesystem::path normalizedCandidate = absoluteLexicalPath(candidate);
    const std::filesystem::path normalizedRoot = absoluteLexicalPath(root);
    auto candIt = normalizedCandidate.begin();
    auto rootIt = normalizedRoot.begin();
    for (; rootIt != normalizedRoot.end(); ++rootIt, ++candIt) {
        if (candIt == normalizedCandidate.end() || *candIt != *rootIt) {
            return false;
        }
    }
    return true;
}

common::Result<core::scheduler::SchedulerMetricsPaths> schedulerMetricsPaths(
    const config::TreeTransferOptions& options, const std::string& manifestPath,
    const char* direction) {
    const std::string localRoot =
        std::string(direction) == "upload" ? options.sourceDir : options.destDir;
    std::filesystem::path metricsDir;
    if (options.schedulerMetricsDir.empty()) {
        metricsDir = std::filesystem::path(manifestPath);
        metricsDir.replace_filename(metricsDir.filename().string() + ".scheduler");
    } else {
        metricsDir = std::filesystem::path(options.schedulerMetricsDir);
    }
    if (pathInsideOrEqualPath(metricsDir, localRoot)) {
        return common::Status::invalidArgument(
            "scheduler metrics directory must be outside the local transfer root");
    }
    core::scheduler::SchedulerMetricsPaths paths;
    paths.summaryCsv = (metricsDir / "scheduler_summary.csv").string();
    paths.eventsJsonl = (metricsDir / "scheduler_events.jsonl").string();
    paths.samplesCsv = (metricsDir / "scheduler_samples.csv").string();
    return paths;
}

struct TreeRunStats {
    std::atomic<std::uint64_t> completedThisRun{0};
    std::atomic<std::uint64_t> skippedFiles{0};
    std::atomic<std::uint64_t> transferredBytes{0};
    std::atomic<std::uint64_t> wireBytes{0};
    std::atomic<std::uint64_t> compressionAttempts{0};
    std::atomic<std::uint64_t> compressedFrames{0};
    std::atomic<std::uint64_t> rawFallbackFrames{0};
    std::atomic<std::uint64_t> compressionFailures{0};
    std::atomic<std::uint64_t> decompressionFailures{0};
    std::atomic<std::uint64_t> compressedLogicalBytes{0};
    std::atomic<std::uint64_t> compressedWireBytes{0};
    std::atomic<std::uint64_t> controlConnectCount{0};
    std::atomic<std::uint64_t> controlReconnectCount{0};
    std::atomic<std::uint64_t> dataTransferCount{0};
    std::atomic<std::uint64_t> phaseASecondsNs{0};
    std::atomic<std::uint64_t> phaseBSecondsNs{0};
    std::atomic<std::uint64_t> phaseCSecondsNs{0};
    std::atomic<std::uint64_t> phaseCWaitSecondsNs{0};
    std::atomic<std::uint64_t> phaseACount{0};
    std::atomic<std::uint64_t> phaseBCount{0};
    std::atomic<std::uint64_t> phaseCCount{0};
    std::atomic<std::uint64_t> phaseCWaitCount{0};
    std::atomic<std::uint64_t> controlPrepareSecondsNs{0};
    std::atomic<std::uint64_t> controlPrepareCount{0};
    std::atomic<std::uint64_t> pipelineSlotCount{0};
    std::atomic<std::uint64_t> pipelinePendingHighWatermark{0};
};

std::uint64_t phaseNanos(std::chrono::steady_clock::duration duration) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

void addTransferMetrics(TreeRunStats* stats, const core::io::TransferRuntimeMetrics* metrics) {
    if (stats == nullptr || metrics == nullptr) {
        return;
    }
    stats->wireBytes.fetch_add(metrics->wireBytes);
    stats->compressionAttempts.fetch_add(metrics->compressionAttempts);
    stats->compressedFrames.fetch_add(metrics->compressedFrames);
    stats->rawFallbackFrames.fetch_add(metrics->rawFallbackFrames);
    stats->compressionFailures.fetch_add(metrics->compressionFailures);
    stats->decompressionFailures.fetch_add(metrics->decompressionFailures);
    stats->compressedLogicalBytes.fetch_add(metrics->compressedLogicalBytes);
    stats->compressedWireBytes.fetch_add(metrics->compressedWireBytes);
}

struct TreeSummary {
    std::uint64_t completedFiles = 0;
    std::uint64_t failedFiles = 0;
    std::uint64_t changedFiles = 0;
};

std::string hex32(std::uint32_t value) {
    std::ostringstream output;
    output << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << value;
    return output.str();
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (ch < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(ch) << std::dec;
                } else {
                    output << static_cast<char>(ch);
                }
                break;
        }
    }
    return output.str();
}

struct ChangedErrorDetails {
    std::string changedPath;
    std::string manifestSize;
    std::string manifestMtime;
    std::string currentSize;
    std::string currentMtime;
};

ChangedErrorDetails parseChangedError(const std::string& message) {
    ChangedErrorDetails details;
    const std::string marker = " manifest_size=";
    const std::size_t markerPos = message.find(marker);
    if (markerPos != std::string::npos) {
        const std::size_t colon = message.rfind(": ", markerPos);
        details.changedPath =
            message.substr(colon == std::string::npos ? 0 : colon + 2,
                           markerPos - (colon == std::string::npos ? 0 : colon + 2));
    }
    std::istringstream input(message);
    std::string token;
    while (input >> token) {
        const std::size_t equals = token.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = token.substr(0, equals);
        const std::string value = token.substr(equals + 1);
        if (key == "manifest_size") {
            details.manifestSize = value;
        } else if (key == "manifest_mtime") {
            details.manifestMtime = value;
        } else if (key == "current_size") {
            details.currentSize = value;
        } else if (key == "current_mtime") {
            details.currentMtime = value;
        }
    }
    return details;
}

common::Result<std::string> computeTreeVerificationHash(const std::string& root) {
    std::error_code error;
    const std::filesystem::path rootPath(root);
    if (!std::filesystem::exists(rootPath, error) || error ||
        !std::filesystem::is_directory(rootPath, error) || error) {
        return common::Status::invalidArgument("tree hash root is not a directory");
    }
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator iterator(
             rootPath, std::filesystem::directory_options::none, error);
         iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(error)) {
        if (error) {
            return common::Status::systemError("tree hash scan failed: " + error.message(),
                                               error.value());
        }
        const auto& entry = *iterator;
        if (entry.is_symlink(error)) {
            return common::Status::invalidArgument("tree hash rejects symlink");
        }
        if (error) {
            return common::Status::systemError("tree hash entry failed: " + error.message(),
                                               error.value());
        }
        if (entry.is_regular_file(error)) {
            const std::filesystem::path relative =
                std::filesystem::relative(entry.path(), rootPath, error);
            if (error) {
                return common::Status::systemError("tree hash relative failed: " + error.message(),
                                                   error.value());
            }
            const std::string relativeText = relative.generic_string();
            if (relativeText.find(".cpnetflux.") != std::string::npos ||
                relativeText.find(".part.") != std::string::npos) {
                continue;
            }
            files.push_back(entry.path());
        } else if (!entry.is_directory(error)) {
            return common::Status::invalidArgument("tree hash rejects non-regular file");
        }
    }
    std::sort(files.begin(), files.end(), [&](const auto& left, const auto& right) {
        return std::filesystem::relative(left, rootPath).generic_string() <
               std::filesystem::relative(right, rootPath).generic_string();
    });

    checksum::ChecksumComputer computer(checksum::ChecksumAlgorithm::Crc32c,
                                        checksum::ChecksumBackend::Software);
    std::vector<std::uint8_t> buffer(1024 * 1024);
    for (const auto& path : files) {
        const std::filesystem::path relative = std::filesystem::relative(path, rootPath, error);
        if (error) {
            return common::Status::systemError("tree hash relative failed: " + error.message(),
                                               error.value());
        }
        const std::string relativeText = relative.generic_string();
        const std::uint64_t size = std::filesystem::file_size(path, error);
        if (error) {
            return common::Status::systemError("tree hash file size failed: " + error.message(),
                                               error.value());
        }
        const std::string sizeText = std::to_string(size);
        computer.update(reinterpret_cast<const std::uint8_t*>(relativeText.data()),
                        relativeText.size());
        const std::uint8_t zero = 0;
        computer.update(&zero, 1);
        computer.update(reinterpret_cast<const std::uint8_t*>(sizeText.data()), sizeText.size());
        computer.update(&zero, 1);
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return common::Status::runtimeError("tree hash failed to open file: " + path.string());
        }
        while (input) {
            input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize read = input.gcount();
            if (read > 0) {
                computer.update(buffer.data(), static_cast<std::size_t>(read));
            }
        }
        if (!input.eof()) {
            return common::Status::runtimeError("tree hash read failed: " + path.string());
        }
        computer.update(&zero, 1);
    }
    return "crc32c:" + hex32(computer.finalize().value);
}

TreeSummary summarizeManifest(const core::tree::TreeManifest& manifest) {
    TreeSummary summary;
    for (const auto& file : manifest.files) {
        switch (file.status) {
            case core::tree::TreeFileStatus::Completed:
                ++summary.completedFiles;
                break;
            case core::tree::TreeFileStatus::Failed:
                ++summary.failedFiles;
                break;
            case core::tree::TreeFileStatus::Changed:
                ++summary.changedFiles;
                break;
            case core::tree::TreeFileStatus::Pending:
            case core::tree::TreeFileStatus::Transferring:
                break;
        }
    }
    return summary;
}

void printTreeSummary(const char* label, const char* result,
                      const core::tree::TreeManifest& manifest, const TreeRunStats& stats,
                      const config::TreeTransferOptions& options,
                      std::chrono::steady_clock::time_point startedAt) {
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt)
                             .count();
    const std::uint64_t logicalBytes = totalBytes(manifest);
    const double throughputGbps =
        elapsed > 0.0 ? static_cast<double>(logicalBytes) * 8.0 / elapsed / 1'000'000'000.0 : 0.0;
    const TreeSummary summary = summarizeManifest(manifest);
    std::cout << label << " result=" << result << " file_count=" << manifest.files.size()
              << " completed_files=" << summary.completedFiles
              << " skipped_files=" << stats.skippedFiles.load()
              << " failed_files=" << summary.failedFiles
              << " changed_files=" << summary.changedFiles
              << " active_file_parallelism=" << options.fileParallelism
              << " control_reuse_mode=" << config::controlReuseModeName(options.controlReuseMode)
              << " control_connect_count=" << stats.controlConnectCount.load()
              << " control_reconnect_count=" << stats.controlReconnectCount.load()
              << " data_transfer_count=" << stats.dataTransferCount.load()
              << " total_bytes=" << logicalBytes
              << " transferred_bytes=" << stats.transferredBytes.load()
              << " elapsed_seconds=" << elapsed << " throughput_gbps=" << throughputGbps
              << '\n';
}

void emitTreeEvent(const config::TreeTransferOptions& options, const char* event,
                   const char* direction, const std::string& path,
                   const common::Status& status, std::uint64_t bytes = 0) {
    (void)core::metrics::writeEventLog(
        options.eventLogPath,
        core::metrics::EventRecord{std::string("cpnetflux-tree-") + direction + "-client",
                                   event,
                                   "",
                                   direction,
                                   path,
                                   status.isOk() ? "pass" : "fail",
                                   core::metrics::classifyStatus(status),
                                   status.isOk() ? "" : status.message(),
                                   0.0,
                                   bytes});
}

common::Status writeTreeJsonSummary(const char* direction, const char* result,
                                    const config::TreeTransferOptions& options,
                                    const core::tree::TreeManifest& manifest,
                                    const TreeRunStats& stats,
                                    std::chrono::steady_clock::time_point startedAt,
                                    const common::Status& status) {
    if (options.jsonSummaryPath.empty()) {
        return common::Status::ok();
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt)
                             .count();
    const std::uint64_t logicalBytes = totalBytes(manifest);
    const double throughputGbps =
        elapsed > 0.0 ? static_cast<double>(logicalBytes) * 8.0 / elapsed / 1'000'000'000.0 : 0.0;
    const TreeSummary summary = summarizeManifest(manifest);
    std::string treeHash;
    const std::string hashRoot = std::string(direction) == "upload" ? options.sourceDir : options.destDir;
    auto hash = computeTreeVerificationHash(hashRoot);
    if (hash.isOk()) {
        treeHash = hash.value();
    }

    const std::filesystem::path outputPath(options.jsonSummaryPath);
    std::error_code error;
    if (!outputPath.parent_path().empty()) {
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            return common::Status::systemError("create JSON summary directory failed: " +
                                                   error.message(),
                                               error.value());
        }
    }
    std::ofstream output(outputPath, std::ios::trunc);
    if (!output) {
        return common::Status::runtimeError("failed to open JSON summary: " + outputPath.string());
    }
    output << "{\n";
    output << "  \"direction\": \"" << jsonEscape(direction) << "\",\n";
    output << "  \"source\": \"" << jsonEscape(options.sourceDir) << "\",\n";
    output << "  \"dest\": \"" << jsonEscape(options.destDir) << "\",\n";
    output << "  \"file_count\": " << manifest.files.size() << ",\n";
    output << "  \"completed_files\": " << summary.completedFiles << ",\n";
    output << "  \"skipped_files\": " << stats.skippedFiles.load() << ",\n";
    output << "  \"failed_files\": " << summary.failedFiles << ",\n";
    output << "  \"changed_files\": " << summary.changedFiles << ",\n";
    output << "  \"bytes_total\": " << logicalBytes << ",\n";
    output << "  \"bytes_transferred\": " << stats.transferredBytes.load() << ",\n";
    output << "  \"wire_bytes\": " << stats.wireBytes.load() << ",\n";
    output << "  \"file_parallelism\": " << options.fileParallelism << ",\n";
    output << "  \"connections\": " << options.connections << ",\n";
    output << "  \"scheduler_mode\": \"" << config::treeSchedulerModeName(options.schedulerMode)
           << "\",\n";
    output << "  \"scheduler_policy\": \""
           << core::scheduler::schedulerPolicyName(options.schedulerPolicy) << "\",\n";
    if (options.schedulerMode == config::TreeSchedulerMode::Global) {
        auto metricsPaths = schedulerMetricsPaths(options, std::string(direction) == "upload"
                                                              ? core::tree::treeManifestPathForUpload(options.sourceDir)
                                                              : core::tree::treeManifestPathForDownload(options.destDir),
                                                  direction);
        if (metricsPaths.isOk()) {
            output << "  \"scheduler_metrics_dir\": \""
                   << jsonEscape(std::filesystem::path(metricsPaths.value().summaryCsv)
                                     .parent_path()
                                     .string())
                   << "\",\n";
            output << "  \"scheduler_summary_csv\": \""
                   << jsonEscape(metricsPaths.value().summaryCsv) << "\",\n";
            output << "  \"scheduler_events_jsonl\": \""
                   << jsonEscape(metricsPaths.value().eventsJsonl) << "\",\n";
            output << "  \"scheduler_samples_csv\": \""
                   << jsonEscape(metricsPaths.value().samplesCsv) << "\",\n";
        }
    }
    output << "  \"control_reuse_mode\": \""
           << config::controlReuseModeName(options.controlReuseMode) << "\",\n";
    output << "  \"control_connect_count\": " << stats.controlConnectCount.load() << ",\n";
    output << "  \"control_reconnect_count\": " << stats.controlReconnectCount.load() << ",\n";
    output << "  \"data_transfer_count\": " << stats.dataTransferCount.load() << ",\n";
    output << "  \"planner_preset\": \"" << jsonEscape(options.plannerPreset) << "\",\n";
    output << "  \"checksum_algorithm\": \""
           << checksum::checksumAlgorithmName(options.checksumAlgorithm) << "\",\n";
    output << "  \"checksum_backend\": \"" << checksum::checksumBackendName(options.checksumBackend)
           << "\",\n";
    output << "  \"resume\": " << (options.resume ? "true" : "false") << ",\n";
    output << "  \"elapsed_seconds\": " << elapsed << ",\n";
    output << "  \"throughput_gbps\": " << throughputGbps << ",\n";
    if (options.controlPipelineDepth != 0) {
        output << "  \"control_pipeline_depth\": " << options.controlPipelineDepth << ",\n";
        output << "  \"control_pipeline_slot_count\": " << stats.pipelineSlotCount.load() << ",\n";
        output << "  \"control_pipeline_pending_high_watermark\": "
               << stats.pipelinePendingHighWatermark.load() << ",\n";
    }
    if (options.phaseTiming) {
        output << "  \"phase_timing_schema\": \"phase_timing_v1\",\n";
        output << "  \"phase_timing_units\": \"seconds\",\n";
        output << "  \"phase_a_count\": " << stats.phaseACount.load() << ",\n";
        output << "  \"phase_a_seconds\": " << static_cast<double>(stats.phaseASecondsNs.load()) / 1e9 << ",\n";
        output << "  \"phase_b_count\": " << stats.phaseBCount.load() << ",\n";
        output << "  \"phase_b_seconds\": " << static_cast<double>(stats.phaseBSecondsNs.load()) / 1e9 << ",\n";
        output << "  \"phase_c_count\": " << stats.phaseCCount.load() << ",\n";
        output << "  \"phase_c_seconds\": " << static_cast<double>(stats.phaseCSecondsNs.load()) / 1e9 << ",\n";
        output << "  \"phase_c_wait_count\": " << stats.phaseCWaitCount.load() << ",\n";
        output << "  \"phase_c_wait_seconds\": " << static_cast<double>(stats.phaseCWaitSecondsNs.load()) / 1e9 << ",\n";
        output << "  \"control_prepare_count\": " << stats.controlPrepareCount.load() << ",\n";
        output << "  \"control_prepare_seconds\": ";
        if (stats.controlPrepareCount.load() == 0) {
            output << "null";
        } else {
            output << static_cast<double>(stats.controlPrepareSecondsNs.load()) / 1e9;
        }
        output << ",\n";
        output << "  \"transfer_complete_wait_count\": " << stats.phaseCWaitCount.load() << ",\n";
        output << "  \"transfer_complete_wait_seconds\": ";
        if (stats.phaseCWaitCount.load() == 0) {
            output << "null";
        } else {
            output << static_cast<double>(stats.phaseCWaitSecondsNs.load()) / 1e9;
        }
        output << ",\n";
    }
    output << "  \"result\": \"" << jsonEscape(result) << "\",\n";
    output << "  \"error_code\": \""
           << core::metrics::errorCodeName(core::metrics::classifyStatus(status)) << "\",\n";
    output << "  \"tree_hash\": \"" << jsonEscape(treeHash) << "\",\n";
    if (status.isOk()) {
        output << "  \"error\": null\n";
    } else {
        const ChangedErrorDetails details = parseChangedError(status.message());
        output << "  \"error\": {\n";
        output << "    \"error_code\": \""
               << core::metrics::errorCodeName(core::metrics::classifyStatus(status))
               << "\",\n";
        output << "    \"message\": \"" << jsonEscape(status.message()) << "\"";
        if (!details.changedPath.empty()) {
            output << ",\n    \"changed_path\": \"" << jsonEscape(details.changedPath) << "\"";
        }
        if (!details.manifestSize.empty()) {
            output << ",\n    \"manifest_size\": \"" << jsonEscape(details.manifestSize) << "\"";
        }
        if (!details.manifestMtime.empty()) {
            output << ",\n    \"manifest_mtime\": \"" << jsonEscape(details.manifestMtime) << "\"";
        }
        if (!details.currentSize.empty()) {
            output << ",\n    \"current_size\": \"" << jsonEscape(details.currentSize) << "\"";
        }
        if (!details.currentMtime.empty()) {
            output << ",\n    \"current_mtime\": \"" << jsonEscape(details.currentMtime) << "\"";
        }
        output << "\n  }\n";
    }
    output << "}\n";
    if (!output) {
        return common::Status::runtimeError("failed to write JSON summary: " + outputPath.string());
    }
    return common::Status::ok();
}

common::Status emitTreeSummary(const char* label, const char* direction,
                               const common::Status& status,
                               const config::TreeTransferOptions& options,
                               const core::tree::TreeManifest& manifest,
                               const TreeRunStats& stats,
                               std::chrono::steady_clock::time_point startedAt) {
    const char* result = status.isOk() ? "pass" : "fail";
    printTreeSummary(label, result, manifest, stats, options, startedAt);
    const common::Status jsonStatus =
        writeTreeJsonSummary(direction, result, options, manifest, stats, startedAt, status);
    if (!jsonStatus.isOk()) {
        return jsonStatus;
    }
    return status;
}

struct SchedulerState {
    core::tree::TreeManifest* manifest = nullptr;
    std::string manifestPath;
    std::mutex mutex;
    std::size_t nextIndex = 0;
    std::uint64_t startedTransfers = 0;
    bool stop = false;
    bool stoppedByMaxFiles = false;
    std::atomic<bool> pipelineCancelRequested{false};
    common::Status firstError = common::Status::ok();
    TreeRunStats stats;
    bool phaseTiming = false;
};

void recordControlPrepare(SchedulerState* state,
                          std::chrono::steady_clock::time_point started) {
    if (state != nullptr && state->phaseTiming) {
        state->stats.controlPrepareCount.fetch_add(1, std::memory_order_relaxed);
        state->stats.controlPrepareSecondsNs.fetch_add(
            phaseNanos(std::chrono::steady_clock::now() - started),
            std::memory_order_relaxed);
    }
}

void setFirstErrorLocked(SchedulerState* state, common::Status status) {
    if (status.isOk()) {
        return;
    }
    if (state->firstError.isOk()) {
        state->firstError = std::move(status);
    }
    state->stop = true;
}

common::Status updateRecord(SchedulerState* state, std::size_t index,
                            core::tree::TreeFileStatus status, std::string error = "") {
    const auto started = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(state->mutex);
    auto& record = state->manifest->files[index];
    record.status = status;
    record.error = std::move(error);
    const common::Status saveStatus = saveManifest(state->manifest, state->manifestPath);
    if (state->phaseTiming) {
        state->stats.phaseBCount.fetch_add(1, std::memory_order_relaxed);
        state->stats.phaseBSecondsNs.fetch_add(phaseNanos(std::chrono::steady_clock::now() - started), std::memory_order_relaxed);
    }
    if (!saveStatus.isOk()) {
        setFirstErrorLocked(state, saveStatus);
    }
    return saveStatus;
}

common::Status updateRecordForTransfer(SchedulerState* state, std::size_t index,
                                       std::string transferId) {
    const auto started = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(state->mutex);
    auto& record = state->manifest->files[index];
    record.transferId = std::move(transferId);
    record.status = core::tree::TreeFileStatus::Transferring;
    record.error.clear();
    const common::Status saveStatus = saveManifest(state->manifest, state->manifestPath);
    if (state->phaseTiming) {
        state->stats.phaseBCount.fetch_add(1, std::memory_order_relaxed);
        state->stats.phaseBSecondsNs.fetch_add(phaseNanos(std::chrono::steady_clock::now() - started), std::memory_order_relaxed);
    }
    if (!saveStatus.isOk()) {
        setFirstErrorLocked(state, saveStatus);
    }
    return saveStatus;
}

bool acquireTransferSlot(SchedulerState* state, const config::TreeTransferOptions& options) {
    if (options.maxFiles == 0) {
        return true;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->startedTransfers >= options.maxFiles) {
        state->stoppedByMaxFiles = true;
        state->stop = true;
        return false;
    }
    ++state->startedTransfers;
    return true;
}

struct TreeWorkerRuntime {
    ControlClient control;
    bool controlReady = false;
    core::io::TransferRuntimeMetrics transferMetrics;
    bool hasTransferMetrics = false;
    core::io::HotPathCompressionOptions hotPathCompression;
    bool forcedRawRetry = false;
};

struct PreparedTransfer;

struct PipelineControlSlot {
    ControlClient control;
    bool ready = false;
    std::uint64_t generation = 0;
    std::size_t index = 0;
    const PreparedTransfer* owner = nullptr;
};

struct PreparedTransfer {
    detail::TreePipelineCandidateIdentity identity;
    PipelineControlSlot* slot = nullptr;
    ControlClient* control = nullptr;
    std::uint16_t dataPort = 0;
    std::string transferId;
    std::atomic<bool> cancelRequested{false};
    SchedulerState* owner = nullptr;
    common::Status status = common::Status::ok();
};

common::Status validatePreparedCandidate(SchedulerState* state,
                                         const PreparedTransfer* prepared,
                                         std::size_t index, bool upload) {
    if (state == nullptr || prepared == nullptr || prepared->slot == nullptr ||
        prepared->control != &prepared->slot->control || !prepared->slot->ready ||
        prepared->slot->owner != prepared) {
        return common::Status::runtimeError("tree pipeline candidate control identity is invalid");
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->manifest == nullptr || index >= state->manifest->files.size()) {
        return common::Status::runtimeError("tree pipeline candidate manifest index is stale");
    }
    const auto& record = state->manifest->files[index];
    if (record.status != core::tree::TreeFileStatus::Pending ||
        !core::tree::validateTreeRelativePath(record.relativePath).isOk() ||
        !detail::treePipelineCandidateIdentityMatches(
            prepared->identity, index, record.relativePath, upload,
            prepared->slot->generation, prepared->slot->index)) {
        return common::Status::runtimeError("tree pipeline candidate identity is stale");
    }
    return common::Status::ok();
}

void resetPipelineSlot(PipelineControlSlot* slot) noexcept {
    if (slot == nullptr) {
        return;
    }
    slot->control.cancel();
    slot->control.reset();
    slot->ready = false;
    slot->owner = nullptr;
    ++slot->generation;
}

void cancelPreparedTransfer(PreparedTransfer* prepared) noexcept {
    if (prepared == nullptr) {
        return;
    }
    prepared->cancelRequested.store(true, std::memory_order_release);
    if (prepared->control != nullptr) {
        prepared->control->cancel();
    }
}

common::Status candidateCancelled(PreparedTransfer* prepared) {
    if (prepared != nullptr &&
        (prepared->cancelRequested.load(std::memory_order_acquire) ||
         (prepared->owner != nullptr &&
          prepared->owner->pipelineCancelRequested.load(std::memory_order_acquire)))) {
        prepared->control->cancel();
        return common::Status::runtimeError("tree pipeline candidate cancelled");
    }
    return common::Status::ok();
}

void mergeTransferMetrics(core::io::TransferRuntimeMetrics* destination,
                          const core::io::TransferRuntimeMetrics& source) {
    destination->elapsedSeconds += source.elapsedSeconds;
    destination->cpuPercent += source.cpuPercent;
    destination->sendSeconds += source.sendSeconds;
    destination->recvSeconds += source.recvSeconds;
    destination->readSeconds += source.readSeconds;
    destination->writeSeconds += source.writeSeconds;
    destination->checksumSeconds += source.checksumSeconds;
    destination->logicalBytes += source.logicalBytes;
    destination->wireBytes += source.wireBytes;
    destination->compressionAttempts += source.compressionAttempts;
    destination->compressedFrames += source.compressedFrames;
    destination->rawFallbackFrames += source.rawFallbackFrames;
    destination->compressionFailures += source.compressionFailures;
    destination->decompressionFailures += source.decompressionFailures;
    destination->compressedLogicalBytes += source.compressedLogicalBytes;
    destination->compressedWireBytes += source.compressedWireBytes;
    if (destination->compressionFallbackReason.empty()) {
        destination->compressionFallbackReason = source.compressionFallbackReason;
    }
}

common::Status ensureControlReadyWithStats(ControlClient* client,
                                           const config::TreeTransferOptions& options,
                                           TreeRunStats* stats) {
    const auto started = std::chrono::steady_clock::now();
    const common::Status status = ensureControlReady(client, options);
    if (stats != nullptr) {
        stats->phaseACount.fetch_add(1, std::memory_order_relaxed);
        stats->phaseASecondsNs.fetch_add(phaseNanos(std::chrono::steady_clock::now() - started), std::memory_order_relaxed);
        if (status.isOk()) {
            stats->controlConnectCount.fetch_add(1);
        }
    }
    return status;
}

common::Result<ControlClient*> controlForFile(SchedulerState* state, TreeWorkerRuntime* runtime,
                                              const config::TreeTransferOptions& options,
                                              ControlClient* localControl) {
    if (options.controlReuseMode == config::ControlReuseMode::Worker) {
        if (runtime == nullptr) {
            return common::Status::runtimeError("tree control reuse worker runtime is missing");
        }
        if (!runtime->controlReady) {
            const common::Status status =
                ensureControlReadyWithStats(&runtime->control, options, &state->stats);
            if (!status.isOk()) {
                return status;
            }
            runtime->controlReady = true;
        } else if (runtime->control.parallelism() != options.connections) {
            const common::Status status = runtime->control.setParallelism(options.connections);
            if (!status.isOk()) {
                return status;
            }
        }
        return &runtime->control;
    }

    const common::Status status =
        ensureControlReadyWithStats(localControl, options, &state->stats);
    if (!status.isOk()) {
        return status;
    }
    return localControl;
}

common::Status nextWorkItem(SchedulerState* state, std::size_t* index, bool* available) {
    if (state == nullptr || index == nullptr || available == nullptr) {
        return common::Status::invalidArgument("tree scheduler work-item output is null");
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->stop) {
        return common::Status::runtimeError("tree scheduler stopped");
    }
    if (state->nextIndex >= state->manifest->files.size()) {
        *available = false;
        return common::Status::ok();
    }
    *index = state->nextIndex++;
    *available = true;
    return common::Status::ok();
}

void markChanged(SchedulerState* state, std::size_t index, const std::string& message) {
    (void)updateRecord(state, index, core::tree::TreeFileStatus::Changed, message);
    std::lock_guard<std::mutex> lock(state->mutex);
    setFirstErrorLocked(state, common::Status::invalidArgument(message));
}

common::Status markManifestChanged(core::tree::TreeManifest* manifest,
                                   const std::string& manifestPath, std::size_t index,
                                   const std::string& message) {
    manifest->files[index].status = core::tree::TreeFileStatus::Changed;
    manifest->files[index].error = message;
    const common::Status saveStatus = saveManifest(manifest, manifestPath);
    if (!saveStatus.isOk()) {
        return saveStatus;
    }
    return common::Status::invalidArgument(message);
}

common::Status preflightUploadResume(const config::TreeTransferOptions& options,
                                     core::tree::TreeManifest* manifest,
                                     const std::string& manifestPath) {
    if (!options.resume) {
        return common::Status::ok();
    }
    auto scanned = core::tree::scanLocalTree(options.sourceDir);
    if (!scanned.isOk()) {
        return scanned.status();
    }
    std::unordered_map<std::string, FileMetadata> current;
    current.reserve(scanned.value().size());
    for (const auto& file : scanned.value()) {
        current.emplace(file.relativePath, FileMetadata{file.size, file.mtimeUnixSeconds});
    }
    for (std::size_t index = 0; index < manifest->files.size(); ++index) {
        const auto& record = manifest->files[index];
        auto found = current.find(record.relativePath);
        if (found == current.end()) {
            const std::string message =
                changedMissingMessage("source file changed", record, "missing from source tree");
            return markManifestChanged(manifest, manifestPath, index, message);
        }
        if (!metadataMatches(record, found->second)) {
            const std::string message = changedMessage("source file changed", record, found->second);
            return markManifestChanged(manifest, manifestPath, index, message);
        }
    }
    for (const auto& [relativePath, metadata] : current) {
        const auto found = std::find_if(
            manifest->files.begin(), manifest->files.end(), [&](const core::tree::TreeFileRecord& record) {
                return record.relativePath == relativePath;
            });
        if (found == manifest->files.end()) {
            std::ostringstream message;
            message << "source tree changed: " << relativePath
                    << " manifest_size=missing manifest_mtime=missing current_size="
                    << metadata.size << " current_mtime=" << metadata.mtimeUnixSeconds;
            return common::Status::invalidArgument(message.str());
        }
    }
    return common::Status::ok();
}

common::Status preflightDownloadResume(const config::TreeTransferOptions& options,
                                       core::tree::TreeManifest* manifest,
                                       const std::string& manifestPath,
                                       TreeRunStats* stats) {
    if (!options.resume) {
        return common::Status::ok();
    }
    ControlClient control;
    const common::Status readyStatus = ensureControlReadyWithStats(&control, options, stats);
    if (!readyStatus.isOk()) {
        return readyStatus;
    }
    for (std::size_t index = 0; index < manifest->files.size(); ++index) {
        const auto& record = manifest->files[index];
        const std::string remotePath = joinRemotePath(options.sourceDir, record.relativePath);
        auto remoteSize = control.size(remotePath);
        if (!remoteSize.isOk()) {
            const std::string message =
                changedMissingMessage("remote file changed", record, remoteSize.status().message());
            return markManifestChanged(manifest, manifestPath, index, message);
        }
        auto remoteMtime = control.mdtm(remotePath);
        if (!remoteMtime.isOk()) {
            const std::string message =
                changedMissingMessage("remote file changed", record, remoteMtime.status().message());
            return markManifestChanged(manifest, manifestPath, index, message);
        }
        const FileMetadata remoteMetadata{remoteSize.value(), remoteMtime.value()};
        if (!metadataMatches(record, remoteMetadata)) {
            const std::string message = changedMessage("remote file changed", record, remoteMetadata);
            return markManifestChanged(manifest, manifestPath, index, message);
        }
        if (record.status == core::tree::TreeFileStatus::Completed) {
            const std::filesystem::path localPath =
                std::filesystem::path(options.destDir) / record.relativePath;
            auto localMetadata = statRegularFile(localPath);
            if (!localMetadata.isOk()) {
                const std::string message = changedMissingMessage(
                    "completed download file changed", record, localMetadata.status().message());
                return markManifestChanged(manifest, manifestPath, index, message);
            }
            if (!metadataMatches(record, localMetadata.value())) {
                const std::string message =
                    changedMessage("completed download file changed", record, localMetadata.value());
                return markManifestChanged(manifest, manifestPath, index, message);
            }
        }
    }
    return common::Status::ok();
}

common::Status checkCandidatePending(SchedulerState* state, std::size_t index) {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (index >= state->manifest->files.size() ||
        state->manifest->files[index].status != core::tree::TreeFileStatus::Pending) {
        return common::Status::runtimeError("tree pipeline candidate is not pending");
    }
    const common::Status pathStatus =
        core::tree::validateTreeRelativePath(state->manifest->files[index].relativePath);
    return pathStatus;
}

common::Status prepareUploadCandidate(SchedulerState* state, std::size_t index,
                                      const config::TreeTransferOptions& options,
                                      PreparedTransfer* prepared) {
    const common::Status pending = checkCandidatePending(state, index);
    if (!pending.isOk()) {
        return pending;
    }
    const common::Status identity = validatePreparedCandidate(state, prepared, index, true);
    if (!identity.isOk()) {
        return identity;
    }
    const auto record = state->manifest->files[index];
    const std::filesystem::path localPath =
        std::filesystem::path(options.sourceDir) / record.relativePath;
    auto metadata = statRegularFile(localPath);
    if (!metadata.isOk()) {
        return metadata.status();
    }
    if (!metadataMatches(record, metadata.value())) {
        return common::Status::invalidArgument(
            changedMessage("source file changed", record, metadata.value()));
    }
    auto cancelled = candidateCancelled(prepared);
    if (!cancelled.isOk()) {
        return cancelled;
    }
    const common::Status ready = common::Status::ok();
    if (!ready.isOk()) {
        return ready;
    }
    cancelled = candidateCancelled(prepared);
    if (!cancelled.isOk()) {
        return cancelled;
    }
    if (!acquireTransferSlot(state, options)) {
        return common::Status::runtimeError("tree pipeline candidate stopped after --max-files");
    }
    cancelled = candidateCancelled(prepared);
    if (!cancelled.isOk()) {
        return cancelled;
    }
    auto transfer = prepared->control->startTransferWithPassive(
        "STOR", joinRemotePath(options.destDir, record.relativePath));
    if (!transfer.isOk()) {
        return transfer.status();
    }
    cancelled = candidateCancelled(prepared);
    if (!cancelled.isOk()) {
        return cancelled;
    }
    prepared->dataPort = transfer.value().dataPort;
    prepared->transferId = transfer.value().transferId;
    return common::Status::ok();
}

common::Status prepareDownloadCandidate(SchedulerState* state, std::size_t index,
                                        const config::TreeTransferOptions& options,
                                        PreparedTransfer* prepared) {
    const common::Status pending = checkCandidatePending(state, index);
    if (!pending.isOk()) {
        return pending;
    }
    const common::Status identity = validatePreparedCandidate(state, prepared, index, false);
    if (!identity.isOk()) {
        return identity;
    }
    const auto record = state->manifest->files[index];
    const std::string remotePath = joinRemotePath(options.sourceDir, record.relativePath);
    auto cancelled = candidateCancelled(prepared);
    if (!cancelled.isOk()) {
        return cancelled;
    }
    const common::Status ready = common::Status::ok();
    if (!ready.isOk()) {
        return ready;
    }
    cancelled = candidateCancelled(prepared);
    if (!cancelled.isOk()) {
        return cancelled;
    }
    auto remoteSize = prepared->control->size(remotePath);
    if (!remoteSize.isOk()) {
        return remoteSize.status();
    }
    auto remoteMtime = prepared->control->mdtm(remotePath);
    if (!remoteMtime.isOk()) {
        return remoteMtime.status();
    }
    const FileMetadata remoteMetadata{remoteSize.value(), remoteMtime.value()};
    if (!metadataMatches(record, remoteMetadata)) {
        return common::Status::invalidArgument(
            changedMessage("remote file changed", record, remoteMetadata));
    }
    if (!acquireTransferSlot(state, options)) {
        return common::Status::runtimeError("tree pipeline candidate stopped after --max-files");
    }
    cancelled = candidateCancelled(prepared);
    if (!cancelled.isOk()) {
        return cancelled;
    }
    auto transfer = prepared->control->startTransferWithPassive("RETR", remotePath);
    if (!transfer.isOk()) {
        return transfer.status();
    }
    cancelled = candidateCancelled(prepared);
    if (!cancelled.isOk()) {
        return cancelled;
    }
    prepared->dataPort = transfer.value().dataPort;
    prepared->transferId = transfer.value().transferId;
    return common::Status::ok();
}

common::Status processUploadFileImpl(
    SchedulerState* state, std::size_t index, const config::TreeTransferOptions& options,
    TreeWorkerRuntime* runtime, PreparedTransfer* prepared,
    const std::function<void()>& onDataStart,
    const std::function<void()>& onDataEnd) {
    if (runtime != nullptr) {
        runtime->transferMetrics = {};
        runtime->hasTransferMetrics = false;
        runtime->forcedRawRetry = false;
    }
    const core::tree::TreeFileRecord record = state->manifest->files[index];
    const std::filesystem::path localPath = std::filesystem::path(options.sourceDir) / record.relativePath;
    const std::string remotePath = joinRemotePath(options.destDir, record.relativePath);
    emitTreeEvent(options, "file_start", "upload", record.relativePath, common::Status::ok(),
                  record.size);

    auto metadata = statRegularFile(localPath);
    if (!metadata.isOk()) {
        const std::string message =
            changedMissingMessage("source file changed", record, metadata.status().message());
        markChanged(state, index, message);
        auto status = common::Status::invalidArgument(message);
        emitTreeEvent(options, "file_changed", "upload", record.relativePath, status,
                      record.size);
        return status;
    }
    if (options.resume && !metadataMatches(record, metadata.value())) {
        const std::string message = changedMessage("source file changed", record, metadata.value());
        markChanged(state, index, message);
        auto status = common::Status::invalidArgument(message);
        emitTreeEvent(options, "file_changed", "upload", record.relativePath, status,
                      record.size);
        return status;
    }

    const auto controlPrepareStarted = std::chrono::steady_clock::now();
    ControlClient localControl;
    ControlClient* controlClient = nullptr;
    if (prepared != nullptr) {
        const common::Status identity = validatePreparedCandidate(state, prepared, index, true);
        if (!identity.isOk()) {
            return identity;
        }
        if (!prepared->status.isOk()) {
            return prepared->status;
        }
        controlClient = prepared->control;
    } else {
        auto control = controlForFile(state, runtime, options, &localControl);
        if (!control.isOk()) {
            return control.status();
        }
        controlClient = control.value();
    }

    if (record.status == core::tree::TreeFileStatus::Completed) {
        const common::Status status =
            validateCompletedUploadFile(controlClient, remotePath, record);
        if (!status.isOk()) {
            const std::string message = changedMissingMessage("completed upload file changed", record,
                                                              status.message());
            markChanged(state, index, message);
            return common::Status::invalidArgument(message);
        }
        state->stats.skippedFiles.fetch_add(1);
        emitTreeEvent(options, "file_skipped", "upload", record.relativePath,
                      common::Status::ok(), record.size);
        return common::Status::ok();
    }

    std::uint16_t dataPort = 0;
    std::string effectiveTransferId;
    bool resumeFile = false;
    if (prepared != nullptr) {
        dataPort = prepared->dataPort;
        effectiveTransferId = prepared->transferId;
    } else {
        if (!acquireTransferSlot(state, options)) {
            return common::Status::runtimeError("tree upload stopped after --max-files");
        }
        resumeFile = options.resume && record.status != core::tree::TreeFileStatus::Pending;
        if (resumeFile) {
            const common::Status restStatus = controlClient->rest(record.transferId);
            if (!restStatus.isOk()) {
                return restStatus;
            }
        }
        auto transfer = controlClient->startTransferWithPassive("STOR", remotePath);
        if (!transfer.isOk()) {
            (void)updateRecord(state, index, core::tree::TreeFileStatus::Failed,
                               transfer.status().message());
            return transfer.status();
        }
        dataPort = transfer.value().dataPort;
        effectiveTransferId = resumeFile ? record.transferId : transfer.value().transferId;
    }
    if (prepared == nullptr) {
        recordControlPrepare(state, controlPrepareStarted);
    }
    const common::Status saveStatus = updateRecordForTransfer(state, index, effectiveTransferId);
    if (!saveStatus.isOk()) {
        return saveStatus;
    }

    config::FileTransferOptions fileOptions;
    fileOptions.host = options.host;
    fileOptions.port = dataPort;
    fileOptions.connections = options.connections;
    fileOptions.bufferSize = options.bufferSize;
    fileOptions.chunkSize = options.chunkSize;
    fileOptions.path = localPath.string();
    fileOptions.transferId = effectiveTransferId;
    fileOptions.checksumAlgorithm = options.checksumAlgorithm;
    fileOptions.checksumBackend = options.checksumBackend;
    fileOptions.dataTls = options.tls;
    fileOptions.dataTls.mode = options.dataTlsMode == core::io::DataTlsMode::Required
                                   ? core::io::TlsMode::Required
                                   : core::io::TlsMode::Off;
    fileOptions.dataTlsMode = options.dataTlsMode;
    fileOptions.resume = resumeFile;
    fileOptions.hotPathCompression =
        runtime != nullptr ? runtime->hotPathCompression : core::io::HotPathCompressionOptions{};
    core::io::TransferRuntimeMetrics transferMetrics;
    fileOptions.runtimeMetrics = &transferMetrics;

    if (onDataStart) {
        onDataStart();
    }
    const auto transferStarted = std::chrono::steady_clock::now();
    common::Status transferStatus = runFileTransferClient(fileOptions);
    if (onDataEnd) {
        onDataEnd();
    }
    if (state->phaseTiming) {
        state->stats.phaseCCount.fetch_add(1, std::memory_order_relaxed);
        state->stats.phaseCSecondsNs.fetch_add(phaseNanos(std::chrono::steady_clock::now() - transferStarted), std::memory_order_relaxed);
    }
    if (runtime != nullptr && options.schedulerMode == config::TreeSchedulerMode::Global) {
        runtime->transferMetrics = transferMetrics;
        runtime->hasTransferMetrics = true;
    }
    if (!transferStatus.isOk() && runtime != nullptr &&
        runtime->hotPathCompression.candidate && !runtime->forcedRawRetry) {
        const common::Status firstControlStatus = controlClient->waitTransferComplete(effectiveTransferId);
        if (!firstControlStatus.isOk() &&
            firstControlStatus.code() == common::StatusCode::SystemError) {
            (void)updateRecord(state, index, core::tree::TreeFileStatus::Failed,
                               transferStatus.message());
            emitTreeEvent(options, "file_failed", "upload", record.relativePath, transferStatus,
                          record.size);
            return transferStatus;
        }

        auto retryPort = controlClient->epsv();
        if (retryPort.isOk()) {
            const common::Status restStatus = controlClient->rest(effectiveTransferId);
            if (restStatus.isOk()) {
                auto retryTransfer =
                    controlClient->startTransfer("STOR", remotePath);
                if (retryTransfer.isOk()) {
                    config::FileTransferOptions retryOptions = fileOptions;
                    retryOptions.port = retryPort.value();
                    retryOptions.resume = true;
                    retryOptions.hotPathCompression.forceRaw = true;
                    retryOptions.hotPathCompression.candidate = true;
                    core::io::TransferRuntimeMetrics retryMetrics;
                    retryOptions.runtimeMetrics = &retryMetrics;
                    const auto retryStarted = std::chrono::steady_clock::now();
                    transferStatus = runFileTransferClient(retryOptions);
                    if (state->phaseTiming) {
                        state->stats.phaseCCount.fetch_add(1, std::memory_order_relaxed);
                        state->stats.phaseCSecondsNs.fetch_add(phaseNanos(std::chrono::steady_clock::now() - retryStarted), std::memory_order_relaxed);
                    }
                    mergeTransferMetrics(&transferMetrics, retryMetrics);
                    runtime->transferMetrics = transferMetrics;
                    runtime->hasTransferMetrics = true;
                    runtime->forcedRawRetry = true;
                }
            }
        }
    }
    if (!transferStatus.isOk()) {
        (void)updateRecord(state, index, core::tree::TreeFileStatus::Failed,
                           transferStatus.message());
        emitTreeEvent(options, "file_failed", "upload", record.relativePath, transferStatus,
                      record.size);
        return transferStatus;
    }
    state->stats.dataTransferCount.fetch_add(1);
    const auto waitStarted = std::chrono::steady_clock::now();
    const common::Status completeStatus = controlClient->waitTransferComplete(effectiveTransferId);
    if (state->phaseTiming) {
        state->stats.phaseCWaitCount.fetch_add(1, std::memory_order_relaxed);
        state->stats.phaseCWaitSecondsNs.fetch_add(phaseNanos(std::chrono::steady_clock::now() - waitStarted), std::memory_order_relaxed);
    }
    if (!completeStatus.isOk()) {
        (void)updateRecord(state, index, core::tree::TreeFileStatus::Failed,
                           completeStatus.message());
        emitTreeEvent(options, "file_failed", "upload", record.relativePath, completeStatus,
                      record.size);
        return completeStatus;
    }
    if (runtime == nullptr || options.schedulerMode != config::TreeSchedulerMode::Global) {
        addTransferMetrics(&state->stats, &transferMetrics);
    }
    const common::Status doneStatus = updateRecord(state, index, core::tree::TreeFileStatus::Completed);
    if (!doneStatus.isOk()) {
        return doneStatus;
    }
    state->stats.completedThisRun.fetch_add(1);
    state->stats.transferredBytes.fetch_add(record.size);
    emitTreeEvent(options, "file_complete", "upload", record.relativePath, common::Status::ok(),
                  record.size);
    return common::Status::ok();
}

common::Status processDownloadFileImpl(
    SchedulerState* state, std::size_t index, const config::TreeTransferOptions& options,
    TreeWorkerRuntime* runtime, PreparedTransfer* prepared,
    const std::function<void()>& onDataStart,
    const std::function<void()>& onDataEnd) {
    if (runtime != nullptr) {
        runtime->transferMetrics = {};
        runtime->hasTransferMetrics = false;
    }
    const core::tree::TreeFileRecord record = state->manifest->files[index];
    const std::filesystem::path localPath = std::filesystem::path(options.destDir) / record.relativePath;
    const std::string remotePath = joinRemotePath(options.sourceDir, record.relativePath);
    emitTreeEvent(options, "file_start", "download", record.relativePath, common::Status::ok(),
                  record.size);

    const auto controlPrepareStarted = std::chrono::steady_clock::now();
    ControlClient localControl;
    ControlClient* controlClient = nullptr;
    if (prepared != nullptr) {
        const common::Status identity = validatePreparedCandidate(state, prepared, index, false);
        if (!identity.isOk()) {
            return identity;
        }
        if (!prepared->status.isOk()) {
            return prepared->status;
        }
        controlClient = prepared->control;
    } else {
        auto control = controlForFile(state, runtime, options, &localControl);
        if (!control.isOk()) {
            return control.status();
        }
        controlClient = control.value();
    }

    if (record.status == core::tree::TreeFileStatus::Completed) {
        auto metadata = statRegularFile(localPath);
        if (!metadata.isOk()) {
            const std::string message = changedMissingMessage("completed download file changed",
                                                              record, metadata.status().message());
            markChanged(state, index, message);
            auto status = common::Status::invalidArgument(message);
            emitTreeEvent(options, "file_changed", "download", record.relativePath, status,
                          record.size);
            return status;
        }
        if (!metadataMatches(record, metadata.value())) {
            const std::string message =
                changedMessage("completed download file changed", record, metadata.value());
            markChanged(state, index, message);
            auto status = common::Status::invalidArgument(message);
            emitTreeEvent(options, "file_changed", "download", record.relativePath, status,
                          record.size);
            return status;
        }
        state->stats.skippedFiles.fetch_add(1);
        emitTreeEvent(options, "file_skipped", "download", record.relativePath,
                      common::Status::ok(), record.size);
        return common::Status::ok();
    }

    if (prepared == nullptr) {
        auto remoteSize = controlClient->size(remotePath);
        if (!remoteSize.isOk()) {
            return remoteSize.status();
        }
        auto remoteMtime = controlClient->mdtm(remotePath);
        if (!remoteMtime.isOk()) {
            return remoteMtime.status();
        }
        const FileMetadata remoteMetadata{remoteSize.value(), remoteMtime.value()};
        if (options.resume && !metadataMatches(record, remoteMetadata)) {
        const std::string message = changedMessage("remote file changed", record, remoteMetadata);
        markChanged(state, index, message);
        auto status = common::Status::invalidArgument(message);
        emitTreeEvent(options, "file_changed", "download", record.relativePath, status,
                      record.size);
            return status;
        }
    }

    if (prepared == nullptr && !acquireTransferSlot(state, options)) {
        return common::Status::runtimeError("tree download stopped after --max-files");
    }

    const common::Status parentStatus = createParentDirectory(localPath);
    if (!parentStatus.isOk()) {
        return parentStatus;
    }
    std::uint16_t dataPort = 0;
    std::string effectiveTransferId;
    bool resumeFile = false;
    if (prepared != nullptr) {
        dataPort = prepared->dataPort;
        effectiveTransferId = prepared->transferId;
    } else {
        resumeFile = options.resume && record.status != core::tree::TreeFileStatus::Pending;
        if (resumeFile) {
            const common::Status restStatus = controlClient->rest(record.transferId);
            if (!restStatus.isOk()) {
                return restStatus;
            }
        }
        auto transfer = controlClient->startTransferWithPassive("RETR", remotePath);
        if (!transfer.isOk()) {
            (void)updateRecord(state, index, core::tree::TreeFileStatus::Failed,
                               transfer.status().message());
            return transfer.status();
        }
        dataPort = transfer.value().dataPort;
        effectiveTransferId = resumeFile ? record.transferId : transfer.value().transferId;
    }
    if (prepared == nullptr) {
        recordControlPrepare(state, controlPrepareStarted);
    }
    const common::Status saveStatus = updateRecordForTransfer(state, index, effectiveTransferId);
    if (!saveStatus.isOk()) {
        return saveStatus;
    }

    config::FileDownloadOptions fileOptions;
    fileOptions.host = options.host;
    fileOptions.port = dataPort;
    fileOptions.connections = options.connections;
    fileOptions.bufferSize = options.bufferSize;
    fileOptions.path = localPath.string();
    fileOptions.transferId = effectiveTransferId;
    fileOptions.checksumAlgorithm = options.checksumAlgorithm;
    fileOptions.checksumBackend = options.checksumBackend;
    fileOptions.dataTls = options.tls;
    fileOptions.dataTls.mode = options.dataTlsMode == core::io::DataTlsMode::Required
                                   ? core::io::TlsMode::Required
                                   : core::io::TlsMode::Off;
    fileOptions.dataTlsMode = options.dataTlsMode;
    fileOptions.resume = resumeFile;
    fileOptions.hotPathCompression = {};
    core::io::TransferRuntimeMetrics transferMetrics;
    fileOptions.runtimeMetrics = &transferMetrics;

    if (onDataStart) {
        onDataStart();
    }
    const auto transferStarted = std::chrono::steady_clock::now();
    const common::Status transferStatus = runFileDownloadClient(fileOptions);
    if (onDataEnd) {
        onDataEnd();
    }
    if (state->phaseTiming) {
        state->stats.phaseCCount.fetch_add(1, std::memory_order_relaxed);
        state->stats.phaseCSecondsNs.fetch_add(phaseNanos(std::chrono::steady_clock::now() - transferStarted), std::memory_order_relaxed);
    }
    if (runtime != nullptr && options.schedulerMode == config::TreeSchedulerMode::Global) {
        runtime->transferMetrics = transferMetrics;
        runtime->hasTransferMetrics = true;
    }
    if (!transferStatus.isOk()) {
        (void)updateRecord(state, index, core::tree::TreeFileStatus::Failed,
                           transferStatus.message());
        emitTreeEvent(options, "file_failed", "download", record.relativePath, transferStatus,
                      record.size);
        return transferStatus;
    }
    state->stats.dataTransferCount.fetch_add(1);
    const auto waitStarted = std::chrono::steady_clock::now();
    const common::Status completeStatus = controlClient->waitTransferComplete(effectiveTransferId);
    if (state->phaseTiming) {
        state->stats.phaseCWaitCount.fetch_add(1, std::memory_order_relaxed);
        state->stats.phaseCWaitSecondsNs.fetch_add(phaseNanos(std::chrono::steady_clock::now() - waitStarted), std::memory_order_relaxed);
    }
    if (!completeStatus.isOk()) {
        (void)updateRecord(state, index, core::tree::TreeFileStatus::Failed,
                           completeStatus.message());
        emitTreeEvent(options, "file_failed", "download", record.relativePath, completeStatus,
                      record.size);
        return completeStatus;
    }
    if (runtime == nullptr || options.schedulerMode != config::TreeSchedulerMode::Global) {
        addTransferMetrics(&state->stats, &transferMetrics);
    }
    const common::Status mtimeStatus = setRegularFileMtime(localPath, record.mtimeUnixSeconds);
    if (!mtimeStatus.isOk()) {
        (void)updateRecord(state, index, core::tree::TreeFileStatus::Failed,
                           mtimeStatus.message());
        emitTreeEvent(options, "file_failed", "download", record.relativePath, mtimeStatus,
                      record.size);
        return mtimeStatus;
    }
    const common::Status doneStatus = updateRecord(state, index, core::tree::TreeFileStatus::Completed);
    if (!doneStatus.isOk()) {
        return doneStatus;
    }
    state->stats.completedThisRun.fetch_add(1);
    state->stats.transferredBytes.fetch_add(record.size);
    emitTreeEvent(options, "file_complete", "download", record.relativePath,
                  common::Status::ok(), record.size);
    return common::Status::ok();
}

common::Status processUploadFile(SchedulerState* state, std::size_t index,
                                 const config::TreeTransferOptions& options,
                                 TreeWorkerRuntime* runtime) {
    return processUploadFileImpl(state, index, options, runtime, nullptr, {}, {});
}

common::Status processDownloadFile(SchedulerState* state, std::size_t index,
                                   const config::TreeTransferOptions& options,
                                   TreeWorkerRuntime* runtime) {
    return processDownloadFileImpl(state, index, options, runtime, nullptr, {}, {});
}

std::uint64_t workItemBytes(const core::scheduler::FilePlan& plan) {
    std::uint64_t bytes = 0;
    for (const auto& item : plan.workItems) {
        bytes += item.length;
    }
    return bytes;
}

struct GlobalSchedulerRuntime {
    std::vector<core::scheduler::FilePlan> plans;
    std::size_t nextPlan = 0;
    std::uint64_t readyBytes = 0;
    std::uint64_t rawWorkItems = 0;
    std::uint64_t compressedWorkItems = 0;
    std::uint64_t retriedWorkItems = 0;
};

struct GlobalDispatch {
    bool hasPlan = false;
    bool waitForCapacity = false;
    core::scheduler::FilePlan plan;
    std::uint32_t targetConnections = 1;
};

void tallyGlobalPlans(const std::vector<core::scheduler::FilePlan>& plans,
                      GlobalSchedulerRuntime* runtime) {
    for (const auto& plan : plans) {
        runtime->readyBytes += workItemBytes(plan);
        const std::uint64_t itemCount = plan.workItems.size();
        if (plan.compression.disposition ==
            core::scheduler::CompressionDisposition::CompressionCandidate) {
            runtime->compressedWorkItems += itemCount;
        } else {
            runtime->rawWorkItems += itemCount;
        }
        if (plan.retryCount > 0) {
            runtime->retriedWorkItems += itemCount;
        }
    }
}

core::scheduler::SchedulerEventRecord schedulerEvent(
    const char* event, const core::scheduler::SchedulerConfig& config,
    const core::scheduler::FilePlan& plan, std::uint64_t offset, std::uint64_t length,
    const std::string& reason, std::uint64_t readyBytes, std::uint32_t targetConnections,
    const std::string& result, const std::string& message) {
    return core::scheduler::SchedulerEventRecord{"",
                                                 event,
                                                 config.taskId,
                                                 plan.fileId,
                                                 config.linkId,
                                                 offset,
                                                 length,
                                                 reason,
                                                 readyBytes,
                                                 targetConnections,
                                                 result,
                                                 message};
}

double phasePressure(double phaseSeconds, double elapsedSeconds) noexcept {
    if (phaseSeconds <= 0.0 || elapsedSeconds <= 0.0) {
        return 0.0;
    }
    return std::min(1.0, phaseSeconds / elapsedSeconds);
}

common::Status recordPlanDispatch(core::scheduler::GlobalScheduler* scheduler,
                                  const core::scheduler::FilePlan& plan,
                                  std::uint64_t readyBytes,
                                  std::uint32_t targetConnections) {
    const auto& config = scheduler->config();
    if (plan.compression.disposition ==
        core::scheduler::CompressionDisposition::CompressionCandidate) {
        const common::Status compressionStatus = scheduler->recordDispatch(
            schedulerEvent("compression_dispatch", config, plan, 0, plan.totalBytes,
                           "worthwhile", readyBytes, targetConnections, "candidate",
                           "file executor will attempt bounded compression"));
        if (!compressionStatus.isOk()) {
            return compressionStatus;
        }
    }
    bool first = true;
    for (const auto& item : plan.workItems) {
        const std::string message = first ? "file_executor_dispatch" : "delegated_accounting";
        const common::Status status = scheduler->recordDispatch(
            schedulerEvent("workitem_dispatch", config, plan, item.offset, item.length,
                           item.dispatchable ? "dispatchable" : "delegated_accounting",
                           readyBytes, targetConnections, "scheduled", message));
        if (!status.isOk()) {
            return status;
        }
        first = false;
    }
    if (plan.retryCount > 0) {
        return scheduler->recordRetry(
            schedulerEvent("workitem_retry", config, plan, 0, plan.totalBytes, "resume_retry",
                           readyBytes, targetConnections, "scheduled", "resume_or_failed_file"));
    }
    return common::Status::ok();
}

common::Result<GlobalDispatch> nextGlobalDispatch(SchedulerState* state,
                                                  GlobalSchedulerRuntime* runtime,
                                                  core::scheduler::GlobalScheduler* scheduler) {
    std::lock_guard<std::mutex> lock(state->mutex);
    GlobalDispatch dispatch;
    if (state->stop) {
        return common::Status::runtimeError("tree scheduler stopped");
    }
    if (runtime->nextPlan >= runtime->plans.size()) {
        return dispatch;
    }

    core::scheduler::LinkManager& manager = scheduler->linkManager();
    const std::string& linkId = scheduler->config().linkId;
    const common::Status readyStatus = manager.setReadyBytes(linkId, runtime->readyBytes);
    if (!readyStatus.isOk()) {
        return readyStatus;
    }
    core::scheduler::LinkState* link = manager.link(linkId);
    if (link == nullptr) {
        return common::Status::runtimeError("scheduler link state missing");
    }
    const bool paused = manager.shouldPause(*link);
    const common::Status pauseStatus = manager.setPaused(linkId, paused);
    if (!pauseStatus.isOk()) {
        return pauseStatus;
    }
    link = manager.link(linkId);
    core::scheduler::FeedbackDecision decision =
        scheduler->feedbackController().evaluate(*link, link->cpuPressure,
                                                 link->writePressure, link->sendPressure,
                                                 link->retryCount);
    const std::uint64_t previousLowHits = link->lowWatermarkHits;
    const std::uint64_t previousHighHits = link->highWatermarkHits;
    const common::Status watermarkStatus =
        manager.recordWatermarkHit(linkId, decision.queueLow, decision.queueHigh);
    if (!watermarkStatus.isOk()) {
        return watermarkStatus;
    }
    const common::Status rampStatus =
        manager.recordRamp(linkId, decision.rampUp, decision.rampDown);
    if (!rampStatus.isOk()) {
        return rampStatus;
    }
    const common::Status targetStatus =
        manager.setTargetConnections(linkId, decision.targetConnections);
    if (!targetStatus.isOk()) {
        return targetStatus;
    }

    link = manager.link(linkId);
    const core::scheduler::FilePlan& plan = runtime->plans[runtime->nextPlan];
    if (decision.queueLow && previousLowHits == 0) {
        const common::Status eventStatus = scheduler->recordLinkEvent(
            schedulerEvent("queue_low", scheduler->config(), plan, 0, 0, decision.reason,
                           runtime->readyBytes, decision.targetConnections, "observed",
                           "ready queue below low watermark"));
        if (!eventStatus.isOk()) {
            return eventStatus;
        }
    }
    if (decision.queueHigh && previousHighHits == 0) {
        const common::Status eventStatus = scheduler->recordLinkEvent(
            schedulerEvent("queue_high", scheduler->config(), plan, 0, 0, decision.reason,
                           runtime->readyBytes, decision.targetConnections, "observed",
                           "ready queue above high watermark"));
        if (!eventStatus.isOk()) {
            return eventStatus;
        }
    }
    if (decision.rampUp || decision.rampDown) {
        const common::Status eventStatus = scheduler->recordLinkEvent(
            schedulerEvent(decision.rampUp ? "ramp_up" : "ramp_down", scheduler->config(),
                           plan, 0, 0, decision.reason, runtime->readyBytes,
                           decision.targetConnections, "applied",
                           decision.rampUp ? "target connections increased"
                                           : "target connections decreased"));
        if (!eventStatus.isOk()) {
            return eventStatus;
        }
    }

    if (decision.paused && link->inflightBytes > 0) {
        dispatch.waitForCapacity = true;
        return dispatch;
    }

    dispatch.hasPlan = true;
    dispatch.plan = plan;
    dispatch.targetConnections = link == nullptr ? decision.targetConnections
                                                 : link->targetConnections;
    const std::uint64_t planBytes = workItemBytes(dispatch.plan);
    runtime->readyBytes = planBytes >= runtime->readyBytes ? 0 : runtime->readyBytes - planBytes;
    const common::Status inflightStatus = manager.addInflightBytes(linkId, planBytes);
    if (!inflightStatus.isOk()) {
        return inflightStatus;
    }
    const common::Status retryStatus =
        dispatch.plan.retryCount > 0 ? manager.addRetryCount(linkId, dispatch.plan.retryCount)
                                     : common::Status::ok();
    if (!retryStatus.isOk()) {
        return retryStatus;
    }
    const common::Status dispatchStatus =
        recordPlanDispatch(scheduler, dispatch.plan, runtime->readyBytes,
                           dispatch.targetConnections);
    if (!dispatchStatus.isOk()) {
        return dispatchStatus;
    }
    ++runtime->nextPlan;
    return dispatch;
}

common::Status completeGlobalDispatch(SchedulerState* state, GlobalSchedulerRuntime* runtime,
                                      core::scheduler::GlobalScheduler* scheduler,
                                      const core::scheduler::FilePlan& plan,
                                      std::uint32_t targetConnections,
                                      const core::io::TransferRuntimeMetrics* transferMetrics,
                                      bool forcedRawRetry,
                                      const common::Status& fileStatus) {
    std::lock_guard<std::mutex> lock(state->mutex);
    addTransferMetrics(&state->stats, transferMetrics);
    const std::string& linkId = scheduler->config().linkId;
    core::scheduler::LinkManager& manager = scheduler->linkManager();
    const std::uint64_t bytes = workItemBytes(plan);
    const common::Status inflightStatus = manager.releaseInflightBytes(linkId, bytes);
    if (!inflightStatus.isOk()) {
        return inflightStatus;
    }
    auto* link = manager.link(linkId);
    if (link == nullptr) {
        return common::Status::runtimeError("scheduler link state missing");
    }
    const bool isUpload =
        !plan.workItems.empty() &&
        plan.workItems.front().direction == core::scheduler::SchedulerDirection::Upload;
    double cpuPressure = transferMetrics == nullptr ? 0.0 : transferMetrics->cpuPercent;
    double writePressure = transferMetrics == nullptr
                                ? 0.0
                                : phasePressure(transferMetrics->writeSeconds,
                                                transferMetrics->elapsedSeconds);
    double sendPressure = transferMetrics == nullptr
                               ? 0.0
                               : phasePressure(transferMetrics->sendSeconds,
                                               transferMetrics->elapsedSeconds);
    if (fileStatus.isOk() && isUpload) {
        writePressure = 0.0;
    }
    if (fileStatus.isOk() && !isUpload) {
        sendPressure = 0.0;
    }
    if (!fileStatus.isOk()) {
        cpuPressure = 0.0;
        writePressure = isUpload ? 0.0 : 1.0;
        sendPressure = isUpload ? 1.0 : 0.0;
    }
    const common::Status pressureStatus =
        manager.updatePressure(linkId, cpuPressure, writePressure, sendPressure);
    if (!pressureStatus.isOk()) {
        return pressureStatus;
    }
    const common::Status readyStatus = manager.setReadyBytes(linkId, runtime->readyBytes);
    if (!readyStatus.isOk()) {
        return readyStatus;
    }
    link = manager.link(linkId);
    if (link == nullptr) {
        return common::Status::runtimeError("scheduler link state missing");
    }
    const std::uint64_t previousLowHits = link->lowWatermarkHits;
    const std::uint64_t previousHighHits = link->highWatermarkHits;
    const bool paused = manager.shouldPause(*link);
    const common::Status pauseStatus = manager.setPaused(linkId, paused);
    if (!pauseStatus.isOk()) {
        return pauseStatus;
    }
    link = manager.link(linkId);
    core::scheduler::FeedbackDecision decision =
        scheduler->feedbackController().evaluate(*link, link->cpuPressure,
                                                 link->writePressure, link->sendPressure,
                                                 link->retryCount);
    const common::Status watermarkStatus =
        manager.recordWatermarkHit(linkId, decision.queueLow, decision.queueHigh);
    if (!watermarkStatus.isOk()) {
        return watermarkStatus;
    }
    const common::Status rampStatus =
        manager.recordRamp(linkId, decision.rampUp, decision.rampDown);
    if (!rampStatus.isOk()) {
        return rampStatus;
    }
    const common::Status targetStatus =
        manager.setTargetConnections(linkId, decision.targetConnections);
    if (!targetStatus.isOk()) {
        return targetStatus;
    }
    if (decision.queueLow && previousLowHits == 0) {
        const common::Status eventStatus = scheduler->recordLinkEvent(
            schedulerEvent("queue_low", scheduler->config(), plan, 0, 0, decision.reason,
                           runtime->readyBytes, decision.targetConnections, "observed",
                           "ready queue below low watermark"));
        if (!eventStatus.isOk()) {
            return eventStatus;
        }
    }
    if (decision.queueHigh && previousHighHits == 0) {
        const common::Status eventStatus = scheduler->recordLinkEvent(
            schedulerEvent("queue_high", scheduler->config(), plan, 0, 0, decision.reason,
                           runtime->readyBytes, decision.targetConnections, "observed",
                           "ready queue above high watermark"));
        if (!eventStatus.isOk()) {
            return eventStatus;
        }
    }
    if (decision.rampUp || decision.rampDown) {
        const common::Status eventStatus = scheduler->recordLinkEvent(
            schedulerEvent(decision.rampUp ? "ramp_up" : "ramp_down", scheduler->config(),
                           plan, 0, 0, decision.reason, runtime->readyBytes,
                           decision.targetConnections, "applied",
                           decision.rampUp ? "target connections increased"
                                           : "target connections decreased"));
        if (!eventStatus.isOk()) {
            return eventStatus;
        }
    }

    if (transferMetrics != nullptr) {
        if (transferMetrics->rawFallbackFrames > 0) {
            const common::Status eventStatus = scheduler->recordLinkEvent(
                schedulerEvent("compression_fallback_raw", scheduler->config(), plan, 0,
                               plan.totalBytes,
                               transferMetrics->compressionFallbackReason.empty()
                                   ? "compression_not_beneficial_or_failed"
                                   : transferMetrics->compressionFallbackReason,
                               runtime->readyBytes, targetConnections, "raw",
                               "DATA frames used raw fallback"));
            if (!eventStatus.isOk()) {
                return eventStatus;
            }
        }
        if (transferMetrics->compressionFailures > 0) {
            const common::Status eventStatus = scheduler->recordLinkEvent(
                schedulerEvent("compression_failed", scheduler->config(), plan, 0,
                               plan.totalBytes, "compression_failure", runtime->readyBytes,
                               targetConnections, "raw_fallback", "compression probe failed"));
            if (!eventStatus.isOk()) {
                return eventStatus;
            }
        }
        if (transferMetrics->decompressionFailures > 0) {
            const common::Status eventStatus = scheduler->recordLinkEvent(
                schedulerEvent("decompression_failed", scheduler->config(), plan, 0,
                               plan.totalBytes, "compressed_payload_invalid",
                               runtime->readyBytes, targetConnections, "fail",
                               "compressed DATA could not be decoded"));
            if (!eventStatus.isOk()) {
                return eventStatus;
            }
        }
    }
    if (forcedRawRetry) {
        runtime->retriedWorkItems += plan.workItems.size();
        const common::Status retryStatus = scheduler->recordRetry(
            schedulerEvent("workitem_retry", scheduler->config(), plan, 0, plan.totalBytes,
                           "compression_failure_raw_retry", runtime->readyBytes,
                           targetConnections, "raw", "resume retry forced raw"));
        if (!retryStatus.isOk()) {
            return retryStatus;
        }
    }
    if (!fileStatus.isOk() &&
        fileStatus.message().find("checksum mismatch") != std::string::npos &&
        plan.compression.disposition ==
            core::scheduler::CompressionDisposition::CompressionCandidate) {
        const common::Status eventStatus = scheduler->recordLinkEvent(
            schedulerEvent("compressed_checksum_mismatch", scheduler->config(), plan, 0,
                           plan.totalBytes, "logical_checksum_mismatch", runtime->readyBytes,
                           targetConnections, "fail", fileStatus.message()));
        if (!eventStatus.isOk()) {
            return eventStatus;
        }
    }

    std::ostringstream pressureMessage;
    pressureMessage << (fileStatus.isOk() ? "file executor completed" : fileStatus.message())
                    << " cpu_pressure=" << cpuPressure
                    << " write_pressure=" << writePressure
                    << " send_pressure=" << sendPressure;
    if (fileStatus.isOk()) {
        return scheduler->recordExecutorComplete(schedulerEvent(
            "executor_complete", scheduler->config(), plan, 0, plan.totalBytes, "file_complete",
            runtime->readyBytes, targetConnections, "pass", pressureMessage.str()));
    }
    const common::Status failedStatus = scheduler->recordFailed(
        schedulerEvent("executor_failed", scheduler->config(), plan, 0, plan.totalBytes,
                       "executor_error", runtime->readyBytes, targetConnections, "fail",
                       pressureMessage.str()));
    if (!failedStatus.isOk()) {
        return failedStatus;
    }
    setFirstErrorLocked(state, fileStatus);
    return common::Status::ok();
}

common::Status writeGlobalSchedulerSummary(core::scheduler::GlobalScheduler* scheduler,
                                           const core::tree::TreeManifest& manifest,
                                           const TreeRunStats& stats,
                                           const GlobalSchedulerRuntime& runtime,
                                           const common::Status& runStatus,
                                           std::chrono::steady_clock::time_point startedAt) {
    const std::uint64_t logicalBytes = totalBytes(manifest);
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
    core::scheduler::SchedulerSummaryRecord summary;
    summary.taskId = scheduler->taskId();
    summary.totalBytes = logicalBytes;
    summary.logicalBytes = logicalBytes;
    summary.wireBytes = stats.wireBytes.load();
    summary.elapsedSeconds = elapsed;
    summary.goodputGbps =
        elapsed > 0.0 ? static_cast<double>(logicalBytes) * 8.0 / elapsed / 1'000'000'000.0 : 0.0;
    summary.wireGbps = elapsed > 0.0
                           ? static_cast<double>(summary.wireBytes) * 8.0 / elapsed /
                                 1'000'000'000.0
                           : 0.0;
    summary.compressionAttempts = stats.compressionAttempts.load();
    summary.compressedWorkItems = stats.compressedFrames.load();
    summary.rawFallbackWorkItems = stats.rawFallbackFrames.load();
    summary.compressionFailures = stats.compressionFailures.load();
    summary.decompressionFailures = stats.decompressionFailures.load();
    summary.rawWorkItems = runtime.rawWorkItems;
    summary.retriedWorkItems = runtime.retriedWorkItems;
    summary.compressionRatioEffective =
        stats.compressedLogicalBytes.load() == 0
            ? 1.0
            : static_cast<double>(stats.compressedWireBytes.load()) /
                  static_cast<double>(stats.compressedLogicalBytes.load());
    summary.result = runStatus.isOk() ? "pass" : "fail";

    const auto* link = scheduler->linkManager().link(scheduler->config().linkId);
    if (link != nullptr) {
        summary.policy = core::scheduler::schedulerPolicyName(scheduler->config().policy);
        summary.initialConnections = scheduler->config().initialConnections;
        summary.currentConnections = link->currentConnections;
        summary.targetConnections = link->targetConnections;
        summary.maxConnections = scheduler->config().maxConnections;
        summary.rampUpCount = link->rampUpCount;
        summary.rampDownCount = link->rampDownCount;
        summary.queueLowCount = link->lowWatermarkHits;
        summary.queueHighCount = link->highWatermarkHits;
        summary.sendPressureCount = link->sendPressureHighCount;
        summary.writePressureCount = link->writePressureHighCount;
        summary.cpuPressureCount = link->cpuPressureHighCount;
        summary.retryCount = link->retryCount;
        summary.maxSendPressure = link->maxSendPressure;
        summary.maxWritePressure = link->maxWritePressure;
        summary.maxCpuPressure = link->maxCpuPressure;
        summary.dominantBottleneck = scheduler->dominantBottleneck(summary, *link);
    }
    return scheduler->writeSummary(summary);
}

common::Status runGlobalTreeScheduler(core::tree::TreeManifest* manifest,
                                      const std::string& manifestPath,
                                      const config::TreeTransferOptions& options,
                                      common::Status (*processFile)(
                                          SchedulerState*, std::size_t,
                                          const config::TreeTransferOptions&,
                                          TreeWorkerRuntime*),
                                      TreeRunStats* stats, const char* direction,
                                      std::chrono::steady_clock::time_point startedAt) {
    SchedulerState state;
    state.manifest = manifest;
    state.manifestPath = manifestPath;
    state.phaseTiming = options.phaseTiming;
    const std::uint64_t completedBase =
        stats != nullptr ? stats->completedThisRun.load() : 0;
    const std::uint64_t skippedBase = stats != nullptr ? stats->skippedFiles.load() : 0;
    const std::uint64_t transferredBase =
        stats != nullptr ? stats->transferredBytes.load() : 0;
    const std::uint64_t controlConnectBase =
        stats != nullptr ? stats->controlConnectCount.load() : 0;
    const std::uint64_t controlReconnectBase =
        stats != nullptr ? stats->controlReconnectCount.load() : 0;
    const std::uint64_t dataTransferBase =
        stats != nullptr ? stats->dataTransferCount.load() : 0;
    const std::uint64_t wireBase = stats != nullptr ? stats->wireBytes.load() : 0;
    const std::uint64_t compressionAttemptsBase =
        stats != nullptr ? stats->compressionAttempts.load() : 0;
    const std::uint64_t compressedFramesBase =
        stats != nullptr ? stats->compressedFrames.load() : 0;
    const std::uint64_t rawFallbackFramesBase =
        stats != nullptr ? stats->rawFallbackFrames.load() : 0;
    const std::uint64_t compressionFailuresBase =
        stats != nullptr ? stats->compressionFailures.load() : 0;
    const std::uint64_t decompressionFailuresBase =
        stats != nullptr ? stats->decompressionFailures.load() : 0;
    const std::uint64_t compressedLogicalBytesBase =
        stats != nullptr ? stats->compressedLogicalBytes.load() : 0;
    const std::uint64_t compressedWireBytesBase =
        stats != nullptr ? stats->compressedWireBytes.load() : 0;
    const auto copyStats = [&]() {
        if (stats != nullptr) {
            stats->completedThisRun.store(completedBase + state.stats.completedThisRun.load());
            stats->skippedFiles.store(skippedBase + state.stats.skippedFiles.load());
            stats->transferredBytes.store(transferredBase + state.stats.transferredBytes.load());
            stats->controlConnectCount.store(controlConnectBase +
                                             state.stats.controlConnectCount.load());
            stats->controlReconnectCount.store(controlReconnectBase +
                                               state.stats.controlReconnectCount.load());
            stats->dataTransferCount.store(dataTransferBase + state.stats.dataTransferCount.load());
            stats->phaseASecondsNs.store(state.stats.phaseASecondsNs.load());
            stats->phaseBSecondsNs.store(state.stats.phaseBSecondsNs.load());
            stats->phaseCSecondsNs.store(state.stats.phaseCSecondsNs.load());
            stats->phaseCWaitSecondsNs.store(state.stats.phaseCWaitSecondsNs.load());
            stats->phaseACount.store(state.stats.phaseACount.load());
            stats->phaseBCount.store(state.stats.phaseBCount.load());
            stats->phaseCCount.store(state.stats.phaseCCount.load());
            stats->phaseCWaitCount.store(state.stats.phaseCWaitCount.load());
            stats->controlPrepareSecondsNs.store(state.stats.controlPrepareSecondsNs.load());
            stats->controlPrepareCount.store(state.stats.controlPrepareCount.load());
            stats->wireBytes.store(wireBase + state.stats.wireBytes.load());
            stats->compressionAttempts.store(compressionAttemptsBase +
                                              state.stats.compressionAttempts.load());
            stats->compressedFrames.store(compressedFramesBase +
                                          state.stats.compressedFrames.load());
            stats->rawFallbackFrames.store(rawFallbackFramesBase +
                                           state.stats.rawFallbackFrames.load());
            stats->compressionFailures.store(compressionFailuresBase +
                                             state.stats.compressionFailures.load());
            stats->decompressionFailures.store(decompressionFailuresBase +
                                               state.stats.decompressionFailures.load());
            stats->compressedLogicalBytes.store(
                compressedLogicalBytesBase + state.stats.compressedLogicalBytes.load());
            stats->compressedWireBytes.store(
                compressedWireBytesBase + state.stats.compressedWireBytes.load());
        }
    };

    auto metricsPaths = schedulerMetricsPaths(options, manifestPath, direction);
    if (!metricsPaths.isOk()) {
        return metricsPaths.status();
    }
    core::scheduler::SchedulerConfig schedulerConfig;
    schedulerConfig.taskId = std::string("tree-") + direction + "-" +
                             std::to_string(manifest->updatedAtUnixNanos);
    schedulerConfig.linkId = options.schedulerLinkId;
    schedulerConfig.remoteHost = options.host;
    schedulerConfig.capacityGbps = options.schedulerCapacityGbps;
    schedulerConfig.policy = options.schedulerPolicy;
    schedulerConfig.initialConnections =
        options.schedulerPolicy == core::scheduler::SchedulerPolicy::Adaptive ? 1
                                                                              : options.connections;
    schedulerConfig.maxConnections = options.connections;
    schedulerConfig.workItemMinBytes = options.schedulerWorkItemMinBytes;
    schedulerConfig.workItemMaxBytes = options.schedulerWorkItemMaxBytes;
    schedulerConfig.defaultRttMs = options.schedulerDefaultRttMs;
    schedulerConfig.minCompressGbps = options.schedulerMinCompressGbps;
    schedulerConfig.enableCompression =
        options.compressionMode == config::CompressionMode::Auto;
    schedulerConfig.metricsPaths = metricsPaths.value();

    auto schedulerResult = core::scheduler::GlobalScheduler::create(std::move(schedulerConfig));
    if (!schedulerResult.isOk()) {
        return schedulerResult.status();
    }
    core::scheduler::GlobalScheduler scheduler = std::move(schedulerResult.value());
    auto plansResult = scheduler.planManifest(
        *manifest,
        std::string(direction) == "upload" ? std::filesystem::path(options.sourceDir)
                                           : std::filesystem::path(options.destDir),
        std::string(direction) == "upload" ? core::scheduler::SchedulerDirection::Upload
                                           : core::scheduler::SchedulerDirection::Download,
        std::string(direction) == "upload");
    if (!plansResult.isOk()) {
        return plansResult.status();
    }

    for (std::size_t index = 0; index < manifest->files.size(); ++index) {
        if (manifest->files[index].status != core::tree::TreeFileStatus::Completed) {
            continue;
        }
        TreeWorkerRuntime runtime;
        const common::Status status = processFile(&state, index, options, &runtime);
        if (!status.isOk()) {
            copyStats();
            const common::Status summaryStatus =
                writeGlobalSchedulerSummary(&scheduler, *manifest, state.stats,
                                            GlobalSchedulerRuntime{}, status, startedAt);
            return summaryStatus.isOk() ? status : summaryStatus;
        }
    }

    GlobalSchedulerRuntime runtime;
    runtime.plans = std::move(plansResult.value());
    tallyGlobalPlans(runtime.plans, &runtime);
    const std::uint32_t workerCount =
        runtime.plans.empty()
            ? 1
            : std::max<std::uint32_t>(
                  1, std::min<std::uint32_t>(
                         options.fileParallelism,
                         static_cast<std::uint32_t>(runtime.plans.size())));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (std::uint32_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back(
            [&state, &runtime, &scheduler, &options, processFile, direction]() {
            TreeWorkerRuntime workerRuntime;
            while (true) {
                auto dispatch = nextGlobalDispatch(&state, &runtime, &scheduler);
                if (!dispatch.isOk()) {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    setFirstErrorLocked(&state, dispatch.status());
                    return;
                }
                if (dispatch.value().waitForCapacity) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                if (!dispatch.value().hasPlan) {
                    return;
                }
                config::TreeTransferOptions dispatchOptions = options;
                dispatchOptions.connections = dispatch.value().targetConnections;
                workerRuntime.hotPathCompression = {};
                if (std::string(direction) == "upload" &&
                    dispatch.value().plan.compression.disposition ==
                        core::scheduler::CompressionDisposition::CompressionCandidate) {
                    workerRuntime.hotPathCompression.enabled = true;
                    workerRuntime.hotPathCompression.candidate = true;
                    workerRuntime.hotPathCompression.maxPayloadBytes =
                        dispatchOptions.bufferSize;
                }
                const common::Status fileStatus =
                    processFile(&state, dispatch.value().plan.manifestIndex, dispatchOptions,
                                &workerRuntime);
                const common::Status completeStatus =
                    completeGlobalDispatch(&state, &runtime, &scheduler, dispatch.value().plan,
                                           dispatch.value().targetConnections,
                                           workerRuntime.hasTransferMetrics
                                               ? &workerRuntime.transferMetrics
                                               : nullptr,
                                           workerRuntime.forcedRawRetry,
                                           fileStatus);
                if (!completeStatus.isOk()) {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    setFirstErrorLocked(&state, completeStatus);
                    return;
                }
                if (!fileStatus.isOk()) {
                    return;
                }
            }
            });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    common::Status status = common::Status::ok();
    if (state.stoppedByMaxFiles) {
        status = common::Status::runtimeError("tree transfer stopped after --max-files");
    } else if (!state.firstError.isOk()) {
        status = state.firstError;
    }
    copyStats();
    const common::Status summaryStatus =
        writeGlobalSchedulerSummary(&scheduler, *manifest, state.stats, runtime, status,
                                    startedAt);
    if (!summaryStatus.isOk()) {
        return summaryStatus;
    }
    return status;
}

common::Status runTreeScheduler(core::tree::TreeManifest* manifest, const std::string& manifestPath,
                                const config::TreeTransferOptions& options,
                                common::Status (*processFile)(SchedulerState*, std::size_t,
                                                              const config::TreeTransferOptions&,
                                                              TreeWorkerRuntime*),
                                TreeRunStats* stats) {
    SchedulerState state;
    state.manifest = manifest;
    state.manifestPath = manifestPath;
    state.phaseTiming = options.phaseTiming;
    const std::uint64_t completedBase =
        stats != nullptr ? stats->completedThisRun.load() : 0;
    const std::uint64_t skippedBase = stats != nullptr ? stats->skippedFiles.load() : 0;
    const std::uint64_t transferredBase =
        stats != nullptr ? stats->transferredBytes.load() : 0;
    const std::uint64_t controlConnectBase =
        stats != nullptr ? stats->controlConnectCount.load() : 0;
    const std::uint64_t controlReconnectBase =
        stats != nullptr ? stats->controlReconnectCount.load() : 0;
    const std::uint64_t dataTransferBase =
        stats != nullptr ? stats->dataTransferCount.load() : 0;
    const auto copyStats = [&]() {
        if (stats != nullptr) {
            stats->completedThisRun.store(completedBase + state.stats.completedThisRun.load());
            stats->skippedFiles.store(skippedBase + state.stats.skippedFiles.load());
            stats->transferredBytes.store(transferredBase + state.stats.transferredBytes.load());
            stats->wireBytes.fetch_add(state.stats.wireBytes.load());
            stats->controlConnectCount.store(controlConnectBase +
                                             state.stats.controlConnectCount.load());
            stats->controlReconnectCount.store(controlReconnectBase +
                                               state.stats.controlReconnectCount.load());
            stats->dataTransferCount.store(dataTransferBase + state.stats.dataTransferCount.load());
            stats->phaseASecondsNs.store(state.stats.phaseASecondsNs.load());
            stats->phaseBSecondsNs.store(state.stats.phaseBSecondsNs.load());
            stats->phaseCSecondsNs.store(state.stats.phaseCSecondsNs.load());
            stats->phaseCWaitSecondsNs.store(state.stats.phaseCWaitSecondsNs.load());
            stats->phaseACount.store(state.stats.phaseACount.load());
            stats->phaseBCount.store(state.stats.phaseBCount.load());
            stats->phaseCCount.store(state.stats.phaseCCount.load());
            stats->phaseCWaitCount.store(state.stats.phaseCWaitCount.load());
            stats->controlPrepareSecondsNs.store(state.stats.controlPrepareSecondsNs.load());
            stats->controlPrepareCount.store(state.stats.controlPrepareCount.load());
        }
    };
    const std::uint32_t workerCount =
        std::max<std::uint32_t>(1, std::min<std::uint32_t>(options.fileParallelism,
                                                          static_cast<std::uint32_t>(
                                                              std::max<std::size_t>(1, manifest->files.size()))));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (std::uint32_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&state, &options, processFile]() {
            TreeWorkerRuntime runtime;
            while (true) {
                std::size_t index = 0;
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (state.stop || state.nextIndex >= state.manifest->files.size()) {
                        return;
                    }
                    index = state.nextIndex++;
                }
                common::Status status = processFile(&state, index, options, &runtime);
                if (!status.isOk()) {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    setFirstErrorLocked(&state, std::move(status));
                    return;
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    if (state.stoppedByMaxFiles) {
        copyStats();
        return common::Status::runtimeError("tree transfer stopped after --max-files");
    }
    if (!state.firstError.isOk()) {
        copyStats();
        return state.firstError;
    }
    copyStats();
    return common::Status::ok();
}

}  // namespace


common::Status runPipelinedTreeScheduler(core::tree::TreeManifest* manifest,
                                        const std::string& manifestPath,
                                        const config::TreeTransferOptions& options,
                                        bool upload, TreeRunStats* stats) {
    const std::uint32_t depth = options.controlPipelineDepth;
    if (depth != 1 && depth != 2 && depth != 4) {
        return common::Status::invalidArgument("tree pipeline depth must be 1, 2, or 4");
    }
    if (manifest->files.empty()) {
        return common::Status::ok();
    }
    SchedulerState state;
    state.manifest = manifest;
    state.manifestPath = manifestPath;
    state.phaseTiming = options.phaseTiming;

    // Depth counts pending files, excluding the current data transfer. A slot
    // remains leased until its terminal reply is consumed (or cancellation is
    // joined). In particular depth=1 really uses two independent connections.
    const std::size_t slotCount = depth + 1;
    std::array<PipelineControlSlot, 5> slots;
    const auto resetAllSlots = [&]() {
        for (std::size_t i = 0; i < slotCount; ++i) {
            resetPipelineSlot(&slots[i]);
        }
    };
    auto copyStats = [&]() {
        if (stats == nullptr) {
            return;
        }
        stats->completedThisRun.fetch_add(state.stats.completedThisRun.load());
        stats->skippedFiles.fetch_add(state.stats.skippedFiles.load());
        stats->transferredBytes.fetch_add(state.stats.transferredBytes.load());
        stats->wireBytes.fetch_add(state.stats.wireBytes.load());
        stats->controlConnectCount.fetch_add(state.stats.controlConnectCount.load());
        stats->controlReconnectCount.fetch_add(state.stats.controlReconnectCount.load());
        stats->dataTransferCount.fetch_add(state.stats.dataTransferCount.load());
        stats->phaseASecondsNs.fetch_add(state.stats.phaseASecondsNs.load());
        stats->phaseBSecondsNs.fetch_add(state.stats.phaseBSecondsNs.load());
        stats->phaseCSecondsNs.fetch_add(state.stats.phaseCSecondsNs.load());
        stats->phaseCWaitSecondsNs.fetch_add(state.stats.phaseCWaitSecondsNs.load());
        stats->phaseACount.fetch_add(state.stats.phaseACount.load());
        stats->phaseBCount.fetch_add(state.stats.phaseBCount.load());
        stats->phaseCCount.fetch_add(state.stats.phaseCCount.load());
        stats->phaseCWaitCount.fetch_add(state.stats.phaseCWaitCount.load());
        stats->controlPrepareSecondsNs.fetch_add(state.stats.controlPrepareSecondsNs.load());
        stats->controlPrepareCount.fetch_add(state.stats.controlPrepareCount.load());
        stats->pipelineSlotCount.store(state.stats.pipelineSlotCount.load());
        stats->pipelinePendingHighWatermark.store(state.stats.pipelinePendingHighWatermark.load());
    };


    for (std::size_t i = 0; i < slotCount; ++i) {
        slots[i].index = i;
        slots[i].control.setResponseTimeout(kPipelineControlResponseTimeout);
        common::Status ready = ensureControlReadyWithStats(&slots[i].control, options, &state.stats);
        if (ready.isOk()) {
            ready = slots[i].control.enablePipeline();
        }
        if (!ready.isOk()) {
            resetAllSlots();
            copyStats();
            return ready;
        }
        slots[i].ready = true;
        state.stats.pipelineSlotCount.fetch_add(1);
    }

    const std::uint32_t workerCount = std::max<std::uint32_t>(1,
        std::min<std::size_t>(options.fileParallelism, manifest->files.size()));
    std::vector<std::unique_ptr<TreeWorkerRuntime>> ordinaryRuntimes;
    std::vector<std::thread> workers;
    try {
        ordinaryRuntimes.reserve(workerCount - 1);
        workers.reserve(workerCount - 1);
        for (std::uint32_t i = 1; i < workerCount; ++i) {
            ordinaryRuntimes.push_back(std::make_unique<TreeWorkerRuntime>());
            ordinaryRuntimes.back()->control.setResponseTimeout(kPipelineControlResponseTimeout);
        }
    } catch (const std::exception& error) {
        resetAllSlots();
        copyStats();
        return common::Status::runtimeError(
            std::string("tree pipeline worker allocation failed: ") + error.what());
    }
    const auto failState = [&](common::Status failure) {
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            setFirstErrorLocked(&state, std::move(failure));
            state.pipelineCancelRequested.store(true, std::memory_order_release);
        }
        // shutdown is safe concurrently with a control read; closing/resetting
        // is deferred until all preparation and file workers have joined.
        for (std::size_t i = 0; i < slotCount; ++i) {
            slots[i].control.cancel();
        }
        for (auto& runtime : ordinaryRuntimes) {
            runtime->control.cancel();
        }
    };

    const auto makePrepared = [&](std::size_t index, PipelineControlSlot* slot) {
        auto prepared = std::make_unique<PreparedTransfer>();
        ++slot->generation;
        prepared->identity = detail::TreePipelineCandidateIdentity{
            index, manifest->files[index].relativePath, upload, slot->generation, slot->index};
        prepared->slot = slot;
        prepared->control = &slot->control;
        prepared->owner = &state;
        slot->owner = prepared.get();
        return prepared;
    };
    const auto prepare = [&](PreparedTransfer* item) {
        const auto started = std::chrono::steady_clock::now();
        try {
            item->status = upload
                ? prepareUploadCandidate(&state, item->identity.manifestIndex, options, item)
                : prepareDownloadCandidate(&state, item->identity.manifestIndex, options, item);
        } catch (const std::exception& error) {
            item->status = common::Status::runtimeError(
                std::string("tree pipeline preparation exception: ") + error.what());
        } catch (...) {
            item->status = common::Status::runtimeError("tree pipeline preparation exception");
        }
        recordControlPrepare(&state, started);
        if (!item->status.isOk()) {
            (void)updateRecord(&state, item->identity.manifestIndex,
                               core::tree::TreeFileStatus::Failed, item->status.message());
            failState(item->status);
        }
    };

    // RAII prevents a thread outliving its candidate even if allocation or
    // thread creation throws while filling the bounded queue.
    struct PendingPreparation {
        std::unique_ptr<PreparedTransfer> transfer;
        std::thread worker;
        ~PendingPreparation() {
            if (worker.joinable()) {
                cancelPreparedTransfer(transfer.get());
                worker.join();
            }
        }
    };
    std::deque<std::unique_ptr<PendingPreparation>> pending;
    std::unique_ptr<PreparedTransfer> current;
    common::Status status = common::Status::ok();
    bool exhausted = false;
    const auto fillPending = [&]() {
        while (!exhausted && pending.size() < depth &&
               !state.pipelineCancelRequested.load(std::memory_order_acquire)) {
            PipelineControlSlot* freeSlot = nullptr;
            for (std::size_t i = 0; i < slotCount; ++i) {
                if (slots[i].owner == nullptr) {
                    freeSlot = &slots[i];
                    break;
                }
            }
            if (freeSlot == nullptr) {
                throw std::runtime_error("tree pipeline has no unleased control slot");
            }
            std::size_t index = 0;
            bool available = false;
            const auto claimed = nextWorkItem(&state, &index, &available);
            if (!claimed.isOk()) {
                failState(claimed);
                return;
            }
            if (!available) {
                exhausted = true;
                return;
            }
            auto item = std::make_unique<PendingPreparation>();
            item->transfer = makePrepared(index, freeSlot);
            PendingPreparation* raw = item.get();
            pending.push_back(std::move(item));
            state.stats.pipelinePendingHighWatermark.store(
                std::max<std::uint64_t>(state.stats.pipelinePendingHighWatermark.load(),
                                       pending.size()));
            raw->worker = std::thread([&, raw]() { prepare(raw->transfer.get()); });
        }
    };

    try {
        std::size_t index = 0;
        bool available = false;
        status = nextWorkItem(&state, &index, &available);
        if (status.isOk() && available) {
            current = makePrepared(index, &slots[0]);
            prepare(current.get());
            status = current->status;
        }
        if (status.isOk() && current != nullptr) {
            // Reserve this lane's pending window before other file workers can
            // consume all remaining manifest rows.
            fillPending();
            for (auto& runtime : ordinaryRuntimes) {
                TreeWorkerRuntime* raw = runtime.get();
                workers.emplace_back([&, raw]() {
                    try {
                        while (!state.pipelineCancelRequested.load(std::memory_order_acquire)) {
                            std::size_t next = 0;
                            bool available = false;
                            auto fileStatus = nextWorkItem(&state, &next, &available);
                            if (!fileStatus.isOk()) {
                                failState(fileStatus);
                                return;
                            }
                            if (!available) {
                                return;
                            }
                            fileStatus = upload
                                ? processUploadFile(&state, next, options, raw)
                                : processDownloadFile(&state, next, options, raw);
                            if (!fileStatus.isOk()) {
                                failState(fileStatus);
                                return;
                            }
                        }
                    } catch (const std::exception& error) {
                        failState(common::Status::runtimeError(
                            std::string("tree pipeline file worker exception: ") + error.what()));
                    } catch (...) {
                        failState(common::Status::runtimeError("tree pipeline file worker exception"));
                    }
                });
            }
        }
        while (status.isOk() && current != nullptr &&
               !state.pipelineCancelRequested.load(std::memory_order_acquire)) {
            TreeWorkerRuntime runtime;
            // Preparation happens on other leased connections. Do not join it
            // in onDataEnd: this file's 226 can be consumed independently.
            status = upload
                ? processUploadFileImpl(&state, current->identity.manifestIndex, options,
                                        &runtime, current.get(), fillPending, {})
                : processDownloadFileImpl(&state, current->identity.manifestIndex, options,
                                          &runtime, current.get(), fillPending, {});
            if (!status.isOk()) {
                break;
            }
            current->slot->owner = nullptr;  // data and terminal reply are complete
            current.reset();
            if (!pending.empty()) {
                auto next = std::move(pending.front());
                pending.pop_front();
                if (next->worker.joinable()) {
                    next->worker.join();
                }
                current = std::move(next->transfer);
                status = current->status;
                if (status.isOk()) {
                    fillPending();
                }
            }
        }
    } catch (const std::exception& error) {
        status = common::Status::runtimeError(
            std::string("tree pipeline scheduling exception: ") + error.what());
    } catch (...) {
        status = common::Status::runtimeError("tree pipeline scheduling exception");
    }

    if (!status.isOk()) {
        failState(status);
    }
    // A failed preparation also cancels the current transfer. Read firstError
    // after joining to preserve the originating reject/timeout diagnostic.
    if (state.pipelineCancelRequested.load(std::memory_order_acquire)) {
        cancelPreparedTransfer(current.get());
        for (auto& item : pending) {
            cancelPreparedTransfer(item->transfer.get());
        }
    }
    pending.clear();
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.firstError.isOk()) {
            status = state.firstError;
        }
    }
    resetAllSlots();
    copyStats();
    return status;
}

common::Status runTreeUploadClient(const config::TreeTransferOptions& options) {
    const auto startedAt = std::chrono::steady_clock::now();
    const std::string manifestPath = core::tree::treeManifestPathForUpload(options.sourceDir);
    core::tree::TreeManifest manifest;
    if (options.resume) {
        auto loaded = core::tree::loadTreeManifest(manifestPath);
        if (!loaded.isOk()) {
            return loaded.status();
        }
        manifest = loaded.value();
        if (manifest.mode != core::tree::TreeTransferMode::Upload ||
            manifest.rootLogicalPath != options.sourceDir ||
            manifest.checksumAlgorithm != options.checksumAlgorithm) {
            return common::Status::invalidArgument("tree upload manifest does not match request");
        }
    } else {
        auto files = core::tree::scanLocalTree(options.sourceDir);
        if (!files.isOk()) {
            return files.status();
        }
        manifest.mode = core::tree::TreeTransferMode::Upload;
        manifest.rootLogicalPath = options.sourceDir;
        manifest.checksumAlgorithm = options.checksumAlgorithm;
        manifest.createdAtUnixNanos = checkpoint::nowUnixNanos();
        manifest.updatedAtUnixNanos = manifest.createdAtUnixNanos;
        for (const auto& file : files.value()) {
            manifest.files.push_back(core::tree::TreeFileRecord{
                file.relativePath, file.size, file.mtimeUnixSeconds, generateTransferId(),
                core::tree::TreeFileStatus::Pending, ""});
        }
        const common::Status saveStatus = saveManifest(&manifest, manifestPath);
        if (!saveStatus.isOk()) {
            return saveStatus;
        }
    }

    const bool useControlPipeline = options.controlPipelineDepth != 0;
    config::TreeTransferOptions summaryOptions = options;
    if (!useControlPipeline) {
        summaryOptions.controlPipelineDepth = 0;
    }

    const common::Status preflightStatus =
        preflightUploadResume(options, &manifest, manifestPath);
    if (!preflightStatus.isOk()) {
        TreeRunStats stats;
        return emitTreeSummary("tree_upload_complete", "upload", preflightStatus, summaryOptions,
                               manifest, stats, startedAt);
    }

    TreeRunStats stats;
    const common::Status status =
        useControlPipeline
            ? runPipelinedTreeScheduler(&manifest, manifestPath, options, true, &stats)
            : (options.schedulerMode == config::TreeSchedulerMode::Global
                   ? runGlobalTreeScheduler(&manifest, manifestPath, options, processUploadFile,
                                            &stats, "upload", startedAt)
                   : runTreeScheduler(&manifest, manifestPath, options, processUploadFile, &stats));
    return emitTreeSummary("tree_upload_complete", "upload", status, summaryOptions, manifest, stats,
                           startedAt);
}

common::Status runTreeDownloadClient(const config::TreeTransferOptions& options) {
    const auto startedAt = std::chrono::steady_clock::now();
    const std::string manifestPath = core::tree::treeManifestPathForDownload(options.destDir);
    core::tree::TreeManifest manifest;
    TreeRunStats stats;
    ControlClient control;
    const common::Status readyStatus = ensureControlReadyWithStats(&control, options, &stats);
    if (!readyStatus.isOk()) {
        return readyStatus;
    }

    if (options.resume) {
        auto loaded = core::tree::loadTreeManifest(manifestPath);
        if (!loaded.isOk()) {
            return loaded.status();
        }
        manifest = loaded.value();
        if (manifest.mode != core::tree::TreeTransferMode::Download ||
            manifest.rootLogicalPath != options.sourceDir ||
            manifest.checksumAlgorithm != options.checksumAlgorithm) {
            return common::Status::invalidArgument("tree download manifest does not match request");
        }
    } else {
        std::vector<std::string> stack{options.sourceDir};
        std::vector<core::tree::TreeFileRecord> records;
        while (!stack.empty()) {
            const std::string current = stack.back();
            stack.pop_back();
            auto names = control.nlst(current);
            if (!names.isOk()) {
                return names.status();
            }
            for (const std::string& name : names.value()) {
                const std::string candidate = joinRemotePath(current, name);
                auto size = control.size(candidate);
                if (size.isOk()) {
                    auto relative = remoteRelativePath(options.sourceDir, candidate);
                    if (!relative.isOk()) {
                        return relative.status();
                    }
                    auto mtime = control.mdtm(candidate);
                    if (!mtime.isOk()) {
                        return mtime.status();
                    }
                    records.push_back(core::tree::TreeFileRecord{
                        relative.value(), size.value(), mtime.value(), generateTransferId(),
                        core::tree::TreeFileStatus::Pending, ""});
                } else {
                    stack.push_back(candidate);
                }
            }
        }
        std::sort(records.begin(), records.end(),
                  [](const auto& left, const auto& right) {
                      return left.relativePath < right.relativePath;
                  });
        manifest.mode = core::tree::TreeTransferMode::Download;
        manifest.rootLogicalPath = options.sourceDir;
        manifest.checksumAlgorithm = options.checksumAlgorithm;
        manifest.createdAtUnixNanos = checkpoint::nowUnixNanos();
        manifest.updatedAtUnixNanos = manifest.createdAtUnixNanos;
        manifest.files = std::move(records);
        const common::Status saveStatus = saveManifest(&manifest, manifestPath);
        if (!saveStatus.isOk()) {
            return saveStatus;
        }
    }

    const bool useControlPipeline = options.controlPipelineDepth != 0;
    config::TreeTransferOptions summaryOptions = options;
    if (!useControlPipeline) {
        summaryOptions.controlPipelineDepth = 0;
    }

    const common::Status preflightStatus =
        preflightDownloadResume(options, &manifest, manifestPath, &stats);
    if (!preflightStatus.isOk()) {
        return emitTreeSummary("tree_download_complete", "download", preflightStatus, summaryOptions,
                               manifest, stats, startedAt);
    }

    const common::Status status =
        useControlPipeline
            ? runPipelinedTreeScheduler(&manifest, manifestPath, options, false, &stats)
            : (options.schedulerMode == config::TreeSchedulerMode::Global
                   ? runGlobalTreeScheduler(&manifest, manifestPath, options, processDownloadFile,
                                            &stats, "download", startedAt)
                   : runTreeScheduler(&manifest, manifestPath, options, processDownloadFile, &stats));
    return emitTreeSummary("tree_download_complete", "download", status, summaryOptions, manifest, stats,
                           startedAt);
}

}  // namespace cpnetflux::core::io
