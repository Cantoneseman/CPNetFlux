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

void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " --backend <auto|software|hardware> --bytes <N> --iterations <N> "
                 "[--buffer-size <N>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    cpnetflux::checksum::ChecksumBackend requestedBackend =
        cpnetflux::checksum::ChecksumBackend::Auto;
    std::uint64_t bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t iterations = 5;
    std::uint64_t bufferSize = 1024ULL * 1024ULL;

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
        if (option == "--backend") {
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
