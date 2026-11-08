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
    const char* mode = std::getenv("CPNETFLUX_OFFSET_WRITE_FAULT");
    const char* target = std::getenv("CPNETFLUX_OFFSET_WRITE_FAULT_FILE");
    bool match = false;
    if (mode && target && fd >= 0) {
        char descriptor[64], path[4096];
        const int count = std::snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", fd);
        if (count > 0 && static_cast<std::size_t>(count) < sizeof(descriptor)) {
            const auto length = ::readlink(descriptor, path, sizeof(path) - 1);
            if (length >= 0) {
                path[length] = '\0';
                match = std::strcmp(path, target) == 0;
            }
        }
    }
    if (match && std::strcmp(mode, "fail") == 0) { errno = ENOMEM; return MAP_FAILED; }
    void* mapped = realMmap(address, size, protection, flags, fd, offset);
    // Truncating after mmap makes the first real write fault beyond EOF with SIGBUS.
    if (mapped != MAP_FAILED && match && std::strcmp(mode, "truncate") == 0 && ::ftruncate(fd, 0))
        ::_exit(90);
    return mapped;
}
