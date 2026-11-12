// Synchronous source preparation only, not a queue/socket or transfer benchmark.
#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/storage/file_io.h"
#include <algorithm>
#include <barrier>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using cpnetflux::checksum::ChecksumAlgorithm;
using cpnetflux::checksum::ChecksumComputer;
using cpnetflux::storage::PosixFile;
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t MiB = 1024ULL * 1024, windowBytes = 64 * MiB;
constexpr std::size_t frameBytes = 65532;
void require(bool b, const std::string& s) { if (!b) throw std::runtime_error(s); }
void require(bool b, const char* s) { if (!b) throw std::runtime_error(s); }
double seconds(const timeval& t) { return t.tv_sec + t.tv_usec * 1e-6; }
double cpu(const rusage& u) { return seconds(u.ru_utime) + seconds(u.ru_stime); }
rusage usage(int who) {
    rusage u{}; require(::getrusage(who, &u) == 0, "getrusage"); return u;
}
struct Sample {
    double wall = 0, cpuSeconds = 0;
    std::uint64_t minor = 0, major = 0;
    void finish(Clock::time_point start, const rusage& a, const rusage& b) {
        wall = std::chrono::duration<double>(Clock::now() - start).count();
        cpuSeconds = cpu(b) - cpu(a); minor = b.ru_minflt - a.ru_minflt; major = b.ru_majflt - a.ru_majflt;
    }
    void json() const {
        std::cout << "{\"wall_seconds\":" << wall << ",\"cpu_seconds\":" << cpuSeconds
                  << ",\"minor_faults\":" << minor << ",\"major_faults\":" << major << "}";
    }
};
std::uint64_t number(const std::string& s) {
    std::uint64_t n = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    require(!s.empty() && r.ec == std::errc{} && r.ptr == s.data() + s.size(), "invalid unsigned integer");
    return n;
}
PosixFile openSource(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    require(fd >= 0, "open source errno=" + std::to_string(errno));
    return PosixFile(cpnetflux::core::io::UniqueFd(fd));
}
struct Identity {
    struct stat value{};
    explicit Identity(int fd) {
        require(::fstat(fd, &value) == 0 && S_ISREG(value.st_mode), "source must be regular file");
        require(value.st_size > 0 && value.st_size <= 1024LL * MiB, "source size must be 1..1GiB");
    }
    bool same(const Identity& other) const {
        const auto& a = value; const auto& b = other.value;
        return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
            a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
            a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
    }
    void check(int fd, const std::string& path) const {
        require(same(Identity(fd)), "source changed on descriptor");
        // Reopen with NOFOLLOW so replacement/symlink changes are also rejected.
        auto current = openSource(path);
        require(same(Identity(current.fd())), "source changed at path");
    }
};
// Diagnostic borrow is synchronous and must end before close/remap/destruction.
// There is no in-flight queue pin; do not reuse this as an asynchronous payload.
class SourceWindow {
    int fd_;
    std::uint64_t fileSize_, base_ = 0, length_ = 0;
    void* mapping_ = MAP_FAILED;
 public:
    std::uint64_t maps = 0, unmaps = 0, peak = 0;
    SourceWindow(int fd, std::uint64_t size) : fd_(fd), fileSize_(size) {}
    SourceWindow(const SourceWindow&) = delete;
    ~SourceWindow() { if (mapping_ != MAP_FAILED) (void)::munmap(mapping_, length_); }
    void close() {
        if (mapping_ == MAP_FAILED) return;
        require(::munmap(mapping_, length_) == 0, "source munmap errno=" + std::to_string(errno));
        mapping_ = MAP_FAILED; length_ = 0; ++unmaps;
    }
    std::span<const std::uint8_t> borrow(std::uint64_t offset, std::size_t requested) {
        require(requested && offset < fileSize_ && requested <= fileSize_ - offset, "source borrow out of range");
        if (mapping_ == MAP_FAILED || offset < base_ || offset - base_ >= length_) {
            close();
            base_ = offset / windowBytes * windowBytes;
            length_ = std::min(windowBytes, fileSize_ - base_);
            mapping_ = ::mmap(nullptr, length_, PROT_READ, MAP_SHARED, fd_, static_cast<off_t>(base_));
            require(mapping_ != MAP_FAILED, "source mmap errno=" + std::to_string(errno));
            ++maps; peak = std::max(peak, length_);
        }
        const auto count = std::min<std::uint64_t>(requested, length_ - (offset - base_));
        return {static_cast<const std::uint8_t*>(mapping_) + offset - base_, static_cast<std::size_t>(count)};
    }
};
struct Range {
    std::uint64_t begin = 0, end = 0, frames = 0, reads = 0, readBytes = 0, maps = 0, unmaps = 0, peak = 0;
    std::uint32_t expected = 0, actual = 0;
    Sample work;
    std::string error;
};
}

