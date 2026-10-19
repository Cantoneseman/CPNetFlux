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
#include <charconv>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
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
#include <optional>
#include <unordered_set>

#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/config/file_download_options.h"
#include "cpnetflux/config/file_transfer_options.h"
#include "cpnetflux/core/io/file_download_client.h"
#include "cpnetflux/core/io/file_transfer_client.h"
#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/core/io/work_executor.h"
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
                                           const TlsConfig& tls,
                                           const std::string& tlsServerName = {}, SocketDeadline deadline = {}) {
        if (cancelled_.load(std::memory_order_acquire)) {
            return common::Status::runtimeError("tree pipeline candidate cancelled");
        }
        host_ = host;
        deadline_ = deadline;
        auto connection = connectBlockingSocket(host.c_str(), port, deadline);
        if (!connection.isOk()) return connection.status();
        fd_ = std::move(connection.value());
        if (tls.mode == TlsMode::Required) {
            auto context = TlsClientContext::create(tls);
            if (!context.isOk()) {
                return context.status();
            }
            const std::string& identityName = tlsServerName.empty() ? host_ : tlsServerName;
            auto connection = context.value().connect(std::move(fd_), identityName, deadline);
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

    [[nodiscard]] common::Result<bool> batchDrained(const std::string& id) {
        auto reply = command("XCPNETFLUX DRAIN " + id);
        if (!reply.isOk()) return reply.status();
        if (reply.value().code == 200) return true;
        if (reply.value().code == 450) return false;
        return common::Status::invalidArgument("server does not support safe batch drain");
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

    [[nodiscard]] common::Result<bool> negotiatePersistent(std::uint32_t requestedWindow,
        std::uint32_t requestedChannels, checksum::ChecksumAlgorithm checksumAlgorithm,
        bool resume, DataTlsMode dataTlsMode, std::uint32_t* actualWindow,
        std::uint32_t* actualChannels, bool dynamic = false,
        std::uint32_t requestedLinks = 1, std::uint64_t requestedLinkCapabilities = 0) {
        if (requestedWindow == 0 || requestedWindow > 16 ||
            requestedChannels == 0 || requestedChannels > kPersistentTreeMaxChannels ||
            requestedLinks == 0 || requestedLinks > core::scheduler::LinkSet::kMaxLinks ||
            actualWindow == nullptr || actualChannels == nullptr)
            return common::Status::invalidArgument("invalid persistent channel/window request");
        *actualWindow = 1;
        *actualChannels = 1;
        timeval timeout{30, 0};
        if (::setsockopt(control_.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
            ::setsockopt(control_.fd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
            return common::Status::runtimeError("persistent control timeout configuration failed");
        std::string request = "XCPNETFLUX V2";
        if (requestedWindow != 1 || requestedChannels != 1 || dynamic || resume ||
            checksumAlgorithm != checksum::ChecksumAlgorithm::None || dataTlsMode != DataTlsMode::Off) {
            request += " WINDOW=" + std::to_string(requestedWindow);
            if (requestedChannels != 1)
                request += " CHANNELS=" + std::to_string(requestedChannels);
        }
        request += " CHECKSUM=" + std::string(checksum::checksumAlgorithmName(checksumAlgorithm));
        request += " RESUME=" + std::string(resume ? "1" : "0");
        request += " DATA_TLS=" + std::string(dataTlsModeName(dataTlsMode));
        const bool linkSetRequested = requestedLinks != 1 || requestedLinkCapabilities != 0;
        if (linkSetRequested) {
            request += " " + core::scheduler::encodeLinkSetControlFields(
                core::scheduler::LinkSetCapability{requestedLinks, 1, requestedLinkCapabilities});
        }
        if (dynamic) request += " SCHED=dynamic";
        auto reply = command(request);
        if (!reply.isOk()) return reply.status();
        if (reply.value().code == 200) {
            const std::string replyText = joined(reply.value());
            const std::string checksumToken = "checksum=" +
                std::string(checksum::checksumAlgorithmName(checksumAlgorithm));
            const std::string resumeToken = "resume=" + std::string(resume ? "1" : "0");
            const std::string tlsToken = "data_tls=" + std::string(dataTlsModeName(dataTlsMode));
            const std::string linksToken = "links=" + std::to_string(requestedLinks);
            const std::string linkCapsToken = "link_caps=" + std::to_string(requestedLinkCapabilities);
            if ((dynamic && replyText.find(" scheduling=dynamic") == std::string::npos) ||
                (linkSetRequested && (replyText.find(linksToken) == std::string::npos ||
                                      replyText.find(linkCapsToken) == std::string::npos)) ||
                (requestedChannels > 1 &&
                 replyText.find("channels=" + std::to_string(requestedChannels)) == std::string::npos) ||
                replyText.find(checksumToken) == std::string::npos ||
                replyText.find(resumeToken) == std::string::npos ||
                replyText.find(tlsToken) == std::string::npos) {
                timeout = {0, 0};
                (void)::setsockopt(control_.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                (void)::setsockopt(control_.fd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
                return false;
            }
            *actualWindow = requestedWindow;
            *actualChannels = requestedChannels;
            return true;
        }
        if (reply.value().code == 500 || reply.value().code == 502 ||
            reply.value().code == 504 || reply.value().code == 550) {
            timeout = {0, 0};
            (void)::setsockopt(control_.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            (void)::setsockopt(control_.fd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            return false;
        }
        return common::Status::runtimeError("unexpected persistent negotiation reply");
    }
    [[nodiscard]] common::Status startDirectory(bool upload, const std::string& path,
        std::uint32_t channelIndex, std::uint32_t channelCount, std::uint32_t totalFiles,
        std::uint32_t* remoteTotalFiles, std::uint32_t* expectedChannelFiles, const std::string& batchId = {}) {
        if (path.find_first_of("\r\n") != std::string::npos || path.find(char(0)) != std::string::npos)
            return common::Status::invalidArgument("invalid persistent directory argument");
        if (channelCount == 0 || channelIndex >= channelCount ||
            remoteTotalFiles == nullptr || expectedChannelFiles == nullptr)
            return common::Status::invalidArgument("invalid persistent directory channel");
        std::string request;
        if (!batchId.empty()) {
            request = std::string(upload ? "XDIRD PUT " : "XDIRD GET ") + batchId + " " +
                std::to_string(channelIndex) + " " + std::to_string(channelCount) + " " +
                std::to_string(totalFiles) + " " + path;
        } else if (channelCount > 1) {
            request = std::string(upload ? "XDIRP PUT " : "XDIRP GET ") +
                std::to_string(channelIndex) + " " + std::to_string(channelCount) + " " +
                std::to_string(totalFiles) + " " + path;
        } else {
            request = std::string(upload ? "XDIR PUT " : "XDIR GET ") + path;
        }
        auto reply = command(request);
        if (!reply.isOk()) return reply.status();
        if (reply.value().code != 150)
            return common::Status::runtimeError("persistent directory request rejected");
        *remoteTotalFiles = kUnknownPersistentFileCount;
        *expectedChannelFiles = kUnknownPersistentFileCount;
        static const std::regex counts(R"(total_files=([0-9]+) channel_files=([0-9]+))");
        std::smatch match;
        const std::string text = joined(reply.value());
        if (std::regex_search(text, match, counts)) {
            const auto totalText = match[1].str();
            const auto channelText = match[2].str();
            unsigned long total = 0, channel = 0;
            auto parsedTotal = std::from_chars(totalText.data(), totalText.data() + totalText.size(), total);
            auto parsedChannel = std::from_chars(channelText.data(), channelText.data() + channelText.size(), channel);
            if (parsedTotal.ec != std::errc() || parsedTotal.ptr != totalText.data() + totalText.size() ||
                parsedChannel.ec != std::errc() || parsedChannel.ptr != channelText.data() + channelText.size() ||
                total > UINT32_MAX || channel > UINT32_MAX)
                return common::Status::runtimeError("invalid persistent directory file counts");
            *remoteTotalFiles = static_cast<std::uint32_t>(total);
            *expectedChannelFiles = static_cast<std::uint32_t>(channel);
        } else if (channelCount > 1 || !batchId.empty()) {
            return common::Status::runtimeError("persistent server omitted channel file counts");
        }
        return common::Status::ok();
    }
    [[nodiscard]] common::Status waitTransferComplete(
        const std::string& expectedTransferId = {}, std::string* directoryReply = nullptr) {
        // V2 directory completion uses a single untagged control terminal reply.
        // Per-file V3/pipeline completion retains transfer_id matching below.
        if (expectedTransferId.empty()) {
            auto reply = readReply();
            if (!reply.isOk()) return reply.status();
            if (directoryReply) *directoryReply = joined(reply.value());
            return reply.value().code == 226 ? common::Status::ok() :
                common::Status::runtimeError("directory transfer did not complete: " + joined(reply.value()));
        }
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
        auto status = control_.writeAll(text.data(), text.size());
        if (!status.isOk()) return status;
        return status;
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
        if (deadline_ && (!deadline || *deadline_ < *deadline)) deadline = deadline_;
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
    SocketDeadline deadline_;
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


common::Status saveManifest(core::tree::TreeManifest* manifest, const std::string& path) {
    manifest->updatedAtUnixNanos = checkpoint::nowUnixNanos();
    return core::tree::saveTreeManifestAtomic(path, *manifest);
}

common::Status ensureControlReady(ControlClient* client, const config::TreeTransferOptions& options) {
    const common::Status connectStatus =
        client->connectTo(options.host, options.port, options.tls, options.tlsServerName, options.transferDeadline);
    if (!connectStatus.isOk()) {
        return connectStatus;
    }
    if (options.authMode == "token") {
        auto token = cpnetflux::protocol::control::loadTokenFile(options.authTokenFile);
        if (!token.isOk()) {
            return token.status();
        }
        return client->login("token", token.value(), options.connections);
    }
    return client->login(options.user, options.password, options.connections);
}

TreeAuthProbeResult runTreeAuthProbeInternal(const config::TreeTransferOptions& options) {
    TreeAuthProbeResult result;
    ControlClient control;
    const common::Status connectStatus =
        control.connectTo(options.host, options.port, options.tls, options.tlsServerName, options.transferDeadline);
    if (!connectStatus.isOk()) {
        const std::string message = connectStatus.message();
        result.error = message;
        if (message.find("getaddrinfo") != std::string::npos ||
            message.find("connect") != std::string::npos) {
            result.dnsControlPort = "failed";
        } else {
            result.dnsControlPort = "passed";
            result.tlsIdentity = "failed";
        }
        result.authentication = "failed";
        return result;
    }
    result.dnsControlPort = "passed";
    result.tlsIdentity = "passed";

    common::Status loginStatus = common::Status::ok();
    if (options.authMode == "token") {
        auto token = cpnetflux::protocol::control::loadTokenFile(options.authTokenFile);
        if (!token.isOk()) {
            loginStatus = token.status();
        } else {
            loginStatus = control.login("token", token.value(), options.connections);
        }
    } else {
        loginStatus = control.login(options.user, options.password, options.connections);
    }
    if (!loginStatus.isOk()) {
        result.login = "failed";
        result.authentication = "failed";
        result.error = loginStatus.message();
        return result;
    }
    result.login = "passed";
    result.authentication = "authenticated";

    std::uint32_t actualWindow = 0;
    std::uint32_t actualChannels = 0;
    auto negotiated = control.negotiatePersistent(
        1, 1, options.checksumAlgorithm, false, options.dataTlsMode,
        &actualWindow, &actualChannels, false, options.linkSetLinks,
        options.linkSetCapabilityBits);
    if (!negotiated.isOk()) {
        result.capabilities = "failed";
        result.error = negotiated.status().message();
    } else if (negotiated.value()) {
        result.capabilities = "passed";
    } else {
        result.capabilities = "unsupported";
    }
    return result;
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
    std::atomic<std::uint64_t> laneControlReconnectCount{0};
    std::atomic<std::uint64_t> recoveryBudgetUsed{0};
    std::atomic<std::uint64_t> dataTransferCount{0};
    std::atomic<std::uint64_t> laneLocalRecoveryCount{0};
    bool persistentData = false;
    bool perFileStarted = false;
    std::uint64_t persistentDataConnectCount = 0;
    std::vector<std::string> persistentLaneIds;
    std::vector<std::uint64_t> persistentLaneRangeCompletions;
    std::uint64_t resumeSkippedBytes = 0, recoveryPayloadBytes = 0;
    std::uint32_t reconnectAttempts = 0, connectionEpoch = 1;
    std::uint32_t pendingWindow = 1;
    std::uint32_t pendingHighWatermark = 0;
    double controlPrepareSeconds = 0;
    double completeWaitSeconds = 0;
    bool dynamicScheduling = false;
    double dataConnectSeconds = 0, queueWaitSeconds = 0;
    std::optional<double> scanSeconds;
    std::size_t queueHighWatermark = 0;
    std::vector<PersistentFileTiming> fileTimings;
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

class RecoveryBudget {
   public:
    RecoveryBudget(std::uint32_t limit, std::atomic<std::uint64_t>* observed)
        : remaining_(limit), observed_(observed) {}

    bool tryConsume() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (remaining_ == 0) return false;
        --remaining_;
        ++used_;
        if (observed_) observed_->fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool exhausted() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return remaining_ == 0;
    }

    std::uint32_t used() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return used_;
    }

   private:
    mutable std::mutex mutex_;
    std::uint32_t remaining_;
    std::uint32_t used_ = 0;
    std::atomic<std::uint64_t>* observed_;
};

common::Status reconnectBudgetExhausted(const common::Status& cause) {
    std::string message = "reconnect attempt budget exhausted";
    if (!cause.message().empty()) message += ": " + cause.message();
    return common::Status::runtimeError(message);
}

std::uint64_t phaseNanos(std::chrono::steady_clock::duration duration) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
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
    const auto compressionAttempts = stats.compressionAttempts.load();
    const auto compressedFrames = stats.compressedFrames.load();
    const auto rawFallbackFrames = stats.rawFallbackFrames.load();
    std::string compressionDecision = "disabled";
    if (options.compressionMode == config::CompressionMode::Auto) {
        if (compressionAttempts == 0) compressionDecision = "not_observed";
        else if (compressedFrames != 0 && rawFallbackFrames != 0) compressionDecision = "mixed";
        else if (compressedFrames != 0) compressionDecision = "compressed";
        else compressionDecision = "raw_fallback";
    }
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
    output << "  \"resume_skipped_bytes\": " << stats.resumeSkippedBytes << ",\n";
    output << "  \"recovery_payload_bytes\": " << stats.recoveryPayloadBytes << ",\n";
    output << "  \"reconnect_attempts\": " << stats.reconnectAttempts << ",\n";
    output << "  \"reconnect_attempt_budget\": " << options.reconnectAttempts << ",\n";
    output << "  \"recovery_attempts_used\": " << stats.recoveryBudgetUsed.load() << ",\n";
    output << "  \"connection_epoch\": " << stats.connectionEpoch << ",\n";
    output << "  \"compression_policy\": \""
           << (options.compressionMode == config::CompressionMode::Auto ? "auto" : "off") << "\",\n";
    output << "  \"compression_decision\": \"" << compressionDecision << "\",\n";
    output << "  \"compression_attempts\": " << compressionAttempts << ",\n";
    output << "  \"compressed_frames\": " << compressedFrames << ",\n";
    output << "  \"raw_fallback_frames\": " << rawFallbackFrames << ",\n";
    output << "  \"compression_failures\": " << stats.compressionFailures.load() << ",\n";
    output << "  \"compressed_logical_bytes\": " << stats.compressedLogicalBytes.load() << ",\n";
    output << "  \"compressed_wire_bytes\": " << stats.compressedWireBytes.load() << ",\n";
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
    output << "  \"lane_control_reconnect_count\": " << stats.laneControlReconnectCount.load() << ",\n";
    output << "  \"lane_local_recovery_scope\": \"per_lane_data_and_control_session\",\n";
    output << "  \"lane_local_recovery_count\": " << stats.laneLocalRecoveryCount.load() << ",\n";
    output << "  \"data_transfer_count\": " << stats.dataTransferCount.load() << ",\n";
    output << "  \"actual_transport\": \"v3\",\n";
    output << "  \"requested_mode\": \"persistent_tree\",\n";
    output << "  \"actual_mode\": \"" << (stats.persistentData ? "persistent_tree" : "unknown") << "\",\n";
    output << "  \"requested_file_scheduling\": \"" << (options.dynamicFileScheduling ? "dynamic" : "static") << "\",\n";
    output << "  \"actual_file_scheduling\": \"" << (stats.persistentData ? (stats.dynamicScheduling ? "dynamic" : "static") : "unknown") << "\",\n";
    output << "  \"fallback_reason\": \"" << jsonEscape(options.modeFallbackReason) << "\",\n";
    output << "  \"data_session_reuse_mode\": \"tree\",\n";
    output << "  \"data_connect_count\": " << stats.persistentDataConnectCount << ",\n";
    output << "  \"data_connect_count_source\": \"observed\",\n";
    output << "  \"link_set_requested_links\": " << options.linkSetLinks << ",\n";
    if (stats.persistentData) {
        output << "  \"link_set_accepted_links\": " << stats.persistentLaneIds.size() << ",\n";
        output << "  \"persistent_lane_ids\": [";
        for (std::size_t index = 0; index < stats.persistentLaneIds.size(); ++index) {
            if (index != 0) output << ", ";
            output << "\"" << jsonEscape(stats.persistentLaneIds[index]) << "\"";
        }
        output << "],\n";
        output << "  \"persistent_lane_range_completed\": [";
        for (std::size_t index = 0; index < stats.persistentLaneRangeCompletions.size(); ++index) {
            if (index != 0) output << ", ";
            output << stats.persistentLaneRangeCompletions[index];
        }
        output << "],\n";

        output << "  \"control_prepare_seconds\": " << stats.controlPrepareSeconds << ",\n";
        output << "  \"control_prepare_scope\": \"sum_channel_v2_prepare_excludes_preexisting_download_login_and_data_connect\",\n";
        output << "  \"data_connect_seconds\": " << stats.dataConnectSeconds << ",\n";
        output << "  \"queue_wait_seconds\": " << stats.queueWaitSeconds << ",\n";
        output << "  \"queue_wait_scope\": \"" << (std::string(direction) == "upload" ? "sum_local_sender_ring_admission_to_claim" : "sum_remote_sender_ring_admission_to_claim") << "\",\n";
        output << "  \"queue_high_watermark\": " << stats.queueHighWatermark << ",\n";
        const auto dispatchWindow = options.controlPipelineDepth != 0
            ? options.controlPipelineDepth : options.dataPendingWindow;
        output << "  \"queue_capacity\": " << (stats.dynamicScheduling ? stats.persistentDataConnectCount*dispatchWindow*2 : 0) << ",\n";
        output << "  \"dispatch_window\": " << (stats.persistentData ? stats.pendingWindow : 0) << ",\n";
        output << "  \"scan_seconds\": ";
        if (stats.scanSeconds) output << *stats.scanSeconds; else output << "null";
        output << ",\n";
        if (options.phaseTiming) {
            output << "  \"phase_timing_schema\": \"persistent_file_timing_v1\",\n";
            output << "  \"phase_timing_units\": \"seconds\",\n";
            output << "  \"phase_timing_scope\": \"local_monotonic_file_intervals_sums_overlap_not_directory_wall\",\n";
            output << "  \"file_timings\": ";
            appendPersistentFileTimings(output,stats.fileTimings);
            output << ",\n";
        }
        output << "  \"transfer_complete_wait_seconds\": " << stats.completeWaitSeconds << ",\n";
        output << "  \"transfer_complete_wait_scope\": \"" << (std::string(direction) == "upload" ? "file_result_plus_control_final" : "control_final_only") << "\",\n";
        output << "  \"data_pending_window\": " << stats.pendingWindow << ",\n";
        output << "  \"data_pending_high_watermark\": " << stats.pendingHighWatermark << ",\n";
    }
    output << "  \"wire_bytes_scope\": \"" << (stats.persistentData ? "application_frames_both_directions" : "legacy_payload_accounting") << "\",\n";
    output << "  \"planner_preset\": \"" << jsonEscape(options.plannerPreset) << "\",\n";
    output << "  \"checksum_algorithm\": \""
           << checksum::checksumAlgorithmName(options.checksumAlgorithm) << "\",\n";
    output << "  \"checksum_backend\": \"" << checksum::checksumBackendName(options.checksumBackend)
           << "\",\n";
    output << "  \"resume\": " << (options.resume ? "true" : "false") << ",\n";
    output << "  \"elapsed_seconds\": " << elapsed << ",\n";
    output << "  \"throughput_gbps\": " << throughputGbps << ",\n";
    if (stats.persistentData || options.controlPipelineDepth != 0) {
        output << "  \"control_pipeline_depth\": " << options.controlPipelineDepth << ",\n";
        output << "  \"control_pipeline_actual_mode\": \""
               << (stats.persistentData ? "shared_persistent_dispatch" : "unselected")
               << "\",\n";
        output << "  \"control_pipeline_slot_count\": " << stats.pipelineSlotCount.load() << ",\n";
        output << "  \"control_pipeline_pending_high_watermark\": "
               << stats.pipelinePendingHighWatermark.load() << ",\n";
    }
    if (options.phaseTiming && !stats.persistentData) {
        output << "  \"phase_timing_schema\": \"persistent_file_timing_v1\",\n";
        output << "  \"phase_timing_units\": \"seconds\",\n";
        output << "  \"file_timings\": [],\n";
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
                                       TreeRunStats* stats,
                                       ControlClient* connected = nullptr) {
    if (!options.resume) {
        return common::Status::ok();
    }
    ControlClient ownedControl;
    ControlClient* control = connected != nullptr ? connected : &ownedControl;
    if (connected == nullptr) {
        const common::Status readyStatus = ensureControlReadyWithStats(control, options, stats);
        if (!readyStatus.isOk()) return readyStatus;
    }
    for (std::size_t index = 0; index < manifest->files.size(); ++index) {
        const auto& record = manifest->files[index];
        const std::string remotePath = joinRemotePath(options.sourceDir, record.relativePath);
        auto remoteSize = control->size(remotePath);
        if (!remoteSize.isOk()) {
            if (remoteSize.status().code() == common::StatusCode::TransportError) return remoteSize.status();
            const std::string message =
                changedMissingMessage("remote file changed", record, remoteSize.status().message());
            return markManifestChanged(manifest, manifestPath, index, message);
        }
        auto remoteMtime = control->mdtm(remotePath);
        if (!remoteMtime.isOk()) {
            if (remoteMtime.status().code() == common::StatusCode::TransportError) return remoteMtime.status();
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

















struct TreeSchedulerSummaryCounts {
    std::uint64_t rawWorkItems = 0;
    std::uint64_t retriedWorkItems = 0;
};

common::Status writeGlobalSchedulerSummary(core::scheduler::GlobalScheduler* scheduler,
                                           const core::tree::TreeManifest& manifest,
                                           const TreeRunStats& stats,
                                           const TreeSchedulerSummaryCounts& runtime,
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





bool persistentEligible(const config::TreeTransferOptions& options) {
    return options.reuseDataSession &&
        options.fileParallelism >= 1 && options.fileParallelism <= kPersistentTreeMaxChannels &&
        (options.schedulerMode == config::TreeSchedulerMode::Off || options.schedulerMode == config::TreeSchedulerMode::Global) &&
        (options.controlPipelineDepth == 0 || options.controlPipelineDepth == 1 ||
         options.controlPipelineDepth == 2 || options.controlPipelineDepth == 4);
}

common::Status tryPersistentDirectory(const config::TreeTransferOptions& options,
    core::tree::TreeManifest* manifest, const std::string& manifestPath, TreeRunStats* stats,
    bool upload, bool* used, ControlClient* connected = nullptr, std::string* attemptedBatch = nullptr,
    RecoveryBudget* recoveryBudget = nullptr) {
    *used = false;
    const auto persistentStarted = std::chrono::steady_clock::now();
    if (!persistentEligible(options)) {
        return common::Status::invalidArgument(
            "V3 persistent tree configuration is unsupported");
    }
    if (options.resume && !options.dynamicFileScheduling) {
        return common::Status::invalidArgument(
            "V3 resume requires dynamic file scheduling");
    }

    std::unordered_map<std::string, std::size_t> priorDownloadRecords;
    if (!upload && options.resume) {
        for (std::size_t index = 0; index < manifest->files.size(); ++index) {
            if (!priorDownloadRecords.emplace(manifest->files[index].relativePath, index).second) {
                return common::Status::invalidArgument(
                    "tree download manifest contains duplicate paths");
            }
        }
    }
    if (options.linkSetLinks > kPersistentTreeMaxChannels) {
        return common::Status::invalidArgument(
            "persistent multi-link requests above the safe 8-channel limit are unsupported");
    }
    const std::uint32_t channelCount = std::max(options.fileParallelism, options.linkSetLinks);
    std::vector<std::string> laneLinkIds;
    laneLinkIds.reserve(channelCount);
    for (std::uint32_t index = 0; index < channelCount; ++index) {
        laneLinkIds.push_back("link" + std::to_string(index % options.linkSetLinks));
    }
    config::TreeTransferOptions persistentOptions = options;
    // A persistent directory channel carries one file stream at a time. The
    // user-facing connection count remains a per-file control setting; V3 uses
    // fileParallelism for its bounded data channels. When the caller asks for
    // control pipeline depth, it is the shared persistent dispatch window:
    // no second pipeline state machine is allowed to prepare work independently.
    persistentOptions.connections = 1;
    const std::uint32_t dispatchWindow = options.controlPipelineDepth != 0
        ? options.controlPipelineDepth : options.dataPendingWindow;
    persistentOptions.dataPendingWindow = dispatchWindow;
    const std::string batchId = options.dynamicFileScheduling ? generateTransferId() : "";
    if (attemptedBatch) *attemptedBatch = batchId;
    std::unique_ptr<DynamicFileQueue> ready;
    std::unique_ptr<core::scheduler::GlobalScheduler> globalScheduler;
    std::vector<PersistentFileIdentity> files;
    if (upload) {
        if (manifest->files.size() > UINT32_MAX)
            return common::Status::invalidArgument("too many files for persistent directory");
        files.reserve(manifest->files.size());
        for (std::size_t index = 0; index < manifest->files.size(); ++index) {
            const auto& file = manifest->files[index];
            if (options.resume && file.status == core::tree::TreeFileStatus::Completed) continue;
            const auto id = static_cast<std::uint32_t>(index + 1);
            files.push_back({id, id, file.size, file.relativePath, file.transferId,
                             options.chunkSize, file.mtimeUnixSeconds, options.checksumAlgorithm, {}});
        }
    }

    for (auto& file : files) file.attempt = options.reconnectAttempt;
    const std::uint32_t logicalFileCount = static_cast<std::uint32_t>(files.size());
    auto expandedFiles = upload && options.dynamicFileScheduling ? expandPersistentFileRanges(files, channelCount)
                                : common::Result<std::vector<PersistentFileIdentity>>(files);
    if (!expandedFiles.isOk()) return expandedFiles.status();
    if (upload) files = std::move(expandedFiles.value());
    if (upload && !options.dynamicFileScheduling &&
        std::any_of(files.begin(), files.end(), [](const auto& file) { return file.rangeId != 0; }))
        return common::Status::invalidArgument("persistent large-file ranges require dynamic scheduling");
    if (options.dynamicFileScheduling && options.schedulerMode == config::TreeSchedulerMode::Global) {
        auto paths = schedulerMetricsPaths(options, manifestPath, upload ? "upload" : "download");
        if (!paths.isOk()) return paths.status();
        core::scheduler::SchedulerConfig schedulerConfig;
        schedulerConfig.taskId = std::string("persistent-") + (upload ? "upload-" : "download-") +
            std::to_string(manifest->updatedAtUnixNanos) + "-" + generateTransferId();
        schedulerConfig.linkId = options.linkSetLinks > 1 ? "link0" : options.schedulerLinkId;
        schedulerConfig.linkSetLinks = options.linkSetLinks;
        schedulerConfig.remoteHost = options.host;
        schedulerConfig.capacityGbps = options.schedulerCapacityGbps;
        schedulerConfig.policy = options.schedulerPolicy;
        schedulerConfig.initialConnections = options.schedulerPolicy == core::scheduler::SchedulerPolicy::Adaptive ? 1 : channelCount;
        schedulerConfig.maxConnections = channelCount;
        schedulerConfig.workItemMinBytes = options.schedulerWorkItemMinBytes;
        schedulerConfig.workItemMaxBytes = options.schedulerWorkItemMaxBytes;
        schedulerConfig.defaultRttMs = options.schedulerDefaultRttMs;
        schedulerConfig.minCompressGbps = options.schedulerMinCompressGbps;
        schedulerConfig.enableCompression = options.compressionMode == config::CompressionMode::Auto;
        schedulerConfig.metricsPaths = paths.value();
        auto created = core::scheduler::GlobalScheduler::create(std::move(schedulerConfig));
        if (!created.isOk()) return created.status();
        globalScheduler = std::make_unique<core::scheduler::GlobalScheduler>(std::move(created.value()));
    }
    if (upload && options.dynamicFileScheduling) {
        std::vector<WorkItem> boundWork;
        if (globalScheduler) {
            auto plans = globalScheduler->planManifest(*manifest, options.sourceDir,
                core::scheduler::SchedulerDirection::Upload, true);
            if (!plans.isOk()) return plans.status();
            auto bound = bindScheduledPlans(plans.value(), files);
            if (!bound.isOk()) return bound.status();
            boundWork = std::move(bound.value());
        } else {
            boundWork.reserve(files.size());
            for (const auto& file : files) boundWork.push_back(WorkItem{file, file.transferId});
        }
        ready = std::make_unique<DynamicFileQueue>(std::move(boundWork), channelCount, dispatchWindow,
            channelCount * dispatchWindow * 2, options.schedulerMode == config::TreeSchedulerMode::Global,
            false, laneLinkIds, options.linkSetLinks > 1);
        if (globalScheduler) {
            auto attached = ready->attachScheduler(globalScheduler.get());
            if (!attached.isOk()) return attached;
        }
    }
    if (!upload && options.dynamicFileScheduling && globalScheduler) {
        ready = std::make_unique<DynamicFileQueue>(
            std::vector<WorkItem>{}, channelCount, dispatchWindow,
            channelCount * dispatchWindow * 2, true, false, laneLinkIds, options.linkSetLinks > 1);
        auto attached = ready->attachScheduler(globalScheduler.get());
        if (!attached.isOk()) return attached;
    }
    if (upload && !options.dynamicFileScheduling) {
        std::vector<WorkItem> staticWork;
        staticWork.reserve(files.size());
        for (const auto& file : files) {
            staticWork.push_back(WorkItem{file, file.transferId});
        }
        const auto capacity = std::min<std::size_t>(
            256, std::max<std::size_t>(1, channelCount * dispatchWindow * 2));
        ready = std::make_unique<DynamicFileQueue>(
            std::move(staticWork), channelCount, dispatchWindow, capacity, true, true,
            laneLinkIds, options.linkSetLinks > 1);
    }
    if (!upload && !options.dynamicFileScheduling) {
        // Incoming server sessions are accepted independently of the sender's
        // channel index, so preserve shared leasing without sender-side affinity.
        const auto capacity = std::min<std::size_t>(
            256, std::max<std::size_t>(1, channelCount * dispatchWindow * 2));
        ready = std::make_unique<DynamicFileQueue>(
            std::vector<WorkItem>{}, channelCount, dispatchWindow, capacity, true, false,
            laneLinkIds, options.linkSetLinks > 1);
    }

    if (ready) {
        auto epoch = ready->setConnectionEpoch({static_cast<std::uint64_t>(options.reconnectAttempt) + 1});
        if (!epoch.isOk()) return epoch;
    }

    struct Channel {
        std::unique_ptr<ControlClient> ownedControl;
        ControlClient* control = nullptr;
        FramedDataSocket data;
        std::mutex dataMutex;
        std::vector<PersistentFileIdentity> files;
        PersistentTreeStats observed;
        std::uint32_t totalFiles = kUnknownPersistentFileCount;
        std::uint32_t expectedFileCount = kUnknownPersistentFileCount;
        std::uint32_t dataConnectCount = 0, localRecoveryAttempts = 0;
        double controlWaitSeconds = 0, prepareSeconds = 0, reconnectControlPrepareSeconds = 0;
        double connectSeconds = 0, remoteQueueWait = 0, remoteScan = 0;
        bool dataConnected = false;
        std::string linkId;
        std::size_t remoteQueueHigh = 0;
        common::Status status = common::Status::ok();
        bool capable = false;
    };
    std::vector<std::unique_ptr<Channel>> channels;
    channels.reserve(channelCount);
    for (std::uint32_t index = 0; index < channelCount; ++index) {
        auto channel = std::make_unique<Channel>();
        channel->linkId = laneLinkIds[index];
        if (index == 0 && connected != nullptr) {
            channel->control = connected;
        } else {
            channel->ownedControl = std::make_unique<ControlClient>();
            channel->control = channel->ownedControl.get();
        }
        channels.push_back(std::move(channel));
    }

    auto cancelAll = [&]() {
        if (ready) ready->cancel(common::Status::runtimeError("directory channels cancelled"));
        for (auto& channel : channels) {
            if (channel->control != nullptr) channel->control->cancel();
            std::lock_guard<std::mutex> lock(channel->dataMutex);
            if (channel->data.valid()) (void)::shutdown(channel->data.fd(), SHUT_RDWR);
        }
    };
    std::mutex failureMutex;
    common::Status firstFailure = common::Status::ok();
    common::Status firstFileFailure = common::Status::ok();
    auto recordFileFailure = [&](const common::Status& failure) {
        std::lock_guard<std::mutex> lock(failureMutex);
        if (firstFileFailure.isOk()) firstFileFailure = failure;
    };
    auto recordFailure = [&](const common::Status& failure) {
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(failureMutex);
            if (firstFailure.isOk()) {
                firstFailure = failure;
                first = true;
            }
        }
        if (first) cancelAll();
    };

    std::jthread deadlineGuard([&](std::stop_token stop) {
        if (!options.transferDeadline) return;
        while (!stop.stop_requested()) {
            if (std::chrono::steady_clock::now() >= *options.transferDeadline) {
                recordFailure(common::Status::runtimeError("transfer total deadline exceeded"));
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::vector<std::thread> workers;
    workers.reserve(channelCount);
    for (std::uint32_t index = 0; index < channelCount; ++index) {
        workers.emplace_back([&, index]() {
            auto& channel = *channels[index];
            const auto preparing = std::chrono::steady_clock::now();
            if (!(index == 0 && connected != nullptr)) {
                channel.status = ensureControlReadyWithStats(channel.control, persistentOptions, stats);
                if (!channel.status.isOk()) {
                    recordFailure(channel.status);
                    return;
                }
            }
            std::uint32_t actualWindow = 1, actualChannels = 1;
            auto capability = channel.control->negotiatePersistent(
                dispatchWindow, channelCount, options.checksumAlgorithm, options.resume,
                options.dataTlsMode, &actualWindow, &actualChannels, options.dynamicFileScheduling,
                options.linkSetLinks, options.linkSetCapabilityBits);
            if (!capability.isOk()) {
                channel.status = capability.status();
                recordFailure(channel.status);
                return;
            }
            channel.prepareSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-preparing).count();
            channel.capable = capability.value();
            if (channel.capable && (actualWindow != dispatchWindow ||
                actualChannels != channelCount)) {
                channel.status = common::Status::runtimeError(
                    "persistent channel negotiation returned mismatched limits");
                recordFailure(channel.status);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (!firstFailure.isOk()) return firstFailure;
    if (std::any_of(channels.begin(), channels.end(),
                    [](const auto& channel) { return !channel->capable; })) {
        return common::Status::invalidArgument(
            "server does not support the required V3 persistent tree capability");
    }

    *used = true;
    stats->persistentData = true;
    stats->dynamicScheduling = options.dynamicFileScheduling;
    stats->pendingWindow = dispatchWindow;
    if (!upload) {
        std::error_code error;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(options.destDir, error)))
            return common::Status::invalidArgument("persistent local destination is a symlink");
        error.clear();
        std::filesystem::create_directories(options.destDir, error);
        if (error) return common::Status::runtimeError("cannot create persistent local destination");
        manifest->mode = core::tree::TreeTransferMode::Download;
        manifest->rootLogicalPath = options.sourceDir;
        manifest->checksumAlgorithm = options.checksumAlgorithm;
        manifest->createdAtUnixNanos = checkpoint::nowUnixNanos();
        auto saved = saveManifest(manifest, manifestPath);
        if (!saved.isOk()) return saved;
    }
    for (const auto& file : files) {
        if (options.dynamicFileScheduling) break;
        const auto channelIndex = file.rangeId != 0
            ? static_cast<std::uint32_t>((file.rangeId - 1) % channelCount)
            : (file.fileId - 1) % channelCount;
        channels[channelIndex]->files.push_back(file);
    }

    workers.clear();
    for (std::uint32_t index = 0; index < channelCount; ++index) {
        workers.emplace_back([&, index]() {
            auto& channel = *channels[index];
            const auto preparing = std::chrono::steady_clock::now();
            auto port = channel.control->epsv();
            if (!port.isOk()) {
                channel.status = port.status();
                recordFailure(channel.status);
                return;
            }
            const auto totalFiles = upload ? logicalFileCount : 0U;
            channel.status = channel.control->startDirectory(upload,
                upload ? options.destDir : options.sourceDir, index, channelCount, totalFiles,
                &channel.totalFiles, &channel.expectedFileCount, batchId);
            if (!channel.status.isOk()) {
                recordFailure(channel.status);
                return;
            }
            channel.prepareSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now()-preparing).count();
            const auto connecting = std::chrono::steady_clock::now();
            auto data = connectFramedDataSocket(options.host.c_str(), port.value(), options.dataTlsMode, options.tls,
                                                  options.tlsServerName.empty() ? nullptr : options.tlsServerName.c_str(), options.transferDeadline);
            channel.connectSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-connecting).count();
            if (!data.isOk()) {
                channel.status = data.status();
                recordFailure(channel.status);
                return;
            }
            std::lock_guard<std::mutex> lock(channel.dataMutex);
            channel.data = std::move(data.value());
            channel.dataConnected = true;
            channel.dataConnectCount = 1;
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& channel : channels) {
        stats->controlPrepareSeconds += channel->prepareSeconds;
        stats->dataConnectSeconds += channel->connectSeconds;
        stats->persistentDataConnectCount += channel->dataConnectCount;
    }
    if (!firstFailure.isOk()) return firstFailure;

    std::uint32_t expectedTotalFiles = upload ? logicalFileCount :
        kUnknownPersistentFileCount;
    for (std::uint32_t index = 0; index < channelCount; ++index) {
        const auto& channel = *channels[index];
        if (channel.totalFiles != kUnknownPersistentFileCount) {
            if (expectedTotalFiles == kUnknownPersistentFileCount)
                expectedTotalFiles = channel.totalFiles;
            else if (channel.totalFiles != expectedTotalFiles) {
                recordFailure(common::Status::invalidArgument(
                    "persistent channels disagree on total file count"));
                break;
            }
        }
        if (!options.dynamicFileScheduling && channel.expectedFileCount != kUnknownPersistentFileCount) {
            const auto expected = expectedTotalFiles == kUnknownPersistentFileCount ? 0U :
                (index >= expectedTotalFiles ? 0U :
                 1U + (expectedTotalFiles - 1U - index) / channelCount);
            if (channel.expectedFileCount != expected) {
                recordFailure(common::Status::invalidArgument(
                    "persistent channel shard count does not match file IDs"));
                break;
            }
        }
    }
    if (!firstFailure.isOk()) return firstFailure;

    std::mutex manifestMutex;
    std::unordered_map<std::uint32_t, std::size_t> downloadRecords;
    struct ActiveIdentity {
        PersistentFileIdentity base;
        std::uint64_t rangeCount = 0;
        std::unordered_set<std::uint64_t> started;
        std::unordered_set<std::uint64_t> completed;
    };
    std::unordered_map<std::uint32_t, ActiveIdentity> activeIdentities;
    std::unordered_set<std::string> downloadPaths;
    auto callback = [&](const PersistentFileIdentity& id, const common::Status& fileStatus,
                        bool complete) {
        std::lock_guard<std::mutex> lock(manifestMutex);
        {
            std::lock_guard failureLock(failureMutex);
            if (!firstFailure.isOk()) return firstFailure;
        }
        if (complete && !fileStatus.isOk() &&
            fileStatus.code() != common::StatusCode::TransportError) {
            if (!options.dynamicFileScheduling) recordFileFailure(fileStatus);
        }
        if (complete && fileStatus.code() == common::StatusCode::TransportError) {
            // Dynamic workers reconcile only their own leases after both IO threads
            // stop. Static V2 keeps the historical whole-batch cancellation.
            if (!options.dynamicFileScheduling) recordFailure(fileStatus);
            return fileStatus;
        }
        auto base = id;
        base.rangeId = base.rangeOffset = base.rangeLength = base.rangeCount = base.attempt = 0;
        auto active = activeIdentities.find(id.fileId);
        bool logicalComplete = id.rangeId == 0;
        if (!complete) {
            if (!id.generation) return common::Status::invalidArgument("duplicate persistent file begin");
            if (active == activeIdentities.end()) {
                ActiveIdentity value{base, id.rangeCount, {}, {}};
                active = activeIdentities.emplace(id.fileId, std::move(value)).first;
            } else if (!samePersistentIdentity(active->second.base, base) ||
                       active->second.rangeCount != id.rangeCount) {
                return common::Status::invalidArgument("persistent file begin identity mismatch");
            }
            if (id.rangeId != 0) {
                const bool inserted = active->second.started.insert(id.rangeId).second;
                if (!inserted) {
                    // A local lane retry can replay a begin that the sender already
                    // emitted before the data socket reset. Keep the callback
                    // idempotent for retry attempts; the batch validates the same
                    // identity and range before accepting the replay.
                    if (id.attempt == 0)
                        return common::Status::invalidArgument("duplicate persistent range begin");
                    if (active->second.completed.contains(id.rangeId))
                        return common::Status::ok();
                }
            }
        } else {
            if (active == activeIdentities.end() || !samePersistentIdentity(active->second.base, base))
                return common::Status::invalidArgument("persistent completion identity mismatch");
            if (!fileStatus.isOk()) {
                activeIdentities.erase(active);
                logicalComplete = true;
            } else if (id.rangeId == 0) {
                logicalComplete = true;
                activeIdentities.erase(active);
            } else {
                if (!active->second.started.contains(id.rangeId) ||
                    !active->second.completed.insert(id.rangeId).second)
                    return common::Status::invalidArgument("persistent range completion identity mismatch");
                logicalComplete = active->second.completed.size() == active->second.rangeCount;
                if (logicalComplete) activeIdentities.erase(active);
            }
        }
        core::tree::TreeFileRecord* record = nullptr;
        if (upload) {
            if (id.fileId == 0 || id.fileId > manifest->files.size())
                return common::Status::invalidArgument("unexpected persistent file ID");
            record = &manifest->files[id.fileId - 1];
        } else if (!complete && (id.rangeId == 0 || active->second.started.size() == 1)) {
            if (id.fileId == 0 ||
                (expectedTotalFiles != kUnknownPersistentFileCount && id.fileId > expectedTotalFiles))
                return common::Status::invalidArgument("duplicate or out-of-range persistent file ID");
            const auto existing = downloadRecords.find(id.fileId);
            if (existing != downloadRecords.end()) {
                if (id.attempt == 0 || existing->second >= manifest->files.size())
                    return common::Status::invalidArgument("duplicate or out-of-range persistent file ID");
                // A local lane retry can begin the same file again on a new data
                // connection. Reuse its manifest record after identity validation below.
                record = &manifest->files[existing->second];
            } else {
                if (!downloadPaths.insert(id.relativePath).second)
                    return common::Status::invalidArgument("duplicate or out-of-range persistent file ID");
                const auto prior = priorDownloadRecords.find(id.relativePath);
                if (prior != priorDownloadRecords.end()) {
                    if (prior->second >= manifest->files.size())
                        return common::Status::invalidArgument("tree download manifest index is invalid");
                    record = &manifest->files[prior->second];
                    if (record->size != id.totalSize || record->mtimeUnixSeconds != id.mtimeUnixSeconds)
                        return common::Status::invalidArgument("persistent download source identity changed");
                    record->transferId = id.transferId;
                    downloadRecords.emplace(id.fileId, prior->second);
                } else {
                    downloadRecords.emplace(id.fileId, manifest->files.size());
                    manifest->files.push_back({id.relativePath, id.totalSize, id.mtimeUnixSeconds,
                        id.transferId, core::tree::TreeFileStatus::Pending, ""});
                    record = &manifest->files.back();
                }
            }
        } else {
            const auto found = downloadRecords.find(id.fileId);
            if (found == downloadRecords.end() || found->second >= manifest->files.size())
                return common::Status::invalidArgument("persistent completion has no matching file begin");
            record = &manifest->files[found->second];
        }
        if (record->relativePath != id.relativePath || record->transferId != id.transferId ||
            record->size != id.totalSize || record->mtimeUnixSeconds != id.mtimeUnixSeconds)
            return common::Status::invalidArgument("persistent tree manifest identity mismatch");
        if (logicalComplete || !complete) {
            record->status = complete ? (fileStatus.isOk() ? core::tree::TreeFileStatus::Completed :
                core::tree::TreeFileStatus::Failed) : core::tree::TreeFileStatus::Transferring;
            record->error = fileStatus.isOk() ? "" : fileStatus.message();
        }
        if (complete && logicalComplete && fileStatus.isOk()) stats->completedThisRun.fetch_add(1);
        if (!complete && id.rangeId == 0) stats->dataTransferCount.fetch_add(1);
        return saveManifest(manifest, manifestPath);
    };

    config::FileTransferOptions transferOptions;
    transferOptions.resume=options.resume;transferOptions.bufferSize=options.bufferSize;
    transferOptions.checksumBackend=options.checksumBackend;
    transferOptions.hotPathCompression=options.hotPathCompression;
    if (upload && options.compressionMode == config::CompressionMode::Auto) {
        transferOptions.hotPathCompression.enabled = true;
        transferOptions.hotPathCompression.candidate = true;
        if (transferOptions.hotPathCompression.maxPayloadBytes == 0)
            transferOptions.hotPathCompression.maxPayloadBytes = options.bufferSize;
    }
    auto transactions = makePersistentTransferContext();
    workers.clear();
    for (std::uint32_t index = 0; index < channelCount; ++index) {
        workers.emplace_back([&, index]() {
            auto& channel = *channels[index];
            channel.observed.phaseTiming = options.phaseTiming;
            std::uint32_t laneAttempt = 0;
            auto recordTransferOutcome = [&](const common::Status& outcome) {
                if (outcome.isOk()) return;
                {
                    std::lock_guard<std::mutex> lock(failureMutex);
                    if (!options.dynamicFileScheduling && !firstFileFailure.isOk() &&
                        firstFileFailure.code() == outcome.code() &&
                        firstFileFailure.message() == outcome.message()) {
                        return;
                    }
                }
                recordFailure(outcome);
            };
            auto parseTerminalTelemetry = [&](const std::string& terminal,
                                               common::Status* controlStatus) {
                if (!options.dynamicFileScheduling || !controlStatus->isOk()) return;
                std::istringstream fields(terminal);
                std::string field;
                bool sawQueueHigh = false, sawQueueWait = false, sawScan = false;
                while (fields >> field) {
                    const auto separator = field.find('=');
                    if (separator == std::string::npos) continue;
                    const auto name = field.substr(0,separator);
                    const auto value = field.substr(separator+1);
                    if (name == "queue_high_watermark") {
                        std::size_t parsed = 0;
                        auto result = std::from_chars(value.data(),value.data()+value.size(),parsed);
                        if (result.ec != std::errc() || result.ptr != value.data()+value.size() || parsed > 256) {
                            *controlStatus = common::Status::runtimeError("invalid remote queue telemetry"); break;
                        }
                        channel.remoteQueueHigh = parsed;
                        sawQueueHigh = true;
                    } else if (name == "queue_wait_seconds" || name == "scan_seconds") {
                        double parsed = 0;
                        auto result = std::from_chars(value.data(),value.data()+value.size(),parsed);
                        if (result.ec != std::errc() || result.ptr != value.data()+value.size() || !std::isfinite(parsed) || parsed < 0) {
                            *controlStatus = common::Status::runtimeError("invalid remote duration telemetry"); break;
                        }
                        if (name == "queue_wait_seconds") { channel.remoteQueueWait = parsed; sawQueueWait = true; }
                        else { channel.remoteScan = parsed; sawScan = true; }
                    }
                }
                if (controlStatus->isOk() && !(sawQueueHigh && sawQueueWait && sawScan))
                    *controlStatus = common::Status::runtimeError("dynamic server omitted directory telemetry");
            };
            auto reconnectLane = [&](bool reconnectControl) -> common::Status {
                if (reconnectControl) {
                    const auto controlPreparing = std::chrono::steady_clock::now();
                    channel.control->reset();
                    stats->laneControlReconnectCount.fetch_add(1);
                    const auto connectedStatus = ensureControlReadyWithStats(
                        channel.control, persistentOptions, stats);
                    if (!connectedStatus.isOk()) return connectedStatus;
                    std::uint32_t actualWindow = 1, actualChannels = 1;
                    auto capability = channel.control->negotiatePersistent(
                        dispatchWindow, channelCount, options.checksumAlgorithm, options.resume,
                        options.dataTlsMode, &actualWindow, &actualChannels,
                        options.dynamicFileScheduling, options.linkSetLinks,
                        options.linkSetCapabilityBits);
                    if (!capability.isOk()) return capability.status();
                    if (!capability.value() || actualWindow != dispatchWindow ||
                        actualChannels != channelCount) {
                        return common::Status::runtimeError(
                            "persistent lane control reconnect negotiation failed");
                    }
                    channel.reconnectControlPrepareSeconds += std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - controlPreparing).count();
                }
                auto port = channel.control->epsv();
                if (!port.isOk()) return port.status();
                const auto totalFiles = upload ? logicalFileCount : 0U;
                auto started = channel.control->startDirectory(upload,
                    upload ? options.destDir : options.sourceDir, index, channelCount, totalFiles,
                    &channel.totalFiles, &channel.expectedFileCount, batchId);
                if (!started.isOk()) return started;
                const auto connecting = std::chrono::steady_clock::now();
                auto data = connectFramedDataSocket(options.host.c_str(), port.value(), options.dataTlsMode,
                    options.tls, options.tlsServerName.empty() ? nullptr : options.tlsServerName.c_str(),
                    options.transferDeadline);
                channel.connectSeconds += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - connecting).count();
                if (!data.isOk()) return data.status();
                {
                    std::lock_guard<std::mutex> lock(channel.dataMutex);
                    channel.data = std::move(data.value());
                }
                channel.dataConnected = true;
                ++channel.dataConnectCount;
                return common::Status::ok();
            };
            for (;;) {
                config::FileTransferOptions attemptOptions = transferOptions;
                attemptOptions.resume = options.resume || laneAttempt != 0;
                attemptOptions.reconnectAttempt = laneAttempt;
                common::Status transferStatus = upload
                    ? sendPersistentTree(&channel.data, options.sourceDir, channel.files,
                        &channel.observed, callback, dispatchWindow, index, channelCount, ready.get(),
                        &attemptOptions, !options.dynamicFileScheduling, options.reconnectAttempts)
                    : receivePersistentTree(&channel.data, options.destDir, true, options.sourceDir,
                        &channel.observed, callback, dispatchWindow, index, channelCount,
                        channel.expectedFileCount, options.dynamicFileScheduling,
                        attemptOptions.resume, transactions, ready.get(), options.reconnectAttempts);
                {
                    std::lock_guard<std::mutex> lock(channel.dataMutex);
                    channel.data = FramedDataSocket{};
                }
                const auto waiting = std::chrono::steady_clock::now();
                std::string terminal;
                auto controlStatus = channel.control->waitTransferComplete({}, &terminal);
                channel.controlWaitSeconds += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - waiting).count();
                const bool transferCanResume = transferStatus.isOk() ||
                    transferStatus.code() == common::StatusCode::TransportError;
                const bool transportDisconnected =
                    transferStatus.code() == common::StatusCode::TransportError ||
                    controlStatus.code() == common::StatusCode::TransportError;
                const bool committedOutputIntegrityFailure = !controlStatus.isOk() &&
                    controlStatus.message().find("committed output ") != std::string::npos;
                const common::Status transferOrControlStatus = committedOutputIntegrityFailure
                    ? controlStatus : (transferStatus.isOk() ? controlStatus : transferStatus);
                if (recoveryBudget && recoveryBudget->exhausted() &&
                    recoveryBudget->used() != 0 &&
                    transferStatus.message() == "work retry limit exceeded") {
                    channel.status = reconnectBudgetExhausted(transferOrControlStatus);
                    recordTransferOutcome(channel.status);
                    break;
                }
                // Reattaching a lane is safe only when the data operation itself
                // failed. If data completed but its final control reply was lost,
                // use the outer retry with a fresh batch identity.
                const bool retryEligible = options.dynamicFileScheduling && transferCanResume &&
                    transportDisconnected && !committedOutputIntegrityFailure &&
                    transferStatus.code() == common::StatusCode::TransportError;
                const bool canRetryWithoutBudget =
                    !recoveryBudget && laneAttempt < options.reconnectAttempts;
                if (retryEligible && (recoveryBudget || canRetryWithoutBudget)) {
                    if (recoveryBudget && !recoveryBudget->tryConsume()) {
                        channel.status = reconnectBudgetExhausted(transferOrControlStatus);
                        recordTransferOutcome(channel.status);
                        break;
                    }
                    ++laneAttempt;
                    channel.localRecoveryAttempts = laneAttempt;
                    stats->laneLocalRecoveryCount.fetch_add(1);
                    const bool reconnectControl =
                        controlStatus.code() == common::StatusCode::TransportError;
                    auto reconnected = reconnectLane(reconnectControl);
                    if (!reconnected.isOk()) {
                        channel.status = reconnected;
                        recordTransferOutcome(channel.status);
                        break;
                    }
                    continue;
                }
                parseTerminalTelemetry(terminal, &controlStatus);
                channel.status = committedOutputIntegrityFailure ? controlStatus :
                    (transferStatus.isOk() ? controlStatus : transferStatus);
                if (channel.status.isOk() && !controlStatus.isOk()) channel.status = controlStatus;
                if (!channel.status.isOk()) recordTransferOutcome(channel.status);
                break;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& channel : channels)
        stats->controlPrepareSeconds += channel->reconnectControlPrepareSeconds;

    stats->persistentLaneIds.clear();
    for (std::uint32_t index = 0; index < options.linkSetLinks; ++index) {
        stats->persistentLaneIds.push_back("link" + std::to_string(index));
    }
    stats->persistentLaneRangeCompletions.assign(options.linkSetLinks, 0);
    std::uint64_t transferredBytes = 0, wireBytes = 0;
    // Recompute after workers finish so lane-local reconnects are included.
    stats->persistentDataConnectCount = 0;
    double completeWaitSeconds = 0;
    std::uint32_t pendingHighWatermark = 0;
    for (std::size_t index = 0; index < channels.size(); ++index) {
        const auto& channel = channels[index];
        stats->persistentDataConnectCount += channel->dataConnectCount;
        stats->resumeSkippedBytes += channel->observed.skippedBytes;
        stats->queueWaitSeconds += upload ? channel->observed.queueWaitSeconds : channel->remoteQueueWait;
        if (!upload && options.dynamicFileScheduling)
            stats->scanSeconds = std::max(stats->scanSeconds.value_or(0),channel->remoteScan);
        stats->queueHighWatermark = std::max(stats->queueHighWatermark,channel->remoteQueueHigh);
        stats->fileTimings.insert(stats->fileTimings.end(),channel->observed.fileTimings.begin(),channel->observed.fileTimings.end());
        const auto laneIndex = index % options.linkSetLinks;
        if (laneIndex < stats->persistentLaneRangeCompletions.size()) {
            stats->persistentLaneRangeCompletions[laneIndex] += channel->observed.rangeCompletions;
        }
        transferredBytes += channel->observed.payloadBytes;
        wireBytes += channel->observed.wireBytes;
        stats->compressionAttempts.fetch_add(channel->observed.runtime.compressionAttempts);
        stats->compressedFrames.fetch_add(channel->observed.runtime.compressedFrames);
        stats->rawFallbackFrames.fetch_add(channel->observed.runtime.rawFallbackFrames);
        stats->compressionFailures.fetch_add(channel->observed.runtime.compressionFailures);
        stats->decompressionFailures.fetch_add(channel->observed.runtime.decompressionFailures);
        stats->compressedLogicalBytes.fetch_add(channel->observed.runtime.compressedLogicalBytes);
        stats->compressedWireBytes.fetch_add(channel->observed.runtime.compressedWireBytes);
        completeWaitSeconds += channel->observed.completeWaitSeconds + channel->controlWaitSeconds;
        pendingHighWatermark = std::max(pendingHighWatermark,
            channel->observed.pendingHighWatermark);
    }
    stats->transferredBytes.fetch_add(transferredBytes);
    if (options.reconnectAttempt != 0) stats->recoveryPayloadBytes += transferredBytes;
    stats->wireBytes.fetch_add(wireBytes);
    stats->completeWaitSeconds += completeWaitSeconds;
    stats->pendingHighWatermark = pendingHighWatermark;
    if (ready) stats->queueHighWatermark = ready->highWatermark();

    common::Status result;
    {
        std::lock_guard<std::mutex> lock(failureMutex);
        result = firstFailure.isOk() ? firstFileFailure : firstFailure;
    }
    if (!upload) {
        if (result.isOk() && expectedTotalFiles != kUnknownPersistentFileCount &&
            manifest->files.size() != expectedTotalFiles)
            result = common::Status::invalidArgument("persistent download file count mismatch");
        std::sort(manifest->files.begin(), manifest->files.end(),
            [](const auto& left, const auto& right) { return left.relativePath < right.relativePath; });
    }
    if (!result.isOk()) {
        std::lock_guard<std::mutex> lock(manifestMutex);
        for (auto& file : manifest->files) {
            if (file.status == core::tree::TreeFileStatus::Transferring) {
                file.status = core::tree::TreeFileStatus::Failed;
                file.error = result.message();
            }
        }
    }
    const auto saved = saveManifest(manifest, manifestPath);
    if (!saved.isOk()) return saved;
    if (globalScheduler) {
        const auto summaryStatus = writeGlobalSchedulerSummary(
            globalScheduler.get(), *manifest, *stats, TreeSchedulerSummaryCounts{}, result, persistentStarted);
        if (!summaryStatus.isOk()) return summaryStatus;
    }
    return result;
}


common::Status tryRecoveringPersistentDirectory(const config::TreeTransferOptions& options,
    core::tree::TreeManifest* manifest, const std::string& manifestPath, TreeRunStats* stats,
    bool upload, bool* used, ControlClient* connected = nullptr) {
    if (options.reconnectAttempts == 0) {
        auto status = tryPersistentDirectory(options, manifest, manifestPath, stats, upload, used, connected);
        if (status.code() == common::StatusCode::TransportError ||
            status.message() == "work retry limit exceeded")
            return reconnectBudgetExhausted(status);
        if (status.isOk() && !*used)
            return common::Status::invalidArgument("V3 persistent tree executor was not selected");
        return status;
    }
    if (!persistentEligible(options) || !options.dynamicFileScheduling ||
        options.checksumAlgorithm == checksum::ChecksumAlgorithm::None)
        return common::Status::invalidArgument("automatic reconnect requires persistent dynamic scheduling with checksums");
    RecoveryBudget recoveryBudget(options.reconnectAttempts, &stats->recoveryBudgetUsed);
    config::TreeTransferOptions attemptOptions = options;
    std::string previousBatch;
    common::Status status;
    for (std::uint32_t attempt = 0;; ++attempt) {
        if (options.transferDeadline && std::chrono::steady_clock::now() >= *options.transferDeadline)
            return common::Status::runtimeError("transfer total deadline exceeded");
        stats->reconnectAttempts = attempt;
        stats->connectionEpoch = attempt + 1;
        attemptOptions.reconnectAttempt = attempt;
        if (attempt != 0) {
            if (connected) { connected->reset(); connected = nullptr; }
            stats->controlReconnectCount.fetch_add(options.fileParallelism);
            ControlClient reconciliation;
            status = ensureControlReadyWithStats(&reconciliation, attemptOptions, stats);
            if (status.isOk() && !previousBatch.empty()) {
                for (;;) {
                    auto drained = reconciliation.batchDrained(previousBatch);
                    if (!drained.isOk()) { status = drained.status(); break; }
                    if (drained.value()) break;
                    if (options.transferDeadline && std::chrono::steady_clock::now() >= *options.transferDeadline) {
                        status = common::Status::runtimeError("transfer total deadline exceeded waiting for old batch");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            attemptOptions.resume = true;
            if (status.isOk()) status = upload
                ? preflightUploadResume(attemptOptions, manifest, manifestPath)
                : preflightDownloadResume(attemptOptions, manifest, manifestPath, stats, &reconciliation);
            if (status.isOk() && upload) {
                // Replay every identity, including uncertain FILE_RESULT. The receiver
                // must verify committed output/checkpoint before returning no missing bytes.
                for (auto& file : manifest->files) {
                    file.status = core::tree::TreeFileStatus::Pending;
                    file.error.clear();
                }
                status = saveManifest(manifest, manifestPath);
            }
        }
        if (status.isOk())
            status = tryPersistentDirectory(attemptOptions, manifest, manifestPath, stats,
                                             upload, used, connected, &previousBatch,
                                             &recoveryBudget);
        if (status.isOk() || status.code() != common::StatusCode::TransportError) return status;
        if (!recoveryBudget.tryConsume())
            return reconnectBudgetExhausted(status);
        if (options.schedulerMode == config::TreeSchedulerMode::Global) {
            auto paths = schedulerMetricsPaths(options, manifestPath, upload ? "upload" : "download");
            if (!paths.isOk()) return paths.status();
            for (const auto& path : {paths.value().summaryCsv, paths.value().eventsJsonl, paths.value().samplesCsv}) {
                std::error_code error;
                if (!std::filesystem::exists(path, error)) {
                    if (error) return common::Status::runtimeError("cannot inspect scheduler attempt evidence");
                    continue;
                }
                std::filesystem::copy_file(path, path + ".attempt-" + std::to_string(attempt),
                    std::filesystem::copy_options::overwrite_existing, error);
                if (error) return common::Status::runtimeError("cannot preserve scheduler attempt evidence");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        status = common::Status::ok();
    }
    return status;
}

}  // namespace




common::Status runTreeUploadClient(const config::TreeTransferOptions& inputOptions) {
    const auto startedAt = std::chrono::steady_clock::now();
    auto options = inputOptions;
    if (options.reconnectAttempts > 0 && options.transferTimeoutMs == 0) options.transferTimeoutMs = 300000;
    if (options.transferTimeoutMs != 0)
        options.transferDeadline = startedAt + std::chrono::milliseconds(options.transferTimeoutMs);
    const std::string manifestPath = core::tree::treeManifestPathForUpload(options.sourceDir);
    core::tree::TreeManifest manifest;
    std::optional<double> scanSeconds;
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
        const auto scanStarted = std::chrono::steady_clock::now();
        auto files = persistentEligible(options) ? scanPersistentTree(options.sourceDir)
                                                 : core::tree::scanLocalTree(options.sourceDir);
        scanSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-scanStarted).count();
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

    TreeRunStats persistentStats;
    persistentStats.scanSeconds = scanSeconds;
    if (options.resume) {
        const auto preflight = preflightUploadResume(options, &manifest, manifestPath);
        if (!preflight.isOk())
            return emitTreeSummary("tree_upload_complete", "upload", preflight, options,
                                   manifest, persistentStats, startedAt);
    }
    bool usedPersistent = false;
    auto persistentStatus = tryRecoveringPersistentDirectory(options, &manifest, manifestPath,
        &persistentStats, true, &usedPersistent);
    return emitTreeSummary("tree_upload_complete", "upload", persistentStatus, options,
                           manifest, persistentStats, startedAt);
}

common::Status runTreeDownloadClient(const config::TreeTransferOptions& inputOptions) {
    const auto startedAt = std::chrono::steady_clock::now();
    auto options = inputOptions;
    if (options.reconnectAttempts > 0 && options.transferTimeoutMs == 0) options.transferTimeoutMs = 300000;
    if (options.transferTimeoutMs != 0)
        options.transferDeadline = startedAt + std::chrono::milliseconds(options.transferTimeoutMs);
    const std::string manifestPath = core::tree::treeManifestPathForDownload(options.destDir);
    core::tree::TreeManifest manifest;
    TreeRunStats stats;
    ControlClient control;
    config::TreeTransferOptions controlOptions = options;
    if (options.resume && persistentEligible(options)) controlOptions.connections = 1;
    const bool needInitialControl = !persistentEligible(options) || options.resume;
    if (needInitialControl) {
        const common::Status readyStatus = ensureControlReadyWithStats(&control, controlOptions, &stats);
        if (!readyStatus.isOk()) return readyStatus;
    }
    if (options.resume && persistentEligible(options)) {
        auto loaded = core::tree::loadTreeManifest(manifestPath);
        if (!loaded.isOk()) return loaded.status();
        manifest = loaded.value();
        if (manifest.mode != core::tree::TreeTransferMode::Download ||
            manifest.rootLogicalPath != options.sourceDir ||
            manifest.checksumAlgorithm != options.checksumAlgorithm) {
            return common::Status::invalidArgument("tree download manifest does not match request");
        }
        const common::Status preflight =
            preflightDownloadResume(controlOptions, &manifest, manifestPath, &stats, &control);
        if (!preflight.isOk()) {
            return emitTreeSummary("tree_download_complete", "download", preflight, options,
                                   manifest, stats, startedAt);
        }
    }

    bool usedPersistent = false;
    auto persistentStatus = tryRecoveringPersistentDirectory(options, &manifest, manifestPath,
        &stats, false, &usedPersistent, needInitialControl ? &control : nullptr);
    return emitTreeSummary("tree_download_complete", "download", persistentStatus, options,
                           manifest, stats, startedAt);
}

TreeAuthProbeResult runTreeAuthProbe(const config::TreeTransferOptions& options) {
    return runTreeAuthProbeInternal(options);
}

}  // namespace cpnetflux::core::io
