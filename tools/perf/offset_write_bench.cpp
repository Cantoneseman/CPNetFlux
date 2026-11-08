#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <string>
#include <string_view>
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
    double cpuSeconds = 0;
    Clock::time_point started{}, finished{};
    std::string error;
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
    if (argc != 8 && argc != 9)
        throw std::runtime_error("usage: cpnetflux-offset-write-bench directory shared|independent "
            "workers bytes fresh|rewrite frame-bytes chunk-bytes [--keep-files]");
    const bool keep = argc == 9;
    if (keep && std::string_view(argv[8]) != "--keep-files")
        throw std::runtime_error("invalid optional argument");
    const std::string_view layout(argv[2]), mode(argv[5]);
    if (layout != "shared" && layout != "independent") throw std::runtime_error("invalid layout");
    if (mode != "fresh" && mode != "rewrite") throw std::runtime_error("invalid write mode");
    const auto workers = number(argv[3], 64);
    const auto total = number(argv[4], 8ULL << 30);
    const auto frame = number(argv[6], 65536);
    const auto chunk = number(argv[7], 64ULL << 20);
    if (total < workers) throw std::runtime_error("bytes must cover every worker");
    const bool shared = layout == "shared";
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
    if (mode == "rewrite") {
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
                    require(files[shared ? 0 : i].writeAtAll(
                        shared ? offset : offset - lane.begin, buffer.data(), length));
                    lane.written += length;
                    offset += length;
                }
                lane.cpuSeconds = cpu(usage(RUSAGE_THREAD)) - cpu(before);
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
    std::uint64_t written = 0, calls = 0;
    double workerCpu = 0;
    for (const auto& lane : lanes) {
        first = std::min(first, lane.started);
        last = std::max(last, lane.finished);
        latestStart = std::max(latestStart, lane.started);
        written += lane.written; calls += lane.calls; workerCpu += lane.cpuSeconds;
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
    files.clear();
    owned.retain = keep;
    if (!owned.cleanup()) throw std::runtime_error("owned file cleanup failed");
    const auto elapsed = seconds(first, last);
    const auto maximum = usage(RUSAGE_SELF);
    std::cout << std::setprecision(12)
        << "{\"schema_version\":1,\"result\":\"pass\",\"diagnostic_only\":true,\"v3\":false,\"network\":false,\"durability\":false"
        << ",\"layout\":\"" << layout << "\",\"write_mode\":\"" << mode << "\",\"workers\":" << workers
        << ",\"logical_bytes\":" << total << ",\"written_bytes\":" << written << ",\"verified_bytes\":" << verified
        << ",\"file_count\":" << fileCount << ",\"frame_bytes\":" << frame << ",\"chunk_bytes\":" << chunk
        << ",\"write_at_all_calls\":" << calls << ",\"prefill_bytes\":" << (mode == "rewrite" ? total : 0)
        << ",\"elapsed_seconds\":" << elapsed << ",\"write_gbps\":" << total * 8 / elapsed / 1e9
        << ",\"write_thread_cpu_seconds\":" << workerCpu
        << ",\"task_cpu_seconds\":" << cpu(processAfter) - cpu(processBefore)
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
