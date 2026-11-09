#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cpnetflux/checksum/checksum.h"

namespace {

cpnetflux::common::Result<std::uint64_t> parseUnsigned(std::string_view value,
                                                      std::string_view name) {
    if (value.empty()) {
        return cpnetflux::common::Status::invalidArgument(std::string(name) + " must not be empty");
    }
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end) {
        return cpnetflux::common::Status::invalidArgument(std::string(name) +
                                                         " must be a decimal integer");
    }
    return parsed;
}

// This isolates chunk-CRC assembly. It neither reads a file nor sends DATA.
int combineBench(std::uint64_t bytes, std::uint64_t iterations, std::uint64_t frameBytes,
                 cpnetflux::checksum::ChecksumBackend backend) {
    using cpnetflux::checksum::ChecksumAlgorithm;
    using cpnetflux::checksum::ChecksumComputer;
    using Clock = std::chrono::steady_clock;
    constexpr std::uint64_t chunkBytes = 1024ULL * 1024;
    if (bytes > 8ULL * 1024 * chunkBytes || bytes % chunkBytes != 0 || iterations > 100 ||
        (frameBytes != 65532 && frameBytes != 256 * 1024 && frameBytes != chunkBytes)) {
        std::cerr << "combine requires 1MiB-aligned bytes <=8GiB, iterations <=100, "
                     "frame-bytes 65532, 262144 or 1048576\n";
        return 2;
    }
    const auto setupStarted = Clock::now();
    std::vector<std::uint8_t> chunk(chunkBytes);
    for (std::size_t i = 0; i < chunk.size(); ++i) chunk[i] = i % 251U;
    ChecksumComputer oracle(ChecksumAlgorithm::Crc32c, backend);
    oracle.update(chunk.data(), chunk.size());
    const auto expected = oracle.finalize();
    struct Leaf { cpnetflux::checksum::ChecksumValue crc; std::uint64_t bytes; };
    std::vector<Leaf> leaves;
    ChecksumComputer warmup(ChecksumAlgorithm::Crc32c, backend);
    for (std::uint64_t offset = 0; offset < chunkBytes;) {
        const auto count = std::min(frameBytes, chunkBytes - offset);
        ChecksumComputer part(ChecksumAlgorithm::Crc32c, backend);
        part.update(chunk.data() + offset, count);
        leaves.push_back({part.finalize(), count});
        if (!warmup.combine(leaves.back().crc, count)) return 1;
        offset += count;
    }
    if (warmup.finalize().value != expected.value) {
        std::cerr << "chunk combine warmup differs from independent CRC scan\n";
        return 1;
    }
    const double setup = std::chrono::duration<double>(Clock::now() - setupStarted).count();
    const auto chunks = bytes / chunkBytes;
    std::uint32_t result = 0, guard = 0;
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        for (std::uint64_t n = 0; n < chunks; ++n) {
            ChecksumComputer combined(ChecksumAlgorithm::Crc32c, backend);
            for (const auto& leaf : leaves) {
                if (!combined.combine(leaf.crc, leaf.bytes)) return 1;
            }
            result = combined.finalize().value;
            guard ^= result + static_cast<std::uint32_t>(n);
        }
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    if (result != expected.value) { std::cerr << "chunk combine mismatch\n"; return 1; }
    const auto calls = chunks * iterations * leaves.size();
    std::cout.precision(12);
    std::cout << "{\"schema\":\"cpnetflux_chunk_combine_bench_v1\","
                 "\"mode\":\"chunk_combine\",\"diagnostic_only\":true,\"file_io\":false,"
                 "\"network_io\":false,\"verified\":true,"
                 "\"timing_scope\":\"warm CRC assembly including accumulator construction; "
                 "leaf CRC scans/setup excluded; virtual repeated chunk, not transfer throughput\","
              << "\"backend\":\"" << cpnetflux::checksum::checksumBackendName(backend) << "\","
              << "\"logical_bytes_per_iteration\":" << bytes << ",\"iterations\":" << iterations
              << ",\"chunk_bytes\":" << chunkBytes << ",\"frame_bytes\":" << frameBytes
              << ",\"frames_per_chunk\":" << leaves.size() << ",\"tail_bytes\":" << chunkBytes % frameBytes
              << ",\"combine_calls\":" << calls << ",\"setup_seconds\":" << setup
              << ",\"elapsed_seconds\":" << elapsed << ",\"ns_per_combine\":" << elapsed * 1e9 / calls
              << ",\"expected_crc32c\":" << expected.value << ",\"result_crc32c\":" << result
              << ",\"guard\":" << guard << "}\n";
    return 0;
}

