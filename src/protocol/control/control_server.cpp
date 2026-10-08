#include "cpnetflux/protocol/control/control_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/config/file_transfer_options.h"
#include "cpnetflux/core/io/file_download_sender.h"
#include "cpnetflux/core/io/file_transfer_server.h"
#include "cpnetflux/core/metrics/event_log.h"
#include "cpnetflux/core/tree/tree_scan.h"
#include "cpnetflux/core/io/socket_utils.h"
#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/protocol/control/control_command.h"
#include "cpnetflux/storage/posix_file.h"

namespace cpnetflux::protocol::control {
namespace {

constexpr std::uint16_t kPassiveScanLimit =
    static_cast<std::uint16_t>(kPassiveDataPortWindowSize);

using EventLoggerPtr = std::shared_ptr<core::metrics::EventLogger>;

struct PassiveListener {
    core::io::UniqueFd fd;
    std::uint16_t port = 0;
};

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

common::Status sendAllRaw(int fd, const std::string& text) {
    std::size_t completed = 0;
    while (completed < text.size()) {
        const ssize_t sent =
            ::send(fd, text.data() + completed, text.size() - completed, MSG_NOSIGNAL);
        if (sent > 0) {
            completed += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0) {
            return systemStatus("send control", errno);
        }
        return common::Status::runtimeError("control send returned zero bytes");
    }
    return common::Status::ok();
}

common::Status sendAll(core::io::TlsConnection* connection, const std::string& text) {
    return connection->writeAll(text.data(), text.size());
}

common::Status sendLine(core::io::TlsConnection* connection, const std::string& line) {
    return sendAll(connection, line + "\r\n");
}

common::Status sendResponse(core::io::TlsConnection* connection, const ControlResponse& response) {
    for (const std::string& line : response.lines) {
        const common::Status status = sendLine(connection, line);
        if (!status.isOk()) {
            return status;
        }
    }
    return common::Status::ok();
}

bool isProtectedCommand(ControlCommandType type) noexcept {
    switch (type) {
        case ControlCommandType::Type:
        case ControlCommandType::Epsv:
        case ControlCommandType::Pasv:
        case ControlCommandType::Opts:
        case ControlCommandType::Rest:
        case ControlCommandType::Stor:
        case ControlCommandType::Retr:
        case ControlCommandType::Size:
        case ControlCommandType::Mdtm:
        case ControlCommandType::Cwd:
        case ControlCommandType::Cdup:
        case ControlCommandType::List:
        case ControlCommandType::Nlst:
            return true;
        default:
            return false;
    }
}

double elapsedSeconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void emitControlEvent(const EventLoggerPtr& logger, std::string event, std::string direction,
                      std::string path, std::string transferId, const common::Status& status,
                      std::uint64_t bytes = 0,
                      std::chrono::steady_clock::time_point startedAt =
                          std::chrono::steady_clock::now()) {
    if (!logger || !logger->enabled()) {
        return;
    }
    (void)logger->write(core::metrics::EventRecord{
        "cpnetflux-gridftp-server",
        std::move(event),
        std::move(transferId),
        std::move(direction),
        std::move(path),
        status.isOk() ? "pass" : "fail",
        core::metrics::classifyStatus(status),
        status.isOk() ? "" : status.message(),
        elapsedSeconds(startedAt),
        bytes,
    });
}

struct AsyncTransferCompletion {
    std::string transferId;
    std::string direction;
    std::string path;
    std::uint64_t bytes = 0;
    std::chrono::steady_clock::time_point startedAt;
    common::Status status;
};

class AsyncTransferRegistry {
   public:
    AsyncTransferRegistry(EventLoggerPtr logger, int controlFd)
        : logger_(std::move(logger)), controlFd_(controlFd),
          completionFd_(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
        jobs_.reserve(2);
    }

    ~AsyncTransferRegistry() {
        if (!jobs_.empty() && controlFd_ >= 0) {
            (void)::shutdown(controlFd_, SHUT_RDWR);
        }
        joinAll();
    }

    [[nodiscard]] std::size_t activeCount() const noexcept { return jobs_.size(); }
    [[nodiscard]] bool valid() const noexcept { return completionFd_.isValid(); }

