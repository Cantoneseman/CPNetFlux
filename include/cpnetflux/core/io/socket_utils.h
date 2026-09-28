#pragma once

#include <cstdint>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::io {

class UniqueFd {
   public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) noexcept;
    ~UniqueFd();

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept;
    UniqueFd& operator=(UniqueFd&& other) noexcept;

    [[nodiscard]] int get() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;

    int release() noexcept;
    void reset(int fd = -1) noexcept;

   private:
    int fd_ = -1;
};

common::Status setNonBlocking(int fd);
common::Result<UniqueFd> createListener(const char* host, std::uint16_t port, int backlog);
common::Result<UniqueFd> createNonBlockingConnection(const char* host, std::uint16_t port);
common::Result<UniqueFd> createEpoll();
common::Status addEpollFd(int epollFd, int fd, std::uint32_t events, void* data);
common::Status modifyEpollFd(int epollFd, int fd, std::uint32_t events, void* data);
common::Status deleteEpollFd(int epollFd, int fd);
common::Result<int> getSocketError(int fd);

}  // namespace cpnetflux::core::io
