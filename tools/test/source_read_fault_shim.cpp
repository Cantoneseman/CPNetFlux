// LD_PRELOAD shim restricted to the smoke's exact owned source path.
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
std::atomic<unsigned> reads{0}, maps{0}, unmaps{0}, retries{0}, changes{0};
std::atomic<std::uintptr_t> addresses[128]{};
const char* mode() { const auto* p = std::getenv("CPNETFLUX_SOURCE_READ_FAULT"); return p ? p : "observe"; }
bool target(int fd) {
    const auto* path = std::getenv("CPNETFLUX_SOURCE_READ_FAULT_SOURCE");
    if (!path || fd < 0) return false;
    char name[64], resolved[4096];
    std::snprintf(name, sizeof(name), "/proc/self/fd/%d", fd);
    const auto n = ::readlink(name, resolved, sizeof(resolved) - 1);
    if (n < 0) return false;
    resolved[n] = 0; return std::strcmp(path, resolved) == 0;
}
void change(int fd) {
    if (std::strcmp(mode(), "change") || changes.fetch_add(1)) return;
    struct stat s{};
    if (::fstat(fd, &s)) ::_exit(92);
    timespec times[2]{{0, UTIME_OMIT}, {s.st_mtim.tv_sec + 1, s.st_mtim.tv_nsec}};
    if (::futimens(fd, times)) ::_exit(93);
    std::fputs("cpnetflux-test: source timestamp changed\n", stderr);
}
}

extern "C" ssize_t pread(int fd, void* data, size_t size, off_t offset) {
    using Function = ssize_t (*)(int, void*, size_t, off_t);
    static auto real = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "pread"));
    if (!real) { errno = ENOSYS; return -1; }
    // Reference scans use 1MiB reads (fixture tail is 123 bytes). Only frame-sized
    // service reads trigger faults, keeping the oracle outside fault injection.
    if (target(fd) && size == 65532) {
        ++reads; change(fd);
        if (!std::strcmp(mode(), "read-fail")) { errno = EIO; return -1; }
        if (!std::strcmp(mode(), "read-corrupt")) {
            const auto n = real(fd, data, size, offset);
            if (n > 0) static_cast<unsigned char*>(data)[0] ^= 1;
            return n;
        }
        if (!std::strcmp(mode(), "read-retry")) {
            const auto step = retries.fetch_add(1);
            if (!step) { errno = EINTR; return -1; }
            if (step == 1) return real(fd, data, size / 2, offset);
        }
    }
    return real(fd, data, size, offset);
}
extern "C" void* mmap(void* address, size_t size, int protection, int flags, int fd, off_t offset) {
    using Function = void* (*)(void*, size_t, int, int, int, off_t);
    static auto real = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "mmap"));
    if (!real) { errno = ENOSYS; return MAP_FAILED; }
    const bool owned = target(fd);
    if (owned) {
        if (protection != PROT_READ || flags != MAP_SHARED || size > 64ULL * 1024 * 1024) {
            std::fputs("cpnetflux-test: invalid source mapping protection/bound\n", stderr);
            errno = EINVAL; return MAP_FAILED;
        }
        if (!std::strcmp(mode(), "map-fail")) { errno = ENOMEM; return MAP_FAILED; }
        change(fd);
    }
    auto* p = real(address, size, protection, flags, fd, offset);
    if (owned && p != MAP_FAILED) {
        ++maps;
        bool inserted = false;
        for (auto& entry : addresses) {
            std::uintptr_t expected = 0;
            if (entry.compare_exchange_strong(expected, reinterpret_cast<std::uintptr_t>(p))) { inserted = true; break; }
        }
        if (!inserted) ::_exit(94);
    }
    return p;
}
extern "C" int munmap(void* address, size_t size) {
    using Function = int (*)(void*, size_t);
    static auto real = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "munmap"));
    if (!real) { errno = ENOSYS; return -1; }
    const auto result = real(address, size);
    if (!result) for (auto& entry : addresses) {
        auto expected = reinterpret_cast<std::uintptr_t>(address);
        if (entry.compare_exchange_strong(expected, 0)) { ++unmaps; break; }
    }
    return result;
}
__attribute__((destructor)) static void report() {
    if (!std::getenv("CPNETFLUX_SOURCE_READ_FAULT_SOURCE")) return;
    std::fprintf(stderr, "cpnetflux-source-shim: maps=%u unmaps=%u reads=%u retries=%u changes=%u\n",
                 maps.load(), unmaps.load(), reads.load(), retries.load(), changes.load());
}