    template <typename Operation>
    common::Status start(core::io::TlsConnection* control, PassiveListener* passive,
                         std::string transferId, std::string direction, std::string path,
                         std::uint64_t bytes, std::chrono::steady_clock::time_point startedAt,
                         std::uint32_t connections, Operation operation) {
        if (!valid()) {
            return systemStatus("create async transfer eventfd", errno);
        }
        if (activeCount() >= 2) {
            if (passive != nullptr) {
                passive->fd.reset();
                passive->port = 0;
            }
            return sendLine(control, formatReply(450, "Async transfer window is full"));
        }
        if (passive == nullptr || !passive->fd.isValid()) {
            return sendLine(control, formatReply(550, "Passive data listener is not ready"));
        }

        const auto sharedListener =
            std::make_shared<core::io::UniqueFd>(std::move(passive->fd));
        passive->port = 0;
        const std::string prelude =
            "Opening CPNetFlux data connection transfer_id=GFID:" + transferId +
            " connections=" + std::to_string(connections);
        try {
            const std::string jobId = transferId;
            const int completionFd = completionFd_.get();
            std::thread worker([this, completionFd, jobId, direction, path, bytes, startedAt,
                                sharedListener, operation = std::move(operation)]() mutable {
                common::Status status;
                try {
                    status = operation(std::move(*sharedListener), controlFd_);
                } catch (const std::exception& error) {
                    status = common::Status::runtimeError(
                        std::string("async transfer exception: ") + error.what());
                } catch (...) {
                    status = common::Status::runtimeError("async transfer unknown exception");
                }
                {
                    std::lock_guard<std::mutex> lock(completionMutex_);
                    completions_.push_back(AsyncTransferCompletion{
                        jobId, direction, path, bytes, startedAt, std::move(status)});
                }
                const std::uint64_t one = 1;
                ssize_t notified = -1;
                do {
                    notified = ::write(completionFd, &one, sizeof(one));
                } while (notified < 0 && errno == EINTR);
            });
            jobs_.push_back(Job{transferId, std::move(worker)});
        } catch (const std::exception& error) {
            return sendLine(control, formatReply(
                450, std::string("Unable to start async transfer: ") + error.what()));
        } catch (...) {
            return sendLine(control, formatReply(450, "Unable to start async transfer"));
        }

        const common::Status sent = sendLine(control, formatReply(150, prelude));
        if (!sent.isOk()) {
            return sent;
        }
        emitControlEvent(logger_,
                         direction == "upload" ? "stor_start" : "retr_start",
                         direction, path, transferId, common::Status::ok(), bytes, startedAt);
        return common::Status::ok();
    }

    common::Result<std::string> readCommandLine(core::io::TlsConnection* control,
                                                std::string* buffer) {
        while (true) {
            const std::size_t newline = buffer->find('\n');
            if (newline != std::string::npos) {
                std::string line = buffer->substr(0, newline + 1);
                buffer->erase(0, newline + 1);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                    line.pop_back();
                }
                return line;
            }

            pollfd descriptors[2]{};
            descriptors[0].fd = control->fd();
            descriptors[0].events = POLLIN | POLLRDHUP | POLLERR | POLLHUP;
            descriptors[1].fd = completionFd_.get();
            descriptors[1].events = POLLIN;
            const int ready = ::poll(descriptors, 2, -1);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return systemStatus("poll async control", errno);
            }
            if ((descriptors[1].revents & POLLIN) != 0) {
                const common::Status completed = sendReadyCompletions(control);
                if (!completed.isOk()) {
                    return completed;
                }
            }
            if ((descriptors[0].revents & POLLNVAL) != 0) {
                return common::Status::runtimeError("async control fd is invalid");
            }
            if ((descriptors[0].revents & POLLIN) != 0) {
                char chunk[512];
                auto received = control->readSomeNonBlocking(chunk, sizeof(chunk));
                if (!received.isOk()) {
                    return received.status();
                }
                if (received.value() > 0) {
                    buffer->append(chunk, received.value());
                    if (buffer->size() > 8192) {
                        return common::Status::invalidArgument("control line exceeds 8192 bytes");
                    }
                    continue;
                }
            }
            if ((descriptors[0].revents & (POLLHUP | POLLRDHUP | POLLERR)) != 0) {
                return common::Status::runtimeError("control connection closed");
            }
        }
    }

    void joinAll() noexcept {
        for (auto& job : jobs_) {
            if (job.worker.joinable()) {
                job.worker.join();
            }
        }
        jobs_.clear();
    }

   private:
    struct Job {
        std::string transferId;
        std::thread worker;
    };

    common::Status sendReadyCompletions(core::io::TlsConnection* control) {
        std::uint64_t count = 0;
        while (::read(completionFd_.get(), &count, sizeof(count)) > 0) {
        }
        std::deque<AsyncTransferCompletion> ready;
        {
            std::lock_guard<std::mutex> lock(completionMutex_);
            ready.swap(completions_);
        }
        for (auto& completion : ready) {
            const std::string terminal =
                completion.status.isOk()
                    ? "Transfer complete transfer_id=GFID:" + completion.transferId
                    : "Transfer failed transfer_id=GFID:" + completion.transferId + ": " +
                          completion.status.message();
            const common::Status sent =
                sendLine(control, formatReply(completion.status.isOk() ? 226 : 550, terminal));
            emitControlEvent(logger_,
                             completion.status.isOk()
                                 ? (completion.direction == "upload" ? "stor_complete"
                                                                       : "retr_complete")
                                 : (completion.direction == "upload" ? "stor_failed"
                                                                       : "retr_failed"),
                             completion.direction, completion.path, completion.transferId,
                             completion.status, completion.bytes, completion.startedAt);
            auto job = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& candidate) {
                return candidate.transferId == completion.transferId;
            });
            if (job != jobs_.end()) {
                if (job->worker.joinable()) {
                    job->worker.join();
                }
                jobs_.erase(job);
            }
            if (!sent.isOk()) {
                return sent;
            }
        }
        return common::Status::ok();
    }

    EventLoggerPtr logger_;
    int controlFd_ = -1;
    core::io::UniqueFd completionFd_;
    std::mutex completionMutex_;
    std::deque<AsyncTransferCompletion> completions_;
    std::vector<Job> jobs_;
};

