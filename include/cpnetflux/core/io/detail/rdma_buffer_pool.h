#pragma once

#include "cpnetflux/common/status.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>

namespace cpnetflux::core::io::detail {

// Connection-owned storage. The MR must be deregistered before this owner dies.
// Alignment and a best-effort hint permit THP without reserving system hugepages.
// All logical bytes are still zeroed before registration on either page path.
class RdmaBufferPool {
public:
    RdmaBufferPool() = default;
    ~RdmaBufferPool() {
        if (mapping_ != MAP_FAILED) (void)::munmap(mapping_, mappingBytes_);
    }
    RdmaBufferPool(const RdmaBufferPool&) = delete;
    RdmaBufferPool& operator=(const RdmaBufferPool&) = delete;
    RdmaBufferPool(RdmaBufferPool&&) = delete;
    RdmaBufferPool& operator=(RdmaBufferPool&&) = delete;

    common::Status allocate(std::size_t bytes, bool preferHugePages = true) {
        if (data_ != nullptr || bytes == 0)
            return common::Status::invalidArgument("invalid RDMA buffer pool allocation");
        const long pageSize = ::sysconf(_SC_PAGESIZE);
        if (pageSize <= 0 || kAlignment % static_cast<std::size_t>(pageSize) != 0)
            return common::Status::runtimeError("unsupported RDMA buffer pool page size");
        const auto page = static_cast<std::size_t>(pageSize);
        constexpr auto maximum = std::numeric_limits<std::size_t>::max();
        if (bytes > maximum - (page - 1))
            return common::Status::invalidArgument("RDMA buffer pool size overflow");
        const auto rounded = ((bytes + page - 1) / page) * page;
        if (rounded > maximum - kAlignment)
            return common::Status::invalidArgument("RDMA buffer pool mapping overflow");
        const auto reserved = rounded + kAlignment;
        void* mapping = ::mmap(nullptr, reserved, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED)
            return common::Status::systemError("mmap RDMA buffer pool failed", errno);
        mapping_ = mapping;
        mappingBytes_ = reserved;
        const auto base = reinterpret_cast<std::uintptr_t>(mapping);
        const auto aligned = (base + kAlignment - 1) & ~(kAlignment - 1);
        data_ = reinterpret_cast<std::uint8_t*>(aligned);
        size_ = bytes;
        if (preferHugePages) {
            hugePageHintAccepted_ = ::madvise(data_, rounded, MADV_HUGEPAGE) == 0;
            if (!hugePageHintAccepted_) hugePageHintError_ = errno;
        }
        // Touch only the logical pool; alignment padding remains uncommitted.
        std::memset(data_, 0, size_);
        return common::Status::ok();
    }

    std::uint8_t* data() noexcept { return data_; }
    const std::uint8_t* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    std::size_t mappingBytes() const noexcept { return mappingBytes_; }
    bool hugePageHintAccepted() const noexcept { return hugePageHintAccepted_; }
    int hugePageHintError() const noexcept { return hugePageHintError_; }

private:
    static constexpr std::uintptr_t kAlignment = 2U << 20;
    void* mapping_ = MAP_FAILED;
    std::size_t mappingBytes_ = 0;
    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    bool hugePageHintAccepted_ = false;
    int hugePageHintError_ = 0;
};

}  // namespace cpnetflux::core::io::detail
