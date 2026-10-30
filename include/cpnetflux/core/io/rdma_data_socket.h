#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/socket_utils.h"

namespace cpnetflux::core::io {

[[nodiscard]] bool rdmaTransportAvailable() noexcept;
inline constexpr std::uint32_t kRdmaDataQueueDepth = 32;
inline constexpr std::uint32_t kRdmaDataMessageBufferBytes = 128U << 10;

struct RdmaTransportStats {
    std::uint64_t sendBytes = 0;
    std::uint64_t sendWorkRequests = 0;
    std::uint64_t sendCompletions = 0;
    std::uint64_t sendCompletionPollCalls = 0;
    std::uint64_t sendCompletionBatches = 0;
    std::uint64_t sendCompletionWaitCount = 0;
    std::uint64_t sendCompletionWaitNanoseconds = 0;
    std::uint64_t maxSendInFlight = 0;
};

class RdmaDataSocket {
   public:
    RdmaDataSocket();
    ~RdmaDataSocket();

    RdmaDataSocket(const RdmaDataSocket&) = delete;
    RdmaDataSocket& operator=(const RdmaDataSocket&) = delete;
    RdmaDataSocket(RdmaDataSocket&&) noexcept;
    RdmaDataSocket& operator=(RdmaDataSocket&&) noexcept;

    [[nodiscard]] static common::Result<RdmaDataSocket> connect(
        const char* host, std::uint16_t port, SocketDeadline deadline = {},
        std::string_view localBindAddress = {});
    [[nodiscard]] bool valid() const noexcept;
    void shutdown() noexcept;
    [[nodiscard]] int fd() const noexcept { return -1; }
    [[nodiscard]] common::Status writeAll(const std::uint8_t* data, std::size_t length);
    [[nodiscard]] common::Status writeFrame(const std::uint8_t* header,
                                            std::size_t headerLength,
                                            const std::uint8_t* prefix,
                                            std::size_t prefixLength,
                                            const std::uint8_t* payload,
                                            std::size_t payloadLength);
    [[nodiscard]] common::Result<std::size_t> readSome(std::uint8_t* data, std::size_t length);
    [[nodiscard]] common::Status readAll(std::uint8_t* data, std::size_t length);
    [[nodiscard]] RdmaTransportStats stats() const noexcept;
    void close() noexcept;

   private:
    struct Impl;
    explicit RdmaDataSocket(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;

    friend class RdmaListener;
};

class RdmaListener {
   public:
    RdmaListener();
    ~RdmaListener();

    RdmaListener(const RdmaListener&) = delete;
    RdmaListener& operator=(const RdmaListener&) = delete;
    RdmaListener(RdmaListener&&) noexcept;
    RdmaListener& operator=(RdmaListener&&) noexcept;

    [[nodiscard]] static common::Result<RdmaListener> listen(
        const char* host, std::uint16_t port, int backlog);
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] common::Result<RdmaDataSocket> accept(SocketDeadline deadline = {});
    void close() noexcept;

   private:
    struct Impl;
    explicit RdmaListener(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cpnetflux::core::io