common::Result<std::string> readLine(core::io::TlsConnection* connection, std::string* buffer) {
    while (true) {
        const std::size_t newline = buffer->find('\n');
        if (newline != std::string::npos) {
            std::string line = buffer->substr(0, newline + 1);
            buffer->erase(0, newline + 1);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            return line;
        }

        char chunk[512];
        auto received = connection->readSome(chunk, sizeof(chunk));
        if (!received.isOk()) {
            return received.status();
        }
        if (received.value() > 0) {
            buffer->append(chunk, received.value());
            if (buffer->size() > 8192) {
                return common::Status::invalidArgument("control line exceeds 8192 bytes");
            }
            continue;
        }
    }
}

std::string generateTransferId() {
    constexpr char kDigits[] = "0123456789abcdef";
    std::random_device random;
    std::string id(32, '0');
    for (char& value : id) {
        value = kDigits[random() & 0x0F];
    }
    return id;
}

common::Result<PassiveListener> openPassiveListener(const ControlServerOptions& options,
                                                    std::uint32_t connections) {
    const std::uint32_t maxPort = std::numeric_limits<std::uint16_t>::max();
    for (std::uint32_t offset = 0; offset < kPassiveScanLimit; ++offset) {
        const std::uint32_t port = static_cast<std::uint32_t>(options.dataPortBase) + offset;
        if (port == 0 || port > maxPort) {
            break;
        }
        auto listener = core::io::createListener(
            options.host.c_str(), static_cast<std::uint16_t>(port), static_cast<int>(connections));
        if (!listener.isOk()) {
            continue;
        }
        PassiveListener passive;
        passive.fd = std::move(listener.value());
        passive.port = static_cast<std::uint16_t>(port);
        return passive;
    }
    return common::Status::runtimeError("no passive data port available");
}

std::string epsvReply(std::uint16_t port) {
    return formatReply(229, "Entering Extended Passive Mode (|||" + std::to_string(port) + "|)");
}

std::string pasvHostForReply(int controlFd, const std::string& configuredHost) {
    if (configuredHost != "0.0.0.0" && configuredHost != "::") {
        return configuredHost;
    }

    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (::getsockname(controlFd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return "127.0.0.1";
    }
    if (address.ss_family == AF_INET) {
        char text[INET_ADDRSTRLEN];
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        if (::inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text)) != nullptr) {
            return text;
        }
    }
    return "127.0.0.1";
}

common::Result<std::string> pasvReply(int controlFd, const std::string& configuredHost,
                                      std::uint16_t port) {
    const std::string host = pasvHostForReply(controlFd, configuredHost);
    in_addr address{};
    if (::inet_pton(AF_INET, host.c_str(), &address) != 1) {
        return common::Status::invalidArgument("PASV requires an IPv4 bind address");
    }

    const auto* octets = reinterpret_cast<const unsigned char*>(&address.s_addr);
    std::ostringstream payload;
    payload << "Entering Passive Mode (" << static_cast<int>(octets[0]) << ","
            << static_cast<int>(octets[1]) << "," << static_cast<int>(octets[2]) << ","
            << static_cast<int>(octets[3]) << "," << (port / 256) << "," << (port % 256) << ")";
    return formatReply(227, payload.str());
}

