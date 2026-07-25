#include "gridflux/core/io/connection_context.h"

namespace gridflux::core::io {

ConnectionContext::ConnectionContext(int fd) noexcept : fd_(fd) {}

int ConnectionContext::fd() const noexcept { return fd_; }

ConnectionState ConnectionContext::state() const noexcept { return state_; }

bool ConnectionContext::eof() const noexcept { return eof_; }

int ConnectionContext::errorNumber() const noexcept { return errorNumber_; }

std::uint64_t ConnectionContext::bytesReceived() const noexcept { return bytesReceived_; }

std::uint64_t ConnectionContext::bytesSent() const noexcept { return bytesSent_; }

void ConnectionContext::setFd(int fd) noexcept { fd_ = fd; }

void ConnectionContext::markConnecting() noexcept { state_ = ConnectionState::Connecting; }

void ConnectionContext::markConnected() noexcept { state_ = ConnectionState::Connected; }

void ConnectionContext::markClosing() noexcept { state_ = ConnectionState::Closing; }

void ConnectionContext::markClosed() noexcept { state_ = ConnectionState::Closed; }

void ConnectionContext::markEof() noexcept {
    eof_ = true;
    state_ = ConnectionState::Closed;
}

void ConnectionContext::markError(int errorNumber) noexcept {
    errorNumber_ = errorNumber;
    state_ = ConnectionState::Error;
}

void ConnectionContext::addBytesReceived(std::uint64_t bytes) noexcept { bytesReceived_ += bytes; }

void ConnectionContext::addBytesSent(std::uint64_t bytes) noexcept { bytesSent_ += bytes; }

}  // namespace gridflux::core::io
