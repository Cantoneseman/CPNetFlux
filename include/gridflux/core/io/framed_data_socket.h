#pragma once

#include <cstddef>
#include <cstdint>

#include "gridflux/common/status.h"
#include "gridflux/core/io/socket_utils.h"
#include "gridflux/core/io/tls_socket.h"

namespace gridflux::core::io {

class FramedDataSocket {
   public:
    FramedDataSocket();
    explicit FramedDataSocket(UniqueFd fd);
    explicit FramedDataSocket(TlsConnection connection);

    FramedDataSocket(const FramedDataSocket&) = delete;
    FramedDataSocket& operator=(const FramedDataSocket&) = delete;
    FramedDataSocket(FramedDataSocket&& other) noexcept;
    FramedDataSocket& operator=(FramedDataSocket&& other) noexcept;

    [[nodiscard]] int fd() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool tlsEnabled() const noexcept;
    [[nodiscard]] common::Status writeAll(const std::uint8_t* data, std::size_t length);
    [[nodiscard]] common::Result<std::size_t> readSome(std::uint8_t* data, std::size_t length);
    [[nodiscard]] common::Status readAll(std::uint8_t* data, std::size_t length);

   private:
    bool tlsMode_ = false;
    UniqueFd fd_;
    TlsConnection tls_;
};

[[nodiscard]] common::Result<FramedDataSocket> connectFramedDataSocket(
    const char* host, std::uint16_t port, DataTlsMode dataTlsMode, const TlsConfig& tlsConfig);

[[nodiscard]] common::Result<FramedDataSocket> acceptFramedDataSocket(
    UniqueFd fd, DataTlsMode dataTlsMode, const TlsConfig& tlsConfig);

}  // namespace gridflux::core::io