common::Result<core::io::UniqueFd> acceptDataConnection(int listenerFd) {
    while (true) {
        const int acceptedFd = ::accept4(listenerFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (acceptedFd >= 0) {
            return core::io::UniqueFd(acceptedFd);
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            pollfd pollFd{};
            pollFd.fd = listenerFd;
            pollFd.events = POLLIN;
            const int ready = ::poll(&pollFd, 1, 30000);
            if (ready > 0) {
                continue;
            }
            if (ready == 0) {
                return common::Status::runtimeError("timed out waiting for data connection");
            }
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("poll data listener", errno);
        }
        return systemStatus("accept data", errno);
    }
}

common::Status sendAsciiData(PassiveListener* passive, const std::string& payload) {
    if (passive == nullptr || !passive->fd.isValid()) {
        return common::Status::runtimeError("passive data listener is not ready");
    }
    auto dataFd = acceptDataConnection(passive->fd.get());
    if (!dataFd.isOk()) {
        return dataFd.status();
    }
    passive->fd.reset();
    passive->port = 0;
    return sendAllRaw(dataFd.value().get(), payload);
}

common::Result<std::uint64_t> fileSizeOfPath(const std::string& path) {
    struct stat statBuffer {};
    if (::stat(path.c_str(), &statBuffer) != 0) {
        return systemStatus("stat file", errno);
    }
    if (statBuffer.st_size < 0) {
        return common::Status::runtimeError("file size is negative");
    }
    return static_cast<std::uint64_t>(statBuffer.st_size);
}

common::Result<std::int64_t> mtimeOfPath(const std::string& path) {
    struct stat statBuffer {};
    if (::stat(path.c_str(), &statBuffer) != 0) {
        return systemStatus("stat file", errno);
    }
    return static_cast<std::int64_t>(statBuffer.st_mtime);
}

common::Result<std::vector<ControlListEntry>> readDirectoryEntries(const std::string& directory) {
    std::vector<ControlListEntry> entries;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            return common::Status::systemError("directory iteration failed: " + error.message(),
                                               error.value());
        }
        const std::string name = entry.path().filename().string();
        if (name.empty() || name == "." || name == "..") {
            continue;
        }
        ControlListEntry item;
        item.name = name;
        item.isDirectory = entry.is_directory(error);
        if (error) {
            return common::Status::systemError("directory entry type failed: " + error.message(),
                                               error.value());
        }
        const bool isRegularFile = !item.isDirectory && entry.is_regular_file(error);
        if (error) {
            return common::Status::systemError("directory entry type failed: " + error.message(),
                                               error.value());
        }
        if (isRegularFile && core::tree::isInternalTransferSidecar(entry.path().string())) {
            continue;
        }
        if (isRegularFile) {
            item.size = entry.file_size(error);
            if (error) {
                return common::Status::systemError(
                    "directory entry size failed: " + error.message(), error.value());
            }
        }
        auto mtime = mtimeOfPath(entry.path().string());
        if (!mtime.isOk()) {
            return mtime.status();
        }
        item.mtimeUnixSeconds = mtime.value();
        entries.push_back(std::move(item));
    }
    if (error) {
        return common::Status::systemError("directory iteration failed: " + error.message(),
                                           error.value());
    }
    std::sort(entries.begin(), entries.end(),
              [](const ControlListEntry& left, const ControlListEntry& right) {
                  return left.name < right.name;
              });
    return entries;
}

common::Status validateOutputBeforeStor(const std::string& outputPath, bool resume) {
    auto outputExists = storage::PosixFile::pathExists(outputPath);
    if (!outputExists.isOk()) {
        return outputExists.status();
    }
    if (outputExists.value()) {
        return common::Status::invalidArgument("output file already exists");
    }

    const std::string manifestPath = checkpoint::manifestPathForOutput(outputPath);
    auto manifestExists = storage::PosixFile::pathExists(manifestPath);
    if (!manifestExists.isOk()) {
        return manifestExists.status();
    }
    if (resume && !manifestExists.value()) {
        return common::Status::invalidArgument("manifest is required for REST resume");
    }
    if (!resume && manifestExists.value()) {
        return common::Status::invalidArgument("manifest already exists; use REST GFID token");
    }
    return common::Status::ok();
}

common::Status runSize(core::io::TlsConnection* control, const ControlServerOptions& controlOptions,
                       const ControlSession& session, const ControlResponse& response,
                       const EventLoggerPtr& logger) {
    auto path = resolveControlPath(controlOptions.root, session.workingDirectory(), response.path,
                                   ControlPathKind::ExistingFile, "SIZE");
    if (!path.isOk()) {
        emitControlEvent(logger, "metadata_failed", "", response.path, "", path.status());
        return sendLine(control, formatReply(550, path.status().message()));
    }
    auto size = fileSizeOfPath(path.value().fullPath);
    if (!size.isOk()) {
        emitControlEvent(logger, "metadata_failed", "", response.path, "", size.status());
        return sendLine(control, formatReply(550, size.status().message()));
    }
    return sendLine(control, formatReply(213, std::to_string(size.value())));
}

common::Status runMdtm(core::io::TlsConnection* control, const ControlServerOptions& controlOptions,
                       const ControlSession& session, const ControlResponse& response,
                       const EventLoggerPtr& logger) {
    auto path = resolveControlPath(controlOptions.root, session.workingDirectory(), response.path,
                                   ControlPathKind::ExistingFile, "MDTM");
    if (!path.isOk()) {
        emitControlEvent(logger, "metadata_failed", "", response.path, "", path.status());
        return sendLine(control, formatReply(550, path.status().message()));
    }
    auto mtime = mtimeOfPath(path.value().fullPath);
    if (!mtime.isOk()) {
        emitControlEvent(logger, "metadata_failed", "", response.path, "", mtime.status());
        return sendLine(control, formatReply(550, mtime.status().message()));
    }
    return sendLine(control, formatReply(213, formatMdtmTime(mtime.value())));
}

