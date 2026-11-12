#include <sys/resource.h>
#include <time.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/checksum/crc32c.h"
#include "cpnetflux/core/tree/tree_manifest.h"
#include "cpnetflux/core/tree/tree_scan.h"

namespace tree = cpnetflux::core::tree;
namespace checksum = cpnetflux::checksum;
using cpnetflux::common::Result;
using cpnetflux::common::Status;

namespace {

struct Options {
    std::string mode = "baseline", fixture = "matrix", order = "sorted";
    std::string transferMode = "download", algorithm = "crc32c";
    std::size_t files = 10000, snapshots = 225;
    bool emit = false, verify = false;
};

std::size_t number(std::string_view text) {
    std::size_t value = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::runtime_error("invalid numeric argument");
    return value;
}

Options options(int argc, char** argv) {
    Options out;
    for (int i = 1; i < argc; ++i) {
        const std::string_view key(argv[i]);
        if (key == "--emit-body") { out.emit = true; continue; }
        if (key == "--verify-series") { out.verify = true; continue; }
        if (i + 1 >= argc) throw std::runtime_error("missing option value");
        const std::string value(argv[++i]);
        if (key == "--mode") out.mode = value;
        else if (key == "--fixture") out.fixture = value;
        else if (key == "--order") out.order = value;
        else if (key == "--files") out.files = number(value);
        else if (key == "--snapshots") out.snapshots = number(value);
        else if (key == "--transfer-mode") out.transferMode = value;
        else if (key == "--checksum") out.algorithm = value;
        else throw std::runtime_error("unknown argument");
    }
    if ((out.mode != "baseline" && out.mode != "candidate") ||
        (out.order != "sorted" && out.order != "reversed") ||
        (out.transferMode != "upload" && out.transferMode != "download") ||
        (out.algorithm != "none" && out.algorithm != "crc32c") ||
        !out.files || out.files > 100000 || !out.snapshots || out.snapshots > 1000 ||
        out.files * out.snapshots > 10000000)
        throw std::runtime_error("invalid or unbounded request");
    return out;
}

std::string identity(std::size_t index) {
    std::ostringstream out;
    out << std::hex << std::setw(32) << std::setfill('0') << index + 1;
    return out.str();
}

tree::TreeManifest fixture(const Options& o) {
    tree::TreeManifest m;
    m.version = 1; // Frozen file-only diagnostic fixture; runtime manifests use schema 2.
    m.mode = o.transferMode == "upload" ? tree::TreeTransferMode::Upload : tree::TreeTransferMode::Download;
    m.checksumAlgorithm = o.algorithm == "none" ? checksum::ChecksumAlgorithm::None : checksum::ChecksumAlgorithm::Crc32c;
    m.rootLogicalPath = "/tmp/root: data";
    m.updatedAtUnixNanos = std::numeric_limits<std::uint64_t>::max();
    if (o.fixture == "matrix") {
        m.files.reserve(o.files);
        for (std::size_t i = 0; i < o.files; ++i) {
            auto digits = std::to_string(i);
            m.files.push_back({"f" + std::string(6 - digits.size(), '0') + digits + ".bin",
                4096, static_cast<std::int64_t>(1000000 + i), identity(i), tree::TreeFileStatus::Pending, ""});
        }
        if (o.order == "reversed") std::reverse(m.files.begin(), m.files.end());
        return m;
    }
    if (o.fixture == "empty") return m;
    const std::vector<std::string> names{"z.dat", "a/z", "a-1", "bb:line,.dat", "\xc3\xa9/\xe6\x95\xb0\xe6\x8d\xae", "zero"};
    const std::uint64_t sizes[]{0, UINT64_MAX, 4096, 1, 65536, 0};
    const std::int64_t times[]{INT64_MIN, INT64_MAX, -1, 0, 42, 0};
    const tree::TreeFileStatus states[]{tree::TreeFileStatus::Pending, tree::TreeFileStatus::Transferring,
        tree::TreeFileStatus::Completed, tree::TreeFileStatus::Failed, tree::TreeFileStatus::Changed, tree::TreeFileStatus::Completed};
    for (std::size_t i = 0; i < names.size(); ++i)
        m.files.push_back({names[i], sizes[i], times[i], identity(i), states[i],
            i == 3 ? std::string("bad:\n,\0", 7) : i == 4 ? "\xff error" : ""});
    if (o.fixture == "shuffle") std::reverse(m.files.begin(), m.files.end());
    else if (o.fixture == "duplicate") {
        m.files.clear();
        for (std::size_t i = 0; i < 40; ++i)
            m.files.push_back({"same", i, static_cast<std::int64_t>(i), identity(i), tree::TreeFileStatus::Pending, ""});
    } else if (o.fixture == "bad-version") m.version = 3;
    else if (o.fixture == "bad-root") m.rootLogicalPath.clear();
    else if (o.fixture == "bad-path") m.files[0].relativePath = "../escape";
    else if (o.fixture == "bad-control") m.files[0].relativePath = "bad\npath";
    else if (o.fixture == "bad-id") m.files[0].transferId = "bad/id";
    else if (o.fixture == "short-id") m.files[0].transferId = "a-b_.9";
    else if (o.fixture != "mixed") throw std::runtime_error("unknown fixture");
    return m;
}

Result<std::string> candidate(const tree::TreeManifest& m) {
    return tree::serializeTreeManifest(m, tree::TreeManifestSerializer::Buffered);
}

Result<std::string> serialize(const tree::TreeManifest& m, const Options& o) {
    return o.mode == "baseline" ? tree::serializeTreeManifest(m) : candidate(m);
}

bool equal(const tree::TreeManifest& a, const tree::TreeManifest& b) {
    if (a.version != b.version || a.mode != b.mode || a.rootLogicalPath != b.rootLogicalPath ||
        a.checksumAlgorithm != b.checksumAlgorithm || a.createdAtUnixNanos != b.createdAtUnixNanos ||
        a.updatedAtUnixNanos != b.updatedAtUnixNanos || a.files.size() != b.files.size()) return false;
    for (std::size_t i = 0; i < a.files.size(); ++i) {
        const auto& x = a.files[i]; const auto& y = b.files[i];
        if (x.relativePath != y.relativePath || x.size != y.size || x.mtimeUnixSeconds != y.mtimeUnixSeconds ||
            x.transferId != y.transferId || x.status != y.status || x.error != y.error) return false;
    }
    return true;
}

void evolve(tree::TreeManifest& m, std::size_t snapshot, std::size_t count) {
    m.updatedAtUnixNanos = snapshot;
    const auto completed = (snapshot + 1) * m.files.size() / count;
    for (auto& r : m.files) {
        const auto index = static_cast<std::size_t>(r.mtimeUnixSeconds - 1000000);
        r.status = index < completed ? tree::TreeFileStatus::Completed :
            index < completed + 4 ? tree::TreeFileStatus::Transferring : tree::TreeFileStatus::Pending;
    }
}

double clockSeconds(clockid_t clock) {
    timespec value{};
    if (clock_gettime(clock, &value)) throw std::runtime_error("clock_gettime failed");
    return value.tv_sec + value.tv_nsec / 1e9;
}

void verifySeries(const Options& o) {
    auto m = fixture(o);
    for (std::size_t i = 0; i < o.snapshots; ++i) {
        evolve(m, i, o.snapshots);
        const auto before = m;
        auto a = tree::serializeTreeManifest(m), b = candidate(m);
        if (!a.isOk() || !b.isOk() || a.value() != b.value() || !equal(before, m))
            throw std::runtime_error("byte equality or input immutability failed");
        auto parsed = tree::parseTreeManifest(a.value());
        if (!parsed.isOk()) throw std::runtime_error("series parse failed");
        std::sort(parsed.value().files.begin(), parsed.value().files.end(), [](const auto& x, const auto& y) {return x.relativePath < y.relativePath;});
        auto sorted = before;
        std::sort(sorted.files.begin(), sorted.files.end(), [](const auto& x, const auto& y) {return x.relativePath < y.relativePath;});
        if (!equal(parsed.value(), sorted)) throw std::runtime_error("parsed fields changed");
    }
    std::cout << "{\"byte_equal_snapshots\":" << o.snapshots << ",\"parse_equal_snapshots\":" << o.snapshots
              << ",\"input_unchanged\":true}\n";
}

void benchmark(const Options& o) {
    std::uint64_t bytes = 0;
    std::uint32_t digest = checksum::crc32cInitialState();
    const auto wall = clockSeconds(CLOCK_MONOTONIC), cpu = clockSeconds(CLOCK_PROCESS_CPUTIME_ID);
    {
        auto m = fixture(o);
        for (std::size_t i = 0; i < o.snapshots; ++i) {
            evolve(m, i, o.snapshots);
            auto encoded = serialize(m, o);
            if (!encoded.isOk()) throw std::runtime_error(encoded.status().message());
            const auto& text = encoded.value(); bytes += text.size();
            digest = checksum::crc32cUpdate(digest, reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
        }
    }
    const auto cpuSeconds = clockSeconds(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    const auto wallSeconds = clockSeconds(CLOCK_MONOTONIC) - wall;
    rusage usage{};getrusage(RUSAGE_SELF, &usage);
    std::cout << std::setprecision(12) << "{\"mode\":\"" << o.mode << "\",\"order\":\"" << o.order
        << "\",\"files\":" << o.files << ",\"snapshots\":" << o.snapshots << ",\"serialized_bytes\":" << bytes
        << ",\"series_crc32c\":" << checksum::crc32cFinalize(digest) << ",\"cpu_seconds\":" << cpuSeconds << ",\"wall_seconds\":" << wallSeconds
        << ",\"max_rss_bytes\":" << usage.ru_maxrss * 1024 << "}\n";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const rlimit memory{256ULL * 1024 * 1024, 256ULL * 1024 * 1024};
        if (setrlimit(RLIMIT_AS, &memory)) throw std::runtime_error("owned address-space limit failed");
        const auto o = options(argc, argv);
        if (o.emit) {
            auto m = fixture(o);const auto before = m;
            auto result = serialize(m, o);
            if (!result.isOk()) throw std::runtime_error(result.status().message());
            if (!equal(before, m)) throw std::runtime_error("input mutated");
            std::cout << result.value();
        } else if (o.verify) verifySeries(o);
        else if (o.fixture == "matrix") benchmark(o);
        else throw std::runtime_error("benchmark requires matrix fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';return 1;
    }
}
