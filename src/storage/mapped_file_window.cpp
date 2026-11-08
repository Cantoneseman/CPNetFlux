#include "cpnetflux/storage/mapped_file_window.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#ifdef __linux__
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#endif

namespace cpnetflux::storage {
#ifdef __linux__
namespace {
using Clock = std::chrono::steady_clock;

double threadCpu(MappedWriteProfile& profile) {
    timespec value{};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) {
        ++profile.errors;
        return -1;
    }
    return value.tv_sec + value.tv_nsec * 1e-9;
}

class Phase {
 public:
    Phase(MappedWriteProfile& profile, double& wall, double& cpu)
        : profile_(profile), wall_(wall), cpu_(cpu) {
        if (profile_.enabled) {
            started_ = Clock::now();
            cpuStarted_ = threadCpu(profile_);
        }
    }
    ~Phase() {
        if (!profile_.enabled) return;
        const int saved = errno;
        const auto ended = threadCpu(profile_);
        wall_ += std::chrono::duration<double>(Clock::now() - started_).count();
        if (cpuStarted_ >= 0 && ended >= cpuStarted_) cpu_ += ended - cpuStarted_;
        else if (ended >= 0) ++profile_.errors;
        errno = saved;
    }
 private:
    MappedWriteProfile& profile_;
    double &wall_, &cpu_;
    Clock::time_point started_{};
    double cpuStarted_ = -1;
};

void profiledCopy(MappedWriteProfile& profile, void* destination,
                  const void* source, std::size_t count) {
    rusage before{}, after{};
    ++profile.copyCalls;
    profile.copyBytes += count;
    const bool beforeValid = ::getrusage(RUSAGE_THREAD, &before) == 0;
    if (!beforeValid) ++profile.errors;
    {
        Phase phase(profile, profile.copyWallSeconds, profile.copyCpuSeconds);
        std::memcpy(destination, source, count);
    }
    if (::getrusage(RUSAGE_THREAD, &after) != 0) ++profile.errors;
    else if (beforeValid) {
        if (after.ru_minflt < before.ru_minflt || after.ru_majflt < before.ru_majflt)
            ++profile.errors;
        else {
            profile.copyMinorFaults += after.ru_minflt - before.ru_minflt;
            profile.copyMajorFaults += after.ru_majflt - before.ru_majflt;
        }
    }
}
}
#endif

MappedFileWindow::~MappedFileWindow() { (void)close(); }

common::Status MappedFileWindow::close() {
#ifdef __linux__
    if (mapped_) {
        int result;
        {
            Phase phase(stats_.profile, stats_.profile.unmapWallSeconds, stats_.profile.unmapCpuSeconds);
            result = ::munmap(mapping_, stats_.activeMappedBytes);
        }
        if (result != 0)
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
            void* mapping;
            {
                Phase phase(stats_.profile, stats_.profile.mapWallSeconds, stats_.profile.mapCpuSeconds);
                mapping = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                 fd_, static_cast<off_t>(base_));
            }
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
        auto& profile = stats_.profile;
        // SIGBUS is fatal to the isolated opt-in CLI; no shared-process recovery.
        auto* destination = static_cast<std::uint8_t*>(mapping_) + (offset - base_);
        if (profile.enabled) profiledCopy(profile, destination, bytes.data(), count);
        else std::memcpy(destination, bytes.data(), count);
        stats_.writtenBytes += count;
        offset += count;
        bytes = bytes.subspan(count);
    }
    return common::Status::ok();
#endif
}
}  // namespace cpnetflux::storage
