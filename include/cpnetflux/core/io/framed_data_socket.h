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

struct TcpExactReadStats {
    bool enabled = false;
    std::uint64_t largeReadCalls = 0, recvCalls = 0, shortReturns = 0;
    std::uint64_t bytes = 0, interruptions = 0;
};

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
    [[nodiscard]] RdmaTransportStats rdmaStats() const noexcept;
    // Install the control-negotiated V3 limit once, before handing off to IO.
    [[nodiscard]] common::Status configureDataFrameBytes(std::uint32_t logicalBytes);
    [[nodiscard]] std::uint32_t dataPayloadLimit() const noexcept { return dataPayloadLimit_; }
    [[nodiscard]] common::Status configureTcpReceiveBuffer(std::uint32_t requestedBytes);
    [[nodiscard]] std::uint32_t tcpReceiveBufferBytes() const noexcept { return tcpReceiveBufferBytes_; }
    // Explicit plain TCP/256KiB DATA experiment. Small reads keep readSome semantics.
    [[nodiscard]] common::Status configureTcpExactRead(bool enabled);
    [[nodiscard]] TcpExactReadStats tcpExactReadStats() const noexcept { return tcpExactRead_; }
    void shutdown() noexcept;
    void close() noexcept;
    [[nodiscard]] common::Status writeAll(const std::uint8_t* data, std::size_t length);
    [[nodiscard]] common::Status writeFrame(const std::uint8_t* header,
                                            std::size_t headerLength,
                                            const std::uint8_t* payload,
                                            std::size_t payloadLength);
    [[nodiscard]] common::Result<std::size_t> readSome(std::uint8_t* data, std::size_t length);
    [[nodiscard]] common::Status readAll(std::uint8_t* data, std::size_t length);
    // Plain TCP only. Both destinations remain caller-owned until completion.
    // Partial reads may mutate either span on failure; callers must not certify it.
    [[nodiscard]] common::Status readAllV(std::uint8_t* prefix, std::size_t prefixLength,
                                         std::uint8_t* payload, std::size_t payloadLength);

    [[nodiscard]] common::Status writeFrame(const std::uint8_t* header,
                                            std::size_t headerLength,
                                            const std::uint8_t* prefix,
                                            std::size_t prefixLength,
                                            const std::uint8_t* payload,
                                            std::size_t payloadLength);
    [[nodiscard]] common::Status writeFrameBuffered(const std::uint8_t* header,
                                                    std::size_t headerLength,
                                                    const std::uint8_t* prefix,
                                                    std::size_t prefixLength,
                                                    const std::uint8_t* payload,
                                                    std::size_t payloadLength);
   private:
    std::uint32_t dataPayloadLimit_ = 65536;
    bool dataLimitConfigured_ = false;
    std::uint32_t tcpReceiveBufferBytes_ = 0;
    TcpExactReadStats tcpExactRead_;
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
