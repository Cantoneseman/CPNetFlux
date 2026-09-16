#include "cpnetflux/checksum/checksum.h"

#include <string>

#include "cpnetflux/checksum/crc32c.h"

namespace cpnetflux::checksum {

ChecksumComputer::ChecksumComputer(ChecksumAlgorithm algorithm, ChecksumBackend backend) noexcept
    : algorithm_(algorithm) {
    auto resolved = resolveChecksumBackend(algorithm, backend);
    backend_ = resolved.isOk() ? resolved.value() : ChecksumBackend::Software;
    reset();
}

void ChecksumComputer::reset() noexcept {
    switch (algorithm_) {
        case ChecksumAlgorithm::None:
            state_ = 0;
            return;
        case ChecksumAlgorithm::Crc32c:
            state_ = crc32cInitialState();
            return;
    }
    state_ = 0;
}

void ChecksumComputer::update(const std::uint8_t* data, std::size_t size) noexcept {
    switch (algorithm_) {
        case ChecksumAlgorithm::None:
            return;
        case ChecksumAlgorithm::Crc32c:
            state_ = crc32cUpdate(state_, data, size, backend_);
            return;
    }
}

ChecksumValue ChecksumComputer::finalize() const noexcept {
    switch (algorithm_) {
        case ChecksumAlgorithm::None:
            return ChecksumValue{algorithm_, 0};
        case ChecksumAlgorithm::Crc32c:
            return ChecksumValue{algorithm_, crc32cFinalize(state_)};
    }
    return ChecksumValue{ChecksumAlgorithm::None, 0};
}

ChecksumAlgorithm ChecksumComputer::algorithm() const noexcept { return algorithm_; }

ChecksumBackend ChecksumComputer::backend() const noexcept { return backend_; }

const char* checksumAlgorithmName(ChecksumAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case ChecksumAlgorithm::None:
            return "none";
        case ChecksumAlgorithm::Crc32c:
            return "crc32c";
    }
    return "none";
}

const char* checksumBackendName(ChecksumBackend backend) noexcept {
    switch (backend) {
        case ChecksumBackend::Auto:
            return "auto";
        case ChecksumBackend::Software:
            return "software";
        case ChecksumBackend::Hardware:
            return "hardware";
    }
    return "software";
}

common::Result<ChecksumBackend> parseChecksumBackend(std::string_view text) {
    if (text == "auto") {
        return ChecksumBackend::Auto;
    }
    if (text == "software") {
        return ChecksumBackend::Software;
    }
    if (text == "hardware") {
        return ChecksumBackend::Hardware;
    }
    return common::Status::invalidArgument("unsupported checksum backend: " + std::string(text));
}

common::Result<ChecksumAlgorithm> parseChecksumAlgorithm(std::string_view text) {
    if (text == "none") {
        return ChecksumAlgorithm::None;
    }
    if (text == "crc32c") {
        return ChecksumAlgorithm::Crc32c;
    }
    return common::Status::invalidArgument("unsupported checksum algorithm: " + std::string(text));
}

common::Result<ChecksumBackend> resolveChecksumBackend(ChecksumAlgorithm algorithm,
                                                       ChecksumBackend requested) {
    if (algorithm == ChecksumAlgorithm::None) {
        return ChecksumBackend::Software;
    }
    if (algorithm != ChecksumAlgorithm::Crc32c) {
        return common::Status::invalidArgument("unsupported checksum algorithm");
    }

    switch (requested) {
        case ChecksumBackend::Software:
            return ChecksumBackend::Software;
        case ChecksumBackend::Hardware:
            if (!crc32cHardwareAvailable()) {
                return common::Status::runtimeError("CRC32C hardware backend is not available");
            }
            return ChecksumBackend::Hardware;
        case ChecksumBackend::Auto:
            return crc32cHardwareAvailable() ? ChecksumBackend::Hardware
                                             : ChecksumBackend::Software;
    }
    return ChecksumBackend::Software;
}

}  // namespace cpnetflux::checksum