common::Status runCwd(core::io::TlsConnection* control, const ControlServerOptions& controlOptions,
                      ControlSession* session, const ControlResponse& response,
                      const EventLoggerPtr& logger) {
    if (response.path == ".." && session->workingDirectory() == "/") {
        session->setWorkingDirectory("/");
        return sendLine(control, formatReply(250, "Directory changed to /"));
    }
    auto path = resolveControlPath(controlOptions.root, session->workingDirectory(), response.path,
                                   ControlPathKind::ExistingDirectory, "CWD");
    if (!path.isOk()) {
        emitControlEvent(logger, "metadata_failed", "", response.path, "", path.status());
        return sendLine(control, formatReply(550, path.status().message()));
    }
    session->setWorkingDirectory(path.value().virtualPath);
    return sendLine(control,
                    formatReply(250, "Directory changed to " + path.value().virtualPath));
}

common::Status runListLike(core::io::TlsConnection* control, ControlSession& session, PassiveListener* passive,
                           const ControlServerOptions& controlOptions,
                           const ControlResponse& response, bool namesOnly,
                           const EventLoggerPtr& logger) {
    if (passive == nullptr || !passive->fd.isValid()) {
        emitControlEvent(logger, namesOnly ? "nlst_failed" : "list_failed", "", response.path, "",
                         common::Status::runtimeError("Passive data listener is not ready"));
        return sendLine(control, formatReply(550, "Passive data listener is not ready"));
    }
    auto path = resolveControlPath(controlOptions.root, session.workingDirectory(), response.path,
                                   ControlPathKind::ExistingDirectory, namesOnly ? "NLST" : "LIST");
    if (!path.isOk()) {
        passive->fd.reset();
        session.clearPassiveReady();
        emitControlEvent(logger, namesOnly ? "nlst_failed" : "list_failed", "", response.path, "",
                         path.status());
        return sendLine(control, formatReply(550, path.status().message()));
    }
    auto entries = readDirectoryEntries(path.value().fullPath);
    if (!entries.isOk()) {
        passive->fd.reset();
        session.clearPassiveReady();
        emitControlEvent(logger, namesOnly ? "nlst_failed" : "list_failed", "", response.path, "",
                         entries.status());
        return sendLine(control, formatReply(550, entries.status().message()));
    }

    const std::string payload =
        namesOnly ? formatNlst(entries.value()) : formatList(entries.value());
    common::Status status = sendLine(
        control, formatReply(150, namesOnly ? "Opening NLST data" : "Opening LIST data"));
    if (!status.isOk()) {
        return status;
    }
    status = sendAsciiData(passive, payload);
    session.clearPassiveReady();
    if (!status.isOk()) {
        (void)sendLine(control, formatReply(425, status.message()));
        emitControlEvent(logger, namesOnly ? "nlst_failed" : "list_failed", "", response.path, "",
                         status);
        return status;
    }
    return sendLine(control, formatReply(226, namesOnly ? "NLST complete" : "LIST complete"));
}

