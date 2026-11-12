#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void* mmap(void* address, std::size_t size, int protection, int flags, int fd, off_t offset) {
    using Function = void* (*)(void*, std::size_t, int, int, int, off_t);
    static const auto realMmap = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "mmap"));
    if (!realMmap) { errno = ENOSYS; return MAP_FAILED; }
    const char* root = std::getenv("CPNETFLUX_MAPPED_FAULT_ROOT");
    const char* mode = std::getenv("CPNETFLUX_MAPPED_FAULT");
    bool match = false;
    if (root && mode && fd >= 0 && (protection & PROT_WRITE)) {
        char descriptor[64], path[4096];
        std::snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", fd);
        const auto length = ::readlink(descriptor, path, sizeof(path) - 1);
        if (length >= 0) {
            path[length] = '\0';
            match = std::strncmp(path, root, std::strlen(root)) == 0;
        }
    }
    if (match && std::strcmp(mode, "fail") == 0) {
        constexpr char marker[] = "cpnetflux-test: injected mmap ENOMEM\n";
        (void)::write(STDERR_FILENO, marker, sizeof(marker) - 1);
        errno = ENOMEM; return MAP_FAILED;
    }
    void* mapped = realMmap(address, size, protection, flags, fd, offset);
    // Range zero has certified its first 64 MiB before entering its second window.
    if (mapped != MAP_FAILED && match && std::strcmp(mode, "truncate") == 0 &&
        offset == 64LL * 1024 * 1024 && ::ftruncate(fd, offset)) ::_exit(90);
    return mapped;
}
