#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <latch>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "cpnetflux/storage/posix_file.h"

namespace {
using File = cpnetflux::storage::PosixFile;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

void require(const cpnetflux::common::Status& status) {
    if (!status.isOk()) throw std::runtime_error(status.message());
}

std::uint64_t number(std::string_view text, std::uint64_t maximum) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value == 0 || value > maximum) throw std::runtime_error("numeric argument out of range");
    return value;
}

double seconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double>(end - begin).count();
}

rusage usage(int who) {
    rusage value{};
    if (::getrusage(who, &value)) throw std::runtime_error("getrusage failed");
    return value;
}

double cpu(const rusage& value) {
    return value.ru_utime.tv_sec + value.ru_stime.tv_sec +
        (value.ru_utime.tv_usec + value.ru_stime.tv_usec) / 1e6;
}

double systemCpu(const rusage& value) {
    return value.ru_stime.tv_sec + value.ru_stime.tv_usec / 1e6;
}

struct OwnedFiles {
    fs::path root;
    std::vector<fs::path> paths;
    std::vector<bool> opened;
    bool created = false, retain = false;
    bool cleanup() {
        if (!created || retain) return true;
        bool ok = true;
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (!opened[i]) continue;
            std::error_code error;
            const bool removed = fs::remove(paths[i], error);
            if (removed && !error) opened[i] = false;
            else ok = false;
        }
        std::error_code error;
        // An unexpected file prevents directory removal; never remove it recursively.
        if (ok && fs::remove(root, error) && !error) created = false;
        return ok && !created;
    }
    ~OwnedFiles() { (void)cleanup(); }
};

struct alignas(64) Lane {
    std::uint64_t begin = 0, end = 0, written = 0, calls = 0;
    std::uint64_t mappingCalls = 0, unmappingCalls = 0, maximumMappingBytes = 0;
    double cpuSeconds = 0, systemCpuSeconds = 0;
    Clock::time_point started{}, finished{};
    std::string error;
};

constexpr std::uint64_t mappingWindowBytes = 64ULL << 20;

class MappedWindow {
   public:
    MappedWindow(int fd, std::uint64_t size, Lane& stats) : fd_(fd), size_(size), stats_(stats) {}
    MappedWindow(const MappedWindow&) = delete;
    MappedWindow& operator=(const MappedWindow&) = delete;
    ~MappedWindow() { if (data_) (void)::munmap(data_, length_); }

    void close() {
        if (!data_) return;
        if (::munmap(data_, length_)) throw std::system_error(errno, std::generic_category(), "munmap");
        ++stats_.unmappingCalls;
        data_ = nullptr;
    }

    void write(std::uint64_t offset, const std::uint8_t* bytes, std::size_t length) {
        while (length) {
            const auto base = offset / mappingWindowBytes * mappingWindowBytes;
            if (!data_ || base != offset_) {
                close();
                offset_ = base;
                length_ = std::min(mappingWindowBytes, size_ - base);
                void* mapped = ::mmap(nullptr, length_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, base);
                if (mapped == MAP_FAILED) throw std::system_error(errno, std::generic_category(), "mmap");
                data_ = static_cast<std::uint8_t*>(mapped);
                ++stats_.mappingCalls;
                stats_.maximumMappingBytes = std::max<std::uint64_t>(stats_.maximumMappingBytes, length_);
            }
            const auto copied = std::min<std::size_t>(length, length_ - (offset - offset_));
            std::memcpy(data_ + offset - offset_, bytes, copied);
            offset += copied; bytes += copied; length -= copied;
        }
    }

   private:
    int fd_;
    std::uint64_t size_, offset_ = 0;
    std::size_t length_ = 0;
    Lane& stats_;
    std::uint8_t* data_ = nullptr;
};

std::vector<std::uint8_t> pattern(std::size_t size, std::size_t lane) {
    std::vector<std::uint8_t> result(size);
    for (std::size_t i = 0; i < size; ++i) result[i] = 1 + (i * 31 + lane * 17) % 251;
    return result;
}

void stamp(std::vector<std::uint8_t>& bytes, std::uint64_t offset, std::size_t length) {
    for (std::size_t i = 0; i < std::min<std::size_t>(8, length); ++i)
        bytes[i] = 1 + ((offset >> (8 * i)) & 255) % 251;
}

