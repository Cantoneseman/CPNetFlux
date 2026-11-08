#include "cpnetflux/storage/mapped_file_window.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#ifdef __linux__
#include <sys/mman.h>
#endif

namespace cpnetflux::storage {
MappedFileWindow::~MappedFileWindow() { (void)close(); }

common::Status MappedFileWindow::close() {
#ifdef __linux__
    if (mapped_) {
        if (::munmap(mapping_, stats_.activeMappedBytes) != 0)
            return common::Status::systemError("experimental download write munmap", errno);
        ++stats_.unmapCalls;
        mapped_ = false;
        mapping_ = nullptr;
        stats_.activeMappedBytes = 0;
    }
#endif
    return common::Status::ok();
}

common::Status MappedFileWindow::write(std::uint64_t offset, std::span<const std::uint8_t> bytes) {
    if (offset > fileSize_ || bytes.size() > fileSize_ - offset)
        return common::Status::invalidArgument("experimental download write exceeds file boundary");
#ifndef __linux__
    return common::Status::invalidArgument("experimental download write requires Linux");
#else
    ++stats_.writeCalls;
    while (!bytes.empty()) {
        if (!mapped_ || offset < base_ || offset - base_ >= stats_.activeMappedBytes) {
            const auto retired = close();
            if (!retired.isOk()) return retired;
            base_ = offset / kWindowBytes * kWindowBytes;
            const auto length = std::min(kWindowBytes, fileSize_ - base_);
            ++stats_.mapCalls;
            void* mapping = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                   fd_, static_cast<off_t>(base_));
            if (mapping == MAP_FAILED) {
                ++stats_.mapFailures;
                return common::Status::systemError("experimental download write mmap", errno);
            }
            mapping_ = mapping;
            mapped_ = true;
            stats_.activeMappedBytes = length;
            stats_.peakMappedBytes = std::max(stats_.peakMappedBytes, length);
        }
        const auto count = std::min<std::uint64_t>(bytes.size(), stats_.activeMappedBytes - (offset - base_));
        // SIGBUS is fatal to the isolated opt-in CLI; no shared-process recovery.
        std::memcpy(static_cast<std::uint8_t*>(mapping_) + (offset - base_), bytes.data(), count);
        stats_.writtenBytes += count;
        offset += count;
        bytes = bytes.subspan(count);
    }
    return common::Status::ok();
#endif
}
}  // namespace cpnetflux::storage
