// Same-live-mapping diagnostic of the production writer, not transfer throughput.
#include "cpnetflux/storage/mapped_file_window.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using cpnetflux::storage::MappedFileWindow;
using cpnetflux::storage::MappedWriteProfile;
constexpr std::uint64_t MiB = 1024ULL * 1024, GiB = 1024ULL * MiB;
constexpr std::uint64_t windowBytes = MappedFileWindow::kWindowBytes;
struct File {
    int fd = -1;
    std::string path;
    explicit File(const std::string& directory) {
        auto name = (std::filesystem::path(directory) / "cpnetflux-mapped-cost-XXXXXX").string();
        std::vector<char> buffer(name.begin(), name.end());
        buffer.push_back(0);
        fd = ::mkstemp(buffer.data());
        if (fd < 0) throw std::runtime_error("mkstemp");
        path = buffer.data();
    }
    ~File() { if (fd >= 0) ::close(fd); if (!path.empty()) ::unlink(path.c_str()); }
};
struct Worker {
    MappedWriteProfile cold, warm, total;
    std::uint64_t maps = 0, unmaps = 0, peak = 0;
    std::string error;
};
void addDelta(MappedWriteProfile& out, const MappedWriteProfile& a, const MappedWriteProfile& b) {
#define DELTA(field) out.field += b.field - a.field
    DELTA(copyCalls); DELTA(copyBytes); DELTA(errors);
    DELTA(copyMinorFaults); DELTA(copyMajorFaults);
    DELTA(mapCpuSeconds); DELTA(mapWallSeconds);
    DELTA(unmapCpuSeconds); DELTA(unmapWallSeconds);
    DELTA(copyCpuSeconds); DELTA(copyWallSeconds);
#undef DELTA
}
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void verify(int fd, std::uint64_t offset, std::uint64_t length,
            const std::vector<std::uint8_t>& expected, std::vector<std::uint8_t>& actual) {
    for (std::uint64_t done = 0; done < length; done += expected.size()) {
        const auto count = std::min<std::uint64_t>(expected.size(), length - done);
        std::size_t got = 0;
        while (got < count) {
            auto n = ::pread(fd, actual.data() + got, count - got, offset + done + got);
            if (n < 0 && errno == EINTR) continue;
            require(n > 0, "pread verification");
            got += n;
        }
        require(std::memcmp(actual.data(), expected.data(), count) == 0, "content mismatch");
    }
}
void jsonProfile(const MappedWriteProfile& p) {
    std::cout << "{\"copy_calls\":" << p.copyCalls << ",\"copy_bytes\":" << p.copyBytes
      << ",\"errors\":" << p.errors << ",\"copy_minor_faults\":" << p.copyMinorFaults
      << ",\"copy_major_faults\":" << p.copyMajorFaults
      << ",\"map_cpu_seconds\":" << p.mapCpuSeconds << ",\"map_wall_seconds\":" << p.mapWallSeconds
      << ",\"unmap_cpu_seconds\":" << p.unmapCpuSeconds << ",\"unmap_wall_seconds\":" << p.unmapWallSeconds
      << ",\"copy_cpu_seconds\":" << p.copyCpuSeconds << ",\"copy_wall_seconds\":" << p.copyWallSeconds << "}";
}
double seconds(const timeval& t) { return t.tv_sec + t.tv_usec * 1e-6; }
}