common::Status runStor(core::io::TlsConnection* control, ControlSession& session, PassiveListener* passive,
                       const ControlServerOptions& controlOptions,
                       const ControlResponse& response, const EventLoggerPtr& logger,
                       AsyncTransferRegistry* asyncTransfers) {
    if (passive == nullptr || !passive->fd.isValid()) {
        emitControlEvent(logger, "stor_failed", "upload", response.path, "",
                         common::Status::runtimeError("Passive data listener is not ready"));
        return sendLine(control, formatReply(550, "Passive data listener is not ready"));
    }

    auto outputPath =
        resolveStorPath(controlOptions.root, session.workingDirectory(), response.path);
    if (!outputPath.isOk()) {
        emitControlEvent(logger, "stor_failed", "upload", response.path, "",
                         outputPath.status());
        return sendLine(control, formatReply(550, outputPath.status().message()));
    }

    const std::string transferId = response.resume ? response.transferId : generateTransferId();
    const common::Status outputStatus =
        validateOutputBeforeStor(outputPath.value(), response.resume);
    if (!outputStatus.isOk()) {
        emitControlEvent(logger, "stor_failed", "upload", response.path, transferId, outputStatus);
        return sendLine(control, formatReply(550, outputStatus.message()));
    }

    config::FileTransferOptions fileOptions;
    fileOptions.host = controlOptions.host;
    fileOptions.port = passive->port;
    fileOptions.connections = response.connections;
    fileOptions.bufferSize = controlOptions.bufferSize;
    fileOptions.chunkSize = controlOptions.chunkSize;
    fileOptions.path = outputPath.value();
    fileOptions.transferId = transferId;
    fileOptions.checksumAlgorithm = controlOptions.checksumAlgorithm;
    fileOptions.checksumBackend = controlOptions.checksumBackend;
    fileOptions.manifestFlushPolicy = controlOptions.manifestFlushPolicy;
    fileOptions.manifestFlushIntervalChunks = controlOptions.manifestFlushIntervalChunks;
    fileOptions.finalVerifyPolicy = controlOptions.finalVerifyPolicy;
    fileOptions.commitSyncPolicy = controlOptions.commitSyncPolicy;
    fileOptions.preallocateMode = controlOptions.preallocateMode;
    fileOptions.fileIo = controlOptions.fileIo;
    fileOptions.dataTls = controlOptions.tls;
    fileOptions.dataTls.mode = controlOptions.dataTlsMode == core::io::DataTlsMode::Required
                                   ? core::io::TlsMode::Required
                                   : core::io::TlsMode::Off;
    fileOptions.dataTlsMode = controlOptions.dataTlsMode;
    fileOptions.resume = response.resume;

    const std::string prelude = "Opening CPNetFlux data connection transfer_id=GFID:" + transferId +
                                " connections=" + std::to_string(response.connections);
    const auto startedAt = std::chrono::steady_clock::now();
    if (asyncTransfers != nullptr) {
        return asyncTransfers->start(
            control, passive, transferId, "upload", response.path, 0, startedAt, response.connections,
            [fileOptions = std::move(fileOptions)](core::io::UniqueFd listener,
                                                   int controlFd) mutable {
                return core::io::runFileTransferServerOnListener(
                    fileOptions, std::move(listener), controlFd);
            });
    }
    common::Status status = sendLine(control, formatReply(150, prelude));
    if (!status.isOk()) {
        emitControlEvent(logger, "stor_failed", "upload", response.path, transferId, status);
        return status;
    }

    emitControlEvent(logger, "stor_start", "upload", response.path, transferId,
                     common::Status::ok(), 0, startedAt);
    core::io::UniqueFd listener = std::move(passive->fd);
    passive->port = 0;
    status = core::io::runFileTransferServerOnListener(fileOptions, std::move(listener), session.pipelineOptIn() ? control->fd() : -1);
    if (!status.isOk()) {
        (void)sendLine(control, formatReply(550, "Transfer failed transfer_id=GFID:" + transferId + ": " + status.message()));
        emitControlEvent(logger, "stor_failed", "upload", response.path, transferId, status, 0,
                         startedAt);
        return status;
    }

    session.clearPassiveReady();
    auto bytes = fileSizeOfPath(outputPath.value());
    emitControlEvent(logger, "stor_complete", "upload", response.path, transferId,
                     common::Status::ok(), bytes.isOk() ? bytes.value() : 0, startedAt);
    return sendLine(control,
                    formatReply(226, "Transfer complete transfer_id=GFID:" + transferId));
}

common::Status runRetr(core::io::TlsConnection* control, ControlSession& session, PassiveListener* passive,
                       const ControlServerOptions& controlOptions,
                       const ControlResponse& response, const EventLoggerPtr& logger,
                       AsyncTransferRegistry* asyncTransfers) {
    if (passive == nullptr || !passive->fd.isValid()) {
        emitControlEvent(logger, "retr_failed", "download", response.path, "",
                         common::Status::runtimeError("Passive data listener is not ready"));
        return sendLine(control, formatReply(550, "Passive data listener is not ready"));
    }

    auto inputPath =
        resolveRetrPath(controlOptions.root, session.workingDirectory(), response.path);
    if (!inputPath.isOk()) {
        emitControlEvent(logger, "retr_failed", "download", response.path, "",
                         inputPath.status());
        return sendLine(control, formatReply(550, inputPath.status().message()));
    }

    const std::string transferId = response.resume ? response.transferId : generateTransferId();
    auto sourceVirtualPath = resolveVirtualPath(session.workingDirectory(), response.path, false);
    if (!sourceVirtualPath.isOk()) {
        emitControlEvent(logger, "retr_failed", "download", response.path, transferId,
                         sourceVirtualPath.status());
        return sendLine(control, formatReply(550, sourceVirtualPath.status().message()));
    }
    std::string sourcePath = sourceVirtualPath.value();
    if (!sourcePath.empty() && sourcePath.front() == '/') {
        sourcePath.erase(sourcePath.begin());
    }
    core::io::FileDownloadSenderOptions senderOptions;
    senderOptions.path = inputPath.value();
    senderOptions.transferId = transferId;
    senderOptions.connections = response.connections;
    senderOptions.chunkSize = controlOptions.chunkSize;
    senderOptions.bufferSize = controlOptions.bufferSize;
    senderOptions.checksumAlgorithm = controlOptions.checksumAlgorithm;
    senderOptions.checksumBackend = controlOptions.checksumBackend;
    senderOptions.fileIo = controlOptions.fileIo;
    senderOptions.dataTls = controlOptions.tls;
    senderOptions.dataTls.mode = controlOptions.dataTlsMode == core::io::DataTlsMode::Required
                                     ? core::io::TlsMode::Required
                                     : core::io::TlsMode::Off;
    senderOptions.dataTlsMode = controlOptions.dataTlsMode;
    senderOptions.resume = response.resume;
    senderOptions.sourcePath = sourcePath;

    const std::string prelude =
        "Opening CPNetFlux download data connection transfer_id=GFID:" + transferId +
        " connections=" + std::to_string(response.connections);
    const auto startedAt = std::chrono::steady_clock::now();
    auto bytes = fileSizeOfPath(inputPath.value());
    if (asyncTransfers != nullptr) {
        return asyncTransfers->start(
            control, passive, transferId, "download", response.path,
            bytes.isOk() ? bytes.value() : 0, startedAt, response.connections,
            [senderOptions = std::move(senderOptions)](core::io::UniqueFd listener,
                                                       int controlFd) mutable {
                return core::io::runFramedFileSenderOnListener(
                    senderOptions, std::move(listener), controlFd);
            });
    }
    common::Status status = sendLine(control, formatReply(150, prelude));
    if (!status.isOk()) {
        emitControlEvent(logger, "retr_failed", "download", response.path, transferId, status);
        return status;
    }

    emitControlEvent(logger, "retr_start", "download", response.path, transferId,
                     common::Status::ok(), bytes.isOk() ? bytes.value() : 0, startedAt);
    core::io::UniqueFd listener = std::move(passive->fd);
    passive->port = 0;
    status = core::io::runFramedFileSenderOnListener(senderOptions, std::move(listener), session.pipelineOptIn() ? control->fd() : -1);
    if (!status.isOk()) {
        (void)sendLine(control, formatReply(550, "Transfer failed transfer_id=GFID:" + transferId + ": " + status.message()));
        emitControlEvent(logger, "retr_failed", "download", response.path, transferId, status,
                         bytes.isOk() ? bytes.value() : 0, startedAt);
        return status;
    }

    session.clearPassiveReady();
    emitControlEvent(logger, "retr_complete", "download", response.path, transferId,
                     common::Status::ok(), bytes.isOk() ? bytes.value() : 0, startedAt);
    return sendLine(control,
                    formatReply(226, "Transfer complete transfer_id=GFID:" + transferId));
}