std::size_t piece(std::uint64_t offset, std::uint64_t end,
                  std::uint64_t frame, std::uint64_t chunk) {
    return std::min({frame, end - offset, chunk - offset % chunk});
}

int run(int argc, char** argv) {
    if (argc < 8 || argc > 10)
        throw std::runtime_error("usage: cpnetflux-offset-write-bench directory shared|shared-open|independent "
            "workers bytes fresh|sparse|rewrite frame-bytes chunk-bytes [--keep-files] [--backend=pwrite|mmap]");
    bool keep = false, backendSeen = false;
    std::string_view backend = "pwrite";
    for (int i = 8; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--keep-files" && !keep) keep = true;
        else if (option.starts_with("--backend=") && !backendSeen) {
            backend = option.substr(10);
            backendSeen = true;
        } else throw std::runtime_error("invalid optional argument");
    }
    if (backend != "pwrite" && backend != "mmap") throw std::runtime_error("invalid backend");
    const std::string_view layout(argv[2]), mode(argv[5]);
    if (layout != "shared" && layout != "shared-open" && layout != "independent")
        throw std::runtime_error("invalid layout");
    if (mode != "fresh" && mode != "sparse" && mode != "rewrite")
        throw std::runtime_error("invalid write mode");
    if (backend == "mmap" && (layout != "shared" || mode == "fresh"))
        throw std::runtime_error("mapped diagnostic requires shared, sized files");
    const auto workers = number(argv[3], 64);
    const auto total = number(argv[4], 8ULL << 30);
    const auto frame = number(argv[6], 65536);
    const auto chunk = number(argv[7], 64ULL << 20);
    if (total < workers) throw std::runtime_error("bytes must cover every worker");
    const bool shared = layout != "independent";
    if (backend == "mmap") {
        const rlimit limit{0, 0};
        if (::setrlimit(RLIMIT_CORE, &limit)) throw std::runtime_error("cannot disable core dumps");
    }
    const auto fileCount = shared ? 1 : workers;
    OwnedFiles owned{fs::path(argv[1]), {}, std::vector<bool>(fileCount)};
    for (std::size_t i = 0; i < fileCount; ++i)
        owned.paths.push_back(owned.root / (shared ? "shared.bin" : "lane-" + std::to_string(i) + ".bin"));
    if (!fs::create_directory(owned.root)) throw std::runtime_error("directory already exists");
    owned.created = true;
    std::vector<File> files;
    files.reserve(fileCount);
    for (std::size_t i = 0; i < fileCount; ++i) {
        auto opened = File::openReadWriteExclusive(owned.paths[i].string());
        if (!opened.isOk()) throw std::runtime_error(opened.status().message());
        owned.opened[i] = true;
        files.push_back(std::move(opened.value()));
    }
    std::vector<Lane> lanes(workers);
    for (std::size_t i = 0; i < workers; ++i) {
        lanes[i].begin = total * i / workers;
        lanes[i].end = total * (i + 1) / workers;
    }
    if (mode == "sparse") {
        for (std::size_t i = 0; i < fileCount; ++i)
            require(files[i].resize(shared ? total : lanes[i].end - lanes[i].begin));
    } else if (mode == "rewrite") {
        const std::vector<std::uint8_t> zeros(frame);
        for (std::size_t i = 0; i < fileCount; ++i) {
            const auto size = shared ? total : lanes[i].end - lanes[i].begin;
            for (std::uint64_t offset = 0; offset < size;) {
                const auto length = std::min<std::uint64_t>(frame, size - offset);
                require(files[i].writeAtAll(offset, zeros.data(), length));
                offset += length;
            }
        }
    }

    std::uint64_t initialFileBytes = 0, initialAllocatedBytes = 0;
    for (const auto& file : files) {
        struct stat value{};
        if (::fstat(file.fd(), &value)) throw std::runtime_error("initial fstat failed");
        initialFileBytes += value.st_size;
        initialAllocatedBytes += static_cast<std::uint64_t>(value.st_blocks) * 512;
    }

    std::vector<File> reopened;
    if (layout == "shared-open") {
        reopened.reserve(workers);
        for (std::size_t i = 0; i < workers; ++i) {
            auto file = File::openReadWrite(owned.paths.front().string());
            if (!file.isOk()) throw std::runtime_error(file.status().message());
            reopened.push_back(std::move(file.value()));
        }
    }
    std::set<int> writerFds;
    std::set<std::pair<std::uint64_t, std::uint64_t>> writerInodes;
    for (std::size_t i = 0; i < workers; ++i) {
        const auto& file = layout == "shared-open" ? reopened[i] : files[shared ? 0 : i];
        struct stat value{};
        if (::fstat(file.fd(), &value)) throw std::runtime_error("writer fstat failed");
        writerFds.insert(file.fd());
        writerInodes.emplace(value.st_dev, value.st_ino);
    }

    std::latch ready(workers), start(1);
    std::atomic<bool> failed{false};
    std::vector<std::jthread> threads;
    threads.reserve(workers);
    try {
        for (std::size_t i = 0; i < workers; ++i) threads.emplace_back([&, i] {
            bool announced = false;
            auto& lane = lanes[i];
            try {
                auto buffer = pattern(frame, i);
                auto& file = layout == "shared-open" ? reopened[i] : files[shared ? 0 : i];
                MappedWindow mapped(file.fd(), total, lane);
                ready.count_down(); announced = true;
                start.wait();
                if (failed.load(std::memory_order_relaxed)) return;
                lane.started = Clock::now();
                const auto before = usage(RUSAGE_THREAD);
                for (auto offset = lane.begin; offset < lane.end;) {
                    if (failed.load(std::memory_order_relaxed))
                        throw std::runtime_error("sibling write failed");
                    const auto length = piece(offset, lane.end, frame, chunk);
                    stamp(buffer, offset, length);
                    ++lane.calls;
                    if (backend == "mmap") mapped.write(offset, buffer.data(), length);
                    else require(file.writeAtAll(shared ? offset : offset - lane.begin, buffer.data(), length));
                    lane.written += length;
                    offset += length;
                }
                mapped.close();
                const auto after = usage(RUSAGE_THREAD);
                lane.cpuSeconds = cpu(after) - cpu(before);
                lane.systemCpuSeconds = systemCpu(after) - systemCpu(before);
                lane.finished = Clock::now();
            } catch (const std::exception& error) {
                lane.error = error.what();
                failed.store(true, std::memory_order_relaxed);
                if (!announced) ready.count_down();
            }
        });
    } catch (...) {
        // Never strand existing workers on the start latch after construction fails.
        failed.store(true, std::memory_order_relaxed);
        start.count_down();
        threads.clear();
        throw;
    }
    ready.wait();
    const auto processBefore = usage(RUSAGE_SELF);
    start.count_down();
    for (auto& thread : threads) thread.join();
    const auto processAfter = usage(RUSAGE_SELF);
    for (const auto& lane : lanes)
        if (!lane.error.empty()) throw std::runtime_error(lane.error);
    if (failed.load()) throw std::runtime_error("worker initialization failed");
    auto first = lanes.front().started, last = lanes.front().finished, latestStart = first;
    std::uint64_t written = 0, calls = 0, mappingCalls = 0, unmappingCalls = 0, maximumMappingBytes = 0;
    double workerCpu = 0, workerSystemCpu = 0;
    for (const auto& lane : lanes) {
        first = std::min(first, lane.started);
        last = std::max(last, lane.finished);
        latestStart = std::max(latestStart, lane.started);
        written += lane.written; calls += lane.calls; workerCpu += lane.cpuSeconds;
        workerSystemCpu += lane.systemCpuSeconds;
        mappingCalls += lane.mappingCalls; unmappingCalls += lane.unmappingCalls;
        maximumMappingBytes = std::max(maximumMappingBytes, lane.maximumMappingBytes);
    }
    if (written != total) throw std::runtime_error("written byte count mismatch");
    const auto verifying = Clock::now();
    std::uint64_t verified = 0;
    std::vector<std::uint8_t> observed(frame);
    for (std::size_t i = 0; i < workers; ++i) {
        auto expected = pattern(frame, i);
        for (auto offset = lanes[i].begin; offset < lanes[i].end;) {
            const auto length = piece(offset, lanes[i].end, frame, chunk);
            stamp(expected, offset, length);
            require(files[shared ? 0 : i].readAtAll(
                shared ? offset : offset - lanes[i].begin, observed.data(), length));
            if (!std::equal(expected.begin(), expected.begin() + length, observed.begin()))
                throw std::runtime_error("file content mismatch");
            verified += length; offset += length;
        }
    }
    for (std::size_t i = 0; i < fileCount; ++i) {
        auto size = files[i].fileSize();
        const auto expected = shared ? total : lanes[i].end - lanes[i].begin;
        if (!size.isOk() || size.value() != expected) throw std::runtime_error("file size mismatch");
    }
    const auto verifySeconds = seconds(verifying, Clock::now());
    reopened.clear();
    files.clear();
    owned.retain = keep;
    if (!owned.cleanup()) throw std::runtime_error("owned file cleanup failed");
    const auto elapsed = seconds(first, last);
    const auto maximum = usage(RUSAGE_SELF);
    std::cout << std::setprecision(12)
        << "{\"schema_version\":3,\"result\":\"pass\",\"diagnostic_only\":true,\"v3\":false,\"network\":false,\"durability\":false"
        << ",\"layout\":\"" << layout << "\",\"write_mode\":\"" << mode << "\",\"workers\":" << workers
        << ",\"backend\":\"" << backend << "\",\"write_fd_count\":" << writerFds.size()
        << ",\"write_inode_count\":" << writerInodes.size()
        << ",\"logical_bytes\":" << total << ",\"written_bytes\":" << written << ",\"verified_bytes\":" << verified
        << ",\"file_count\":" << fileCount << ",\"frame_bytes\":" << frame << ",\"chunk_bytes\":" << chunk
        << ",\"payload_write_calls\":" << calls << ",\"write_at_all_calls\":" << (backend == "pwrite" ? calls : 0)
        << ",\"mapped_copy_calls\":" << (backend == "mmap" ? calls : 0)
        << ",\"mapping_calls\":" << mappingCalls << ",\"unmapping_calls\":" << unmappingCalls
        << ",\"maximum_worker_mapping_bytes\":" << maximumMappingBytes
        << ",\"mapping_window_bytes\":" << (backend == "mmap" ? mappingWindowBytes : 0)
        << ",\"core_dump_disabled\":" << (backend == "mmap" ? "true" : "false")
        << ",\"mapping_lifecycle_in_write_interval\":" << (backend == "mmap" ? "true" : "false")
        << ",\"prefill_bytes\":" << (mode == "rewrite" ? total : 0)
        << ",\"initial_file_bytes\":" << initialFileBytes << ",\"initial_allocated_bytes\":" << initialAllocatedBytes
        << ",\"elapsed_seconds\":" << elapsed << ",\"write_gbps\":" << total * 8 / elapsed / 1e9
        << ",\"write_thread_cpu_seconds\":" << workerCpu
        << ",\"write_thread_system_cpu_seconds\":" << workerSystemCpu
        << ",\"task_cpu_seconds\":" << cpu(processAfter) - cpu(processBefore)
        << ",\"task_system_cpu_seconds\":" << systemCpu(processAfter) - systemCpu(processBefore)
        << ",\"voluntary_context_switches\":" << processAfter.ru_nvcsw - processBefore.ru_nvcsw
        << ",\"involuntary_context_switches\":" << processAfter.ru_nivcsw - processBefore.ru_nivcsw
        << ",\"lane_start_spread_ms\":" << seconds(first, latestStart) * 1000
        << ",\"verification_seconds\":" << verifySeconds << ",\"rss_max_kib\":" << maximum.ru_maxrss
        << ",\"payload_buffer_bound_bytes\":" << std::max<std::uint64_t>(workers, 2) * frame
        << ",\"kept_files\":" << (keep ? "true" : "false")
        << ",\"owned_files_removed\":" << (keep ? "false" : "true") << "}\n";
    return 0;
}
}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << "cpnetflux-offset-write-bench: " << error.what() << '\n';
        return 2;
    }
}
