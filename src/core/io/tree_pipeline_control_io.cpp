#include "cpnetflux/core/io/tree_pipeline_control_io.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <climits>
#include <poll.h>
#include <sys/socket.h>

namespace cpnetflux::core::io::detail {
namespace {

int timeoutMilliseconds(std::chrono::steady_clock::time_point deadline) {
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
        return 0;
    }
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
    const auto roundedUp = (nanoseconds + 999999) / 1000000;
    return static_cast<int>(std::min<std::int64_t>(roundedUp, INT_MAX));
}

}  // namespace

std::optional<int> parseControlReplyCode(std::string_view line) noexcept {
    if (line.size() < 3 || !std::isdigit(static_cast<unsigned char>(line[0])) ||
        !std::isdigit(static_cast<unsigned char>(line[1])) ||
        !std::isdigit(static_cast<unsigned char>(line[2]))) {
        return std::nullopt;
    }
    return (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
}

common::Status validatePipelineEnableReply(int code) {
    if (code != 200) {
        return common::Status::runtimeError("OPTS PIPELINE=1 rejected");
    }
    return common::Status::ok();
}

common::Result<std::string> readTreePipelineControlLine(
    TlsConnection* control, std::string* buffer, const std::atomic<bool>* cancelled,
    ControlReadDeadline deadline) {
    if (control == nullptr || !control->valid() || buffer == nullptr || cancelled == nullptr) {
        return common::Status::invalidArgument("invalid tree pipeline control reader");
    }

    while (true) {
        if (cancelled->load(std::memory_order_acquire)) {
            return common::Status::runtimeError("tree pipeline candidate cancelled");
        }

        const std::size_t newline = buffer->find('\n');
        if (newline != std::string::npos) {
            std::string line = buffer->substr(0, newline + 1);
            buffer->erase(0, newline + 1);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            return line;
        }

        bool peerClosed = false;
        if (deadline.has_value()) {
            const int timeout = timeoutMilliseconds(*deadline);
            if (timeout == 0) {
                return common::Status::runtimeError("tree pipeline control reply timed out");
            }

            if (!control->hasPendingRead()) {
                pollfd descriptor{};
                descriptor.fd = control->fd();
                descriptor.events = POLLIN;
                const int ready = ::poll(&descriptor, 1, timeout);
                if (ready < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return common::Status::systemError("poll tree pipeline control",
                                                       errno);
                }
                if (ready == 0) {
                    continue;
                }
                if ((descriptor.revents & POLLNVAL) != 0) {
                    return common::Status::runtimeError("tree pipeline control fd is invalid");
                }
                peerClosed = (descriptor.revents & (POLLHUP | POLLERR)) != 0;
                if (cancelled->load(std::memory_order_acquire)) {
                    return common::Status::runtimeError("tree pipeline candidate cancelled");
                }
            }

            // Use a nonblocking recv/SSL_read after poll. A blocking TLS read
            // can otherwise outlive the absolute deadline while receiving a
            // trickle of partial records.
        }

        char chunk[512];
        auto received = deadline.has_value()
                            ? control->readSomeNonBlocking(chunk, sizeof(chunk))
                            : control->readSome(chunk, sizeof(chunk));
        if (!received.isOk()) {
            if (cancelled->load(std::memory_order_acquire)) {
                return common::Status::runtimeError("tree pipeline candidate cancelled");
            }
            if (deadline.has_value() &&
                std::chrono::steady_clock::now() >= *deadline) {
                return common::Status::runtimeError("tree pipeline control reply timed out");
            }
            return received.status();
        }
        if (received.value() > 0) {
            buffer->append(chunk, received.value());
            continue;
        }
        if (peerClosed) {
            return common::Status::runtimeError("tree pipeline control connection closed");
        }
        // TLS may return zero for WANT_READ/EINTR. Retry; an optional absolute
        // deadline bounds the pipeline path without changing depth-zero reads.
    }
}

}  // namespace cpnetflux::core::io::detail