int main(int argc, char** argv) {
    try {
        std::string source, mode;
        unsigned workers = 1;
        std::uint64_t cancelAfter = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i]; require(i + 1 < argc, "missing option value");
            const std::string value = argv[++i];
            if (arg == "--source") source = value;
            else if (arg == "--mode") mode = value;
            else if (arg == "--workers") {
                auto n = number(value); require(n >= 1 && n <= 4, "workers must be 1..4"); workers = n;
            } else if (arg == "--cancel-after-frames") {
                cancelAfter = number(value); require(cancelAfter > 0, "cancel-after-frames must be positive");
            } else throw std::runtime_error("unknown option");
        }
        require(!source.empty() && (mode == "pread" || mode == "mmap"), "source and mode pread|mmap required");
        rlimit limit{0, 0}; require(::setrlimit(RLIMIT_CORE, &limit) == 0, "disable core dumps");
        const auto setupUsage = usage(RUSAGE_SELF); const auto setupStart = Clock::now();
        auto reference = openSource(source);
        const Identity identity(reference.fd());
        const auto bytes = static_cast<std::uint64_t>(identity.value.st_size);
        const auto chunks = (bytes + MiB - 1) / MiB;
        require(chunks >= workers, "each worker needs a nonempty chunk range");
        std::vector<Range> rows(workers);
        std::uint64_t firstChunk = 0;
        std::vector<std::uint8_t> check(MiB);
        for (unsigned i = 0; i < workers; ++i) {
            auto& row = rows[i];
            const auto count = chunks / workers + (i < chunks % workers ? 1 : 0);
            row.begin = firstChunk * MiB; firstChunk += count; row.end = std::min(firstChunk * MiB, bytes);
            ChecksumComputer crc(ChecksumAlgorithm::Crc32c);
            for (auto offset = row.begin; offset < row.end;) {
                const auto length = std::min<std::uint64_t>(MiB, row.end - offset);
                auto status = reference.readAtAll(offset, check.data(), length);
                require(status.isOk(), "reference read: " + status.message());
                crc.update(check.data(), length); offset += length;
            }
            row.expected = crc.finalize().value;
        }
        identity.check(reference.fd(), source);
        reference = PosixFile{};
        Sample setup; setup.finish(setupStart, setupUsage, usage(RUSAGE_SELF));

        const auto serviceUsage = usage(RUSAGE_SELF); const auto serviceStart = Clock::now();
        auto file = openSource(source); require(identity.same(Identity(file.fd())), "source changed before service");
        // All consumers start together after buffers are ready. Construction and
        // synchronization are included in service, but excluded from worker sums.
        std::barrier ready(static_cast<std::ptrdiff_t>(workers + 1));
        std::vector<std::thread> threads;
        try {
            for (unsigned i = 0; i < workers; ++i) threads.emplace_back([&, i] {
                auto& row = rows[i];
                std::vector<std::uint8_t> buffer;
                try { if (mode == "pread") buffer.reserve(frameBytes); }
                catch (const std::exception& e) { row.error = e.what(); }
                ready.arrive_and_wait();
                if (!row.error.empty()) return;
                try {
                    const auto before = usage(RUSAGE_THREAD); const auto start = Clock::now();
                    SourceWindow mapping(file.fd(), bytes);
                    cpnetflux::storage::FileIoStats stats;
                    cpnetflux::storage::FileIoContext context;
                    ChecksumComputer rangeCrc(ChecksumAlgorithm::Crc32c);
                    for (auto offset = row.begin; offset < row.end;) {
                        require(!cancelAfter || row.frames < cancelAfter, "source diagnostic cancelled");
                        const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(frameBytes,
                            std::min<std::uint64_t>(row.end - offset, MiB - offset % MiB)));
                        std::span<const std::uint8_t> data;
                        if (mode == "pread") {
                            buffer.resize(length);
                            auto status = cpnetflux::storage::readAtAll(file, offset, buffer.data(), length, context, &stats);
                            require(status.isOk(), "source read: " + status.message()); data = buffer;
                        } else data = mapping.borrow(offset, length);
                        require(data.size() == length, "frame unexpectedly crosses source window");
                        ChecksumComputer frame(ChecksumAlgorithm::Crc32c);
                        frame.update(data.data(), data.size());
                        require(rangeCrc.combine(frame.finalize(), length), "range CRC combine");
                        offset += length; ++row.frames;
                    }
                    row.actual = rangeCrc.finalize().value;
                    require(row.actual == row.expected, "source range CRC mismatch");
                    mapping.close(); row.maps = mapping.maps; row.unmaps = mapping.unmaps; row.peak = mapping.peak;
                    require(row.maps == row.unmaps, "source mapping lifecycle mismatch");
                    row.reads = stats.readCalls(); row.readBytes = stats.readBytes();
                    row.work.finish(start, before, usage(RUSAGE_THREAD));
                } catch (const std::exception& e) { row.error = e.what(); }
            });
        } catch (...) {
            for (auto n = threads.size(); n < workers; ++n) ready.arrive_and_drop();
            ready.arrive_and_drop(); for (auto& thread : threads) thread.join(); throw;
        }
        ready.arrive_and_wait();
        for (auto& thread : threads) thread.join();
        identity.check(file.fd(), source); file = PosixFile{};
        Sample service; service.finish(serviceStart, serviceUsage, usage(RUSAGE_SELF));
        std::uint64_t frames = 0, reads = 0, readBytes = 0, maps = 0, unmaps = 0, peak = 0;
        double workerWall = 0, workerCpu = 0;
        std::uint64_t faults = 0, major = 0;
        for (const auto& row : rows) {
            require(row.error.empty(), row.error);
            frames += row.frames; reads += row.reads; readBytes += row.readBytes;
            maps += row.maps; unmaps += row.unmaps; peak += row.peak;
            workerWall += row.work.wall; workerCpu += row.work.cpuSeconds;
            faults += row.work.minor; major += row.work.major;
        }
        std::cout.precision(12);
        std::cout << "{\"result\":\"pass\",\"schema\":\"cpnetflux_source_read_v1\",\"diagnostic_only\":true,"
            "\"network_io\":false,\"async_queue\":false,\"source_modified\":false,\"source_removed\":false,"
            "\"scope\":\"resident immutable source; synchronous frame CRC plus range combine, no transport; setup reference CRC scan outside service; service includes open/identity, thread/buffer creation, synchronization, read or mmap/first PTE, CRC/combine, unmap, result checks, final source identity and close; source generation/deletion external\","
            "\"mode\":\"" << mode << "\",\"workers\":" << workers << ",\"bytes\":" << bytes
            << ",\"frame_bytes\":" << frameBytes << ",\"chunk_bytes\":" << MiB << ",\"frames\":" << frames
            << ",\"read_at_all_calls\":" << reads << ",\"read_at_all_bytes\":" << readBytes
            << ",\"logical_source_to_buffer_copy_bytes\":" << readBytes
            << ",\"crc_scan_bytes\":" << bytes << ",\"combine_calls\":" << frames
            << ",\"map_calls\":" << maps << ",\"unmap_calls\":" << unmaps
            << ",\"sum_worker_peak_mapping_bytes\":" << peak
            << ",\"setup\":"; setup.json(); std::cout << ",\"service\":"; service.json();
        std::cout << ",\"worker_wall_seconds_sum\":" << workerWall << ",\"worker_cpu_seconds_sum\":" << workerCpu
            << ",\"worker_minor_faults\":" << faults << ",\"worker_major_faults\":" << major << ",\"ranges\":[";
        for (unsigned i = 0; i < workers; ++i) {
            if (i) std::cout << ','; const auto& row = rows[i];
            std::cout << "{\"begin\":" << row.begin << ",\"end\":" << row.end << ",\"crc32c\":" << row.actual
                      << ",\"expected_crc32c\":" << row.expected << ",\"frames\":" << row.frames << "}";
        }
        std::cout << "]}\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