void handleControlConnection(core::io::UniqueFd controlFd, ControlServerOptions options,
                             EventLoggerPtr logger,
                             std::shared_ptr<core::io::TlsServerContext> tlsContext) {
    core::io::TlsConnection control;
    if (options.tls.mode == core::io::TlsMode::Required) {
        if (!tlsContext) {
            emitControlEvent(logger, "tls_handshake_failed", "", "", "",
                             common::Status::runtimeError("TLS required but unavailable"));
            return;
        }
        auto tlsConnection = tlsContext->accept(std::move(controlFd));
        if (!tlsConnection.isOk()) {
            emitControlEvent(logger, "tls_handshake_failed", "", "", "", tlsConnection.status());
            return;
        }
        control = std::move(tlsConnection.value());
        emitControlEvent(logger, "tls_handshake_success", "", "", "", common::Status::ok());
    } else {
        control = core::io::TlsConnection::plain(std::move(controlFd));
    }

    ControlSession session(options.auth, options.connections);
    PassiveListener passive;
    std::string inputBuffer;
    AsyncTransferRegistry asyncTransfers(logger, control.fd());

    if (!sendLine(&control, formatReply(220, "CPNetFlux GridFTP control ready")).isOk()) {
        return;
    }

    while (control.valid()) {
        auto line = session.pipelineOptIn()
                        ? asyncTransfers.readCommandLine(&control, &inputBuffer)
                        : readLine(&control, &inputBuffer);
        if (!line.isOk()) {
            return;
        }

        auto command = parseControlCommand(line.value());
        if (!command.isOk()) {
            emitControlEvent(logger, "command_failed", "", "", "", command.status());
            (void)sendLine(&control, formatReply(550, command.status().message()));
            continue;
        }

        if (asyncTransfers.activeCount() > 0) {
            const auto type = command.value().type;
            const bool allowed = type == ControlCommandType::Epsv ||
                                 type == ControlCommandType::Pasv ||
                                 type == ControlCommandType::Stor ||
                                 type == ControlCommandType::Retr ||
                                 type == ControlCommandType::Size ||
                                 type == ControlCommandType::Mdtm ||
                                 type == ControlCommandType::Noop;
            if (!allowed) {
                passive.fd.reset();
                passive.port = 0;
                session.clearPassiveReady();
                (void)sendLine(&control,
                               formatReply(503, "Command unavailable while async transfer is active"));
                continue;
            }
        }

        const bool wasAuthenticated = session.authenticated();
        ControlResponse response = session.handleCommand(command.value());
        const int responseCode = replyCode(response);
        if (command.value().type == ControlCommandType::Pass) {
            if (responseCode == 230) {
                emitControlEvent(logger, "auth_success", "", "", "", common::Status::ok());
            } else if (responseCode == 530) {
                emitControlEvent(logger, "auth_failed", "", "", "",
                                 common::Status::invalidArgument("Login incorrect"));
            }
        } else if (!wasAuthenticated && isProtectedCommand(command.value().type) &&
                   responseCode == 530) {
            emitControlEvent(logger, "protected_command_rejected", "", command.value().argument, "",
                             common::Status::invalidArgument("auth required"));
        }
        if (response.action == ControlAction::OpenPassiveEpsv ||
            response.action == ControlAction::OpenPassivePasv) {
            passive.fd.reset();
            auto passiveResult = openPassiveListener(options, session.connections());
            if (!passiveResult.isOk()) {
                (void)sendLine(&control, formatReply(421, "Cannot open passive data listener"));
                return;
            }
            passive = std::move(passiveResult.value());
            session.markPassiveReady();
            if (response.action == ControlAction::OpenPassiveEpsv) {
                (void)sendLine(&control, epsvReply(passive.port));
            } else {
                auto reply = pasvReply(control.fd(), options.host, passive.port);
                if (!reply.isOk()) {
                    (void)sendLine(&control, formatReply(550, reply.status().message()));
                    passive.fd.reset();
                    session.clearPassiveReady();
                } else {
                    (void)sendLine(&control, reply.value());
                }
            }
            continue;
        }

        if (response.action == ControlAction::StartStor) {
            (void)runStor(&control, session, &passive, options, response, logger,
                          session.pipelineOptIn() ? &asyncTransfers : nullptr);
            continue;
        }

        if (response.action == ControlAction::StartRetr) {
            (void)runRetr(&control, session, &passive, options, response, logger,
                          session.pipelineOptIn() ? &asyncTransfers : nullptr);
            continue;
        }

        if (response.action == ControlAction::QuerySize) {
            (void)runSize(&control, options, session, response, logger);
            continue;
        }

        if (response.action == ControlAction::QueryMdtm) {
            (void)runMdtm(&control, options, session, response, logger);
            continue;
        }

        if (response.action == ControlAction::ChangeDirectory) {
            (void)runCwd(&control, options, &session, response, logger);
            continue;
        }

        if (response.action == ControlAction::StartList) {
            (void)runListLike(&control, session, &passive, options, response, false, logger);
            continue;
        }

        if (response.action == ControlAction::StartNlst) {
            (void)runListLike(&control, session, &passive, options, response, true, logger);
            continue;
        }

        const common::Status sendStatus = sendResponse(&control, response);
        if (!sendStatus.isOk() || response.action == ControlAction::Quit) {
            return;
        }
    }
}

}  // namespace

