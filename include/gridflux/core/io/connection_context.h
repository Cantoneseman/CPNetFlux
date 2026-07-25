#pragma once

#include <cstdint>

namespace gridflux::core::io {

enum class ConnectionState {
    Created,
    Connecting,
    Connected,
    Closing,
    Closed,
    Error,
};

class ConnectionContext {
   public:
    ConnectionContext() = default;
    explicit ConnectionContext(int fd) noexcept;

    [[nodiscard]] int fd() const noexcept;
    [[nodiscard]] ConnectionState state() const noexcept;
    [[nodiscard]] bool eof() const noexcept;
    [[nodiscard]] int errorNumber() const noexcept;
    [[nodiscard]] std::uint64_t bytesReceived() const noexcept;
    [[nodiscard]] std::uint64_t bytesSent() const noexcept;

    void setFd(int fd) noexcept;
    void markConnecting() noexcept;
    void markConnected() noexcept;
    void markClosing() noexcept;
    void markClosed() noexcept;
    void markEof() noexcept;
    void markError(int errorNumber) noexcept;
    void addBytesReceived(std::uint64_t bytes) noexcept;
    void addBytesSent(std::uint64_t bytes) noexcept;

   private:
    int fd_ = -1;
    ConnectionState state_ = ConnectionState::Created;
    bool eof_ = false;
    int errorNumber_ = 0;
    std::uint64_t bytesReceived_ = 0;
    std::uint64_t bytesSent_ = 0;
};

}  // namespace gridflux::core::io
