#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/data_transport.h"
#include "cpnetflux/core/io/rdma_data_socket.h"
#include "cpnetflux/core/io/socket_utils.h"
#include "cpnetflux/core/io/tls_socket.h"

namespace cpnetflux::core::io {

class FramedDataSocket {
   public:
    FramedDataSocket();
    ~FramedDataSocket();
    explicit FramedDataSocket(UniqueFd fd);
    explicit FramedDataSocket(TlsConnection connection);
    explicit FramedDataSocket(RdmaDataSocket connection);

    FramedDataSocket(const FramedDataSocket&) = delete;
    FramedDataSocket& operator=(const FramedDataSocket&) = delete;
    FramedDataSocket(FramedDataSocket&& other) noexcept;
    FramedDataSocket& operator=(FramedDataSocket&& other) noexcept;

    [[nodiscard]] int fd() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool tlsEnabled() const noexcept;
    [[nodiscard]] DataTransport transport() const noexcept;
    void shutdown() noexcept;
    void close() noexcept;
    [[nodiscard]] common::Status writeAll(const std::uint8_t* data, std::size_t length);
    [[nodiscard]] common::Status writeFrame(const std::uint8_t* header,
                                            std::size_t headerLength,
                                            const std::uint8_t* payload,
                                            std::size_t payloadLength);
    [[nodiscard]] common::Result<std::size_t> readSome(std::uint8_t* data, std::size_t length);
    [[nodiscard]] common::Status readAll(std::uint8_t* data, std::size_t length);

    [[nodiscard]] common::Status writeFrame(const std::uint8_t* header,
                                            std::size_t headerLength,
                                            const std::uint8_t* prefix,
                                            std::size_t prefixLength,
                                            const std::uint8_t* payload,
                                            std::size_t payloadLength);
   private:
    bool tlsMode_ = false;
    bool rdmaMode_ = false;
    UniqueFd fd_;
    TlsConnection tls_;
    RdmaDataSocket rdma_;
};

[[nodiscard]] common::Result<FramedDataSocket> connectFramedDataSocket(
    const char* host, std::uint16_t port, DataTlsMode dataTlsMode, const TlsConfig& tlsConfig,
    const char* tlsServerName = nullptr, SocketDeadline deadline = {},
    std::string_view localBindAddress = {}, DataTransport transport = DataTransport::Tcp);

[[nodiscard]] common::Result<FramedDataSocket> acceptFramedDataSocket(
    UniqueFd fd, DataTlsMode dataTlsMode, const TlsConfig& tlsConfig);

}  // namespace cpnetflux::core::io