common::Status runControlServer(const ControlServerOptions& options) {
    EventLoggerPtr eventLogger;
    if (!options.eventLogPath.empty()) {
        auto logger = core::metrics::EventLogger::open(options.eventLogPath);
        if (!logger.isOk()) {
            return logger.status();
        }
        eventLogger = std::make_shared<core::metrics::EventLogger>(std::move(logger.value()));
    }
    std::shared_ptr<core::io::TlsServerContext> tlsContext;
    if (options.tls.mode == core::io::TlsMode::Explicit) {
        return common::Status::invalidArgument(
            "TLS explicit mode is reserved for a later phase");
    }
    if (options.tls.mode == core::io::TlsMode::Required) {
        auto context = core::io::TlsServerContext::create(options.tls);
        if (!context.isOk()) {
            emitControlEvent(eventLogger, "tls_config_failed", "", "", "", context.status());
            return context.status();
        }
        tlsContext = std::make_shared<core::io::TlsServerContext>(std::move(context.value()));
    }
    const common::Status dataTlsStatus =
        core::io::validateDataTlsServerConfig(options.tls.mode, options.dataTlsMode, options.tls);
    if (!dataTlsStatus.isOk()) {
        emitControlEvent(eventLogger, "data_tls_config_failed", "", "", "", dataTlsStatus);
        return dataTlsStatus;
    }

    auto listenerResult = core::io::createListener(options.host.c_str(), options.port, 32);
    if (!listenerResult.isOk()) {
        return listenerResult.status();
    }
    core::io::UniqueFd listener = std::move(listenerResult.value());

    std::cout << "gridftp_control_server listening host=" << options.host
              << " port=" << options.port << " root=" << options.root
              << " data_port_base=" << options.dataPortBase
              << " passive_window=[" << options.dataPortBase << ","
              << (static_cast<std::uint32_t>(options.dataPortBase) + kPassiveScanLimit - 1)
              << "]"
              << " connections=" << options.connections
              << " auth_mode=" << authModeName(options.auth.mode)
              << " tls_mode=" << core::io::tlsModeName(options.tls.mode)
              << " data_tls_mode=" << core::io::dataTlsModeName(options.dataTlsMode) << '\n'
              << std::flush;
    emitControlEvent(eventLogger, "server_start", "", options.root, "", common::Status::ok());

    while (true) {
        pollfd pollFd{};
        pollFd.fd = listener.get();
        pollFd.events = POLLIN;
        const int ready = ::poll(&pollFd, 1, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("poll control listener", errno);
        }

        while (true) {
            const int acceptedFd = ::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC);
            if (acceptedFd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                return systemStatus("accept control", errno);
            }

            std::thread(handleControlConnection, core::io::UniqueFd(acceptedFd), options,
                        eventLogger, tlsContext)
                .detach();
        }
    }
}

}  // namespace cpnetflux::protocol::control