int main(int argc, char** argv) {
    try {
        std::string directory;
        std::uint64_t bytes = 128 * MiB;
        unsigned workers = 2;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            require(i + 1 < argc, "missing option value");
            const std::string value = argv[++i];
            std::size_t consumed = 0;
            if (arg == "--directory") directory = value;
            else if (arg == "--bytes") {
                bytes = std::stoull(value, &consumed);
                require(consumed == value.size(), "invalid bytes");
            } else if (arg == "--workers") {
                const auto count = std::stoull(value, &consumed);
                require(consumed == value.size() && count >= 1 && count <= 4, "workers must be 1..4");
                workers = count;
            } else throw std::runtime_error("unknown option");
        }
        require(!directory.empty() && std::filesystem::is_directory(directory), "directory required");
        require(bytes > 0 && bytes <= 8 * GiB && bytes % (workers * windowBytes) == 0,
                "bytes must be <=8GiB and divisible by workers*64MiB");
        rlimit limit{0, 0};
        require(::setrlimit(RLIMIT_CORE, &limit) == 0, "disable payload core dumps");
        File file(directory);
        require(::ftruncate(file.fd, bytes) == 0, "ftruncate");
        std::vector<Worker> results(workers);
        std::vector<std::thread> threads;
        rusage before{}, after{};
        require(::getrusage(RUSAGE_SELF, &before) == 0, "getrusage before");
        const auto started = std::chrono::steady_clock::now();
        try {
            for (unsigned id = 0; id < workers; ++id) threads.emplace_back([&, id] {
                auto& row = results[id];
                try {
                    std::vector<std::uint8_t> buffer(MiB), actual(MiB);
                    MappedFileWindow writer(file.fd, bytes, true);
                    const auto first = id * (bytes / workers), end = first + bytes / workers;
                    for (auto offset = first; offset < end; offset += windowBytes) {
                        for (unsigned warm = 0; warm < 2; ++warm) {
                            // Different patterns reveal failed rewrites; pread checks each phase.
                            const auto value = static_cast<std::uint8_t>(17 + id * 31 +
                                                (offset / windowBytes) % 97 + warm * 101);
                            std::fill(buffer.begin(), buffer.end(), value);
                            const auto prior = writer.stats().profile;
                            for (std::uint64_t chunk = 0; chunk < windowBytes; chunk += MiB) {
                                for (std::size_t done = 0; done < MiB;) {
                                    const auto count = std::min<std::size_t>(65532, MiB - done);
                                    require(writer.write(offset + chunk + done,
                                            std::span<const std::uint8_t>(buffer.data() + done, count)).isOk(),
                                            "mapped write");
                                    done += count;
                                }
                            }
                            addDelta(warm ? row.warm : row.cold, prior, writer.stats().profile);
                            verify(file.fd, offset, windowBytes, buffer, actual);
                        }
                        // Final unmap is accounted separately, after both measured writes.
                        require(writer.close().isOk(), "close mapping");
                    }
                    const auto& stats = writer.stats();
                    row.total = stats.profile;
                    row.maps = stats.mapCalls; row.unmaps = stats.unmapCalls; row.peak = stats.peakMappedBytes;
                    require(stats.activeMappedBytes == 0 && stats.mapFailures == 0 && row.total.errors == 0,
                            "mapping/profile lifecycle");
                } catch (const std::exception& error) { row.error = error.what(); }
            });
        } catch (...) { for (auto& thread : threads) thread.join(); throw; }
        for (auto& thread : threads) thread.join();
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        require(::getrusage(RUSAGE_SELF, &after) == 0, "getrusage after");
        MappedWriteProfile cold, warm, total, zero;
        std::uint64_t maps = 0, unmaps = 0, peak = 0;
        for (const auto& row : results) {
            if (!row.error.empty()) throw std::runtime_error(row.error);
            addDelta(cold, zero, row.cold); addDelta(warm, zero, row.warm); addDelta(total, zero, row.total);
            maps += row.maps; unmaps += row.unmaps; peak += row.peak;
        }
        // Remove only the mkstemp-owned file before reporting successful cleanup.
        require(::unlink(file.path.c_str()) == 0, "unlink owned payload");
        file.path.clear();
        std::cout.precision(12);
        std::cout << "{\"result\":\"pass\",\"schema\":\"cpnetflux_mapped_cost_bench_v1\","
          "\"scope\":\"diagnostic,not transfer; actual MappedFileWindow; cold sparse write then same-live-mapping overwrite;"
          " pread verifies both phases outside copy brackets; lifecycle unmap separately; profile overhead and verification charged to whole process\","
          "\"cpu_clock\":\"CLOCK_THREAD_CPUTIME_ID\",\"fault_scope\":\"RUSAGE_THREAD_copy_brackets\","
          "\"bytes_per_phase\":" << bytes << ",\"workers\":" << workers << ",\"map_calls\":" << maps
          << ",\"unmap_calls\":" << unmaps << ",\"sum_worker_peak_mapping_bytes\":" << peak
          << ",\"whole_diagnostic_wall_seconds\":" << elapsed
          << ",\"whole_process_cpu_seconds\":" << seconds(after.ru_utime) + seconds(after.ru_stime) - seconds(before.ru_utime) - seconds(before.ru_stime)
          << ",\"whole_process_minor_faults\":" << after.ru_minflt - before.ru_minflt
          << ",\"whole_process_major_faults\":" << after.ru_majflt - before.ru_majflt
          << ",\"verified_phases\":2,\"payload_removed\":true,\"cold\":";
        jsonProfile(cold); std::cout << ",\"warm\":"; jsonProfile(warm);
        std::cout << ",\"total\":"; jsonProfile(total); std::cout << "}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
