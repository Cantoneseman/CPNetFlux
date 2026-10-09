#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/socket_utils.h"

namespace cpnetflux::core::io {

enum class TlsMode {
    Off,
    Explicit,
    Required,
};

enum class DataTlsMode {
    Off,
    Required,
};

struct TlsConfig {
    TlsMode mode = TlsMode::Off;
    std::string certFile;
    std::string keyFile;
    std::string caFile;
};

[[nodiscard]] common::Result<TlsMode> parseTlsMode(std::string_view value);
[[nodiscard]] const char* tlsModeName(TlsMode mode) noexcept;
[[nodiscard]] common::Result<DataTlsMode> parseDataTlsMode(std::string_view value);
[[nodiscard]] const char* dataTlsModeName(DataTlsMode mode) noexcept;
[[nodiscard]] common::Status validateTlsServerConfig(const TlsConfig& config);
[[nodiscard]] common::Status validateTlsClientConfig(const TlsConfig& config);
[[nodiscard]] common::Status validateDataTlsServerConfig(TlsMode controlMode,
                                                         DataTlsMode dataMode,
                                                         const TlsConfig& tlsConfig);
[[nodiscard]] common::Status validateDataTlsClientConfig(DataTlsMode dataMode,
                                                         const TlsConfig& tlsConfig);
[[nodiscard]] bool tlsSupportAvailable() noexcept;

class TlsConnection {
   public:
    TlsConnection();
    ~TlsConnection();

    TlsConnection(const TlsConnection&) = delete;
    TlsConnection& operator=(const TlsConnection&) = delete;

    TlsConnection(TlsConnection&& other) noexcept;
    TlsConnection& operator=(TlsConnection&& other) noexcept;

    [[nodiscard]] static TlsConnection plain(UniqueFd fd);

    [[nodiscard]] int fd() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool tlsEnabled() const noexcept;
    [[nodiscard]] bool hasPendingRead() const noexcept;

    [[nodiscard]] common::Status writeAll(const char* data, std::size_t size);
    [[nodiscard]] common::Result<std::size_t> readSome(char* data, std::size_t size);
    [[nodiscard]] common::Result<std::size_t> readSomeNonBlocking(char* data, std::size_t size);

   private:
    friend class TlsServerContext;
    friend class TlsClientContext;
    struct Impl;

    explicit TlsConnection(UniqueFd fd);
    TlsConnection(UniqueFd fd, std::unique_ptr<Impl> impl);

    UniqueFd fd_;
    std::unique_ptr<Impl> impl_;
};

class TlsServerContext {
   public:
    TlsServerContext();
    ~TlsServerContext();

    TlsServerContext(const TlsServerContext&) = delete;
    TlsServerContext& operator=(const TlsServerContext&) = delete;

    TlsServerContext(TlsServerContext&& other) noexcept;
    TlsServerContext& operator=(TlsServerContext&& other) noexcept;

    [[nodiscard]] static common::Result<TlsServerContext> create(const TlsConfig& config);
    [[nodiscard]] common::Result<TlsConnection> accept(UniqueFd fd) const;

   private:
    struct Impl;
    explicit TlsServerContext(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

class TlsClientContext {
   public:
    TlsClientContext();
    ~TlsClientContext();

    TlsClientContext(const TlsClientContext&) = delete;
    TlsClientContext& operator=(const TlsClientContext&) = delete;

    TlsClientContext(TlsClientContext&& other) noexcept;
    TlsClientContext& operator=(TlsClientContext&& other) noexcept;

    [[nodiscard]] static common::Result<TlsClientContext> create(const TlsConfig& config);
    [[nodiscard]] common::Result<TlsConnection> connect(UniqueFd fd, const std::string& host) const;

   private:
    struct Impl;
    explicit TlsClientContext(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace cpnetflux::core::io