void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " --backend <auto|software|hardware> --bytes <N> --iterations <N> "
                 "[--buffer-size <N>] [--mode <scan|combine>] [--frame-bytes <N>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    cpnetflux::checksum::ChecksumBackend requestedBackend =
        cpnetflux::checksum::ChecksumBackend::Auto;
    std::uint64_t bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t iterations = 5;
    std::uint64_t bufferSize = 1024ULL * 1024ULL;
    std::uint64_t frameBytes = 65532;
    std::string_view mode = "scan";
    bool frameSpecified = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "-h" || option == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (index + 1 >= argc) {
            std::cerr << option << " requires a value\n";
            usage(argv[0]);
            return 2;
        }
        const std::string_view value(argv[++index]);
        if (option == "--mode") {
            if (value != "scan" && value != "combine") { std::cerr << "invalid mode\n"; return 2; }
            mode = value;
        } else if (option == "--frame-bytes") {
            auto parsed = parseUnsigned(value, "--frame-bytes");
            if (!parsed.isOk()) { std::cerr << parsed.status().message() << '\n'; return 2; }
            frameBytes = parsed.value();
            frameSpecified = true;
        } else if (option == "--backend") {
            auto parsed = cpnetflux::checksum::parseChecksumBackend(value);
            if (!parsed.isOk()) {
                std::cerr << parsed.status().message() << '\n';
                return 2;
            }
            requestedBackend = parsed.value();
        } else if (option == "--bytes") {
            auto parsed = parseUnsigned(value, "--bytes");
            if (!parsed.isOk() || parsed.value() == 0) {
                std::cerr << (parsed.isOk() ? "--bytes must be greater than zero"
                                            : parsed.status().message())
                          << '\n';
                return 2;
            }
            bytes = parsed.value();
        } else if (option == "--iterations") {
            auto parsed = parseUnsigned(value, "--iterations");
            if (!parsed.isOk() || parsed.value() == 0) {
                std::cerr << (parsed.isOk() ? "--iterations must be greater than zero"
                                            : parsed.status().message())
                          << '\n';
                return 2;
            }
            iterations = parsed.value();
        } else if (option == "--buffer-size") {
            auto parsed = parseUnsigned(value, "--buffer-size");
            if (!parsed.isOk() || parsed.value() == 0) {
                std::cerr << (parsed.isOk() ? "--buffer-size must be greater than zero"
                                            : parsed.status().message())
                          << '\n';
                return 2;
            }
            bufferSize = parsed.value();
        } else {
            std::cerr << "unknown option: " << option << '\n';
            usage(argv[0]);
            return 2;
        }
    }

    auto resolved = cpnetflux::checksum::resolveChecksumBackend(
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c, requestedBackend);
    if (!resolved.isOk()) {
        std::cerr << resolved.status().message() << '\n';
        return 1;
    }

    if (mode == "combine") return combineBench(bytes, iterations, frameBytes, resolved.value());
    if (frameSpecified) { std::cerr << "frame-bytes requires combine mode\n"; return 2; }
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(bufferSize));
    for (std::size_t index = 0; index < buffer.size(); ++index) {
        buffer[index] = static_cast<std::uint8_t>(index % 251U);
    }

    std::uint32_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        cpnetflux::checksum::ChecksumComputer computer(cpnetflux::checksum::ChecksumAlgorithm::Crc32c,
                                                      resolved.value());
        std::uint64_t completed = 0;
        while (completed < bytes) {
            const std::size_t size =
                static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), bytes - completed));
            computer.update(buffer.data(), size);
            completed += size;
        }
        checksum ^= computer.finalize().value;
    }
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    const double gbps = seconds > 0.0
                            ? static_cast<double>(bytes) * static_cast<double>(iterations) * 8.0 /
                                  seconds / 1'000'000'000.0
                            : 0.0;

    std::cout << "checksum_bench algorithm=crc32c"
              << " backend=" << cpnetflux::checksum::checksumBackendName(resolved.value())
              << " bytes=" << bytes << " iterations=" << iterations
              << " elapsed_seconds=" << seconds << " throughput_gbps=" << gbps
              << " checksum=" << checksum << '\n';
    return 0;
}
