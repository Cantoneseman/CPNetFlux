#include "cpnetflux/core/io/compression_controller.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace cpnetflux::core::io {
namespace {

bool startsWith(const std::uint8_t* head, std::size_t size, std::size_t offset,
                std::string_view magic) noexcept {
    return size >= offset + magic.size() &&
           std::memcmp(head + offset, magic.data(), magic.size()) == 0;
}

constexpr std::size_t codecIndex(CompressionCodec codec) noexcept {
    return static_cast<std::size_t>(codec);
}

// zstd compresses about as well as zlib at a fraction of the CPU; lz4 is the fallback when
// even zstd cannot keep up with the link.
constexpr std::array<CompressionCodec, protocol::kCompressionCodecCount> kPreferenceOrder = {
    CompressionCodec::Zstd, CompressionCodec::Zlib, CompressionCodec::Lz4};

double secondsBetween(CompressionController::Clock::time_point from,
                      CompressionController::Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}

}  // namespace

const char* compressionReasonName(CompressionReason reason) noexcept {
    switch (reason) {
        case CompressionReason::Active:
            return "active";
        case CompressionReason::AlwaysOn:
            return "always";
        case CompressionReason::SmallFile:
            return "small_file";
        case CompressionReason::CompressedFormat:
            return "compressed_format";
        case CompressionReason::Bandwidth:
            return "bandwidth";
        case CompressionReason::LowRatio:
            return "low_ratio";
        case CompressionReason::CpuBound:
            return "cpu_bound";
        case CompressionReason::NoGain:
            return "no_gain";
    }
    return "unknown";
}

bool looksAlreadyCompressed(const std::uint8_t* head, std::size_t size) noexcept {
    if (head == nullptr) {
        return false;
    }
    return startsWith(head, size, 0, "\x1F\x8B") ||                          // gzip, BGZF (BAM)
           startsWith(head, size, 0, "\x28\xB5\x2F\xFD") ||                  // zstd
           startsWith(head, size, 0, std::string_view("\xFD" "7zXZ\x00", 6)) ||  // xz
           startsWith(head, size, 0, "BZh") ||                               // bzip2
           startsWith(head, size, 0, "\x04\x22\x4D\x18") ||                  // lz4 frame
           startsWith(head, size, 0, "PK\x03\x04") ||                        // zip, docx, jar
           startsWith(head, size, 0, "7z\xBC\xAF\x27\x1C") ||                // 7z
           startsWith(head, size, 0, "\x89PNG") ||                           // PNG
           startsWith(head, size, 0, "\xFF\xD8\xFF") ||                      // JPEG
           startsWith(head, size, 0, "GIF8") ||                              // GIF
           (startsWith(head, size, 0, "RIFF") && startsWith(head, size, 8, "WEBP")) ||
           startsWith(head, size, 4, "ftyp");                                // MP4, MOV, HEIC
}

std::vector<CodecCalibration> calibrateCompressionCodecs(const std::uint8_t* head, std::size_t size,
                                                         std::uint32_t codecMask, int level) {
    std::vector<CodecCalibration> calibrations;
    if (head == nullptr || size == 0 || looksAlreadyCompressed(head, size)) {
        return calibrations;
    }
    for (std::size_t index = 0; index < protocol::kCompressionCodecCount; ++index) {
        const auto codec = static_cast<CompressionCodec>(index);
        if ((codecMask & protocol::compressionCodecBit(codec)) == 0) {
            continue;
        }
        const auto start = CompressionController::Clock::now();
        auto encoded = protocol::encodeCompressedDataPayload(
            head, size, size + protocol::kCompressedDataPrefixSize + 1024, codec, level);
        const double seconds = secondsBetween(start, CompressionController::Clock::now());
        if (!encoded.isOk() || seconds <= 0.0) {
            continue;
        }
        CodecCalibration calibration;
        calibration.codec = codec;
        calibration.bytesPerSecond = static_cast<double>(size) / seconds;
        calibration.ratio = static_cast<double>(encoded.value().size()) / static_cast<double>(size);
        calibrations.push_back(calibration);
    }
    return calibrations;
}

CompressionController::CompressionController(CompressionPolicyOptions options,
                                             std::uint32_t workers, std::uint32_t codecMask)
    : options_(options), workers_(std::max(1U, workers)), windowBytes_(options.maxWindowBytes) {
    codecMask &= protocol::supportedCompressionCodecMask();
    if (codecMask == 0) {
        codecMask = protocol::compressionCodecBit(CompressionCodec::Zlib);
    }
    for (std::size_t index = 0; index < codecs_.size(); ++index) {
        codecs_[index].candidate =
            (codecMask & protocol::compressionCodecBit(static_cast<CompressionCodec>(index))) != 0;
    }
    compress_.store(true);
    probing_ = true;
    useCodecLocked(preferredCodecLocked());
}

void CompressionController::inspectFile(std::uint64_t totalSize, const std::uint8_t* head,
                                        std::size_t headSize,
                                        const std::vector<CodecCalibration>& calibrations) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (final_) {
        return;
    }
    // Decisions taken before the first block are not switches.
    struct KeepSwitches {
        std::uint32_t& switches;
        const std::uint32_t saved;
        ~KeepSwitches() { switches = saved; }
    } keepSwitches{switches_, switches_};
    if (totalSize < options_.minFileBytes) {
        final_ = true;
        enterLocked(false, CompressionReason::SmallFile);
        return;
    }
    if (looksAlreadyCompressed(head, headSize)) {
        final_ = true;
        enterLocked(false, CompressionReason::CompressedFormat);
        return;
    }
    windowBytes_ = std::clamp<std::uint64_t>(totalSize / 16, options_.minWindowBytes,
                                             options_.maxWindowBytes);
    for (const CodecCalibration& calibration : calibrations) {
        CodecState& state = codecs_[codecIndex(calibration.codec)];
        if (state.candidate) {
            state.bytesPerSecond = std::max(state.bytesPerSecond, calibration.bytesPerSecond);
            state.ratio = calibration.ratio > 0.0 ? calibration.ratio : state.ratio;
        }
    }

    if (options_.linkBytesPerSecond > 0.0) {
        if (linkTooFastLocked()) {
            final_ = true;
            enterLocked(false, CompressionReason::Bandwidth);
            return;
        }
        std::optional<CompressionCodec> best;
        double bestRate = 0.0;
        for (std::size_t index = 0; index < codecs_.size(); ++index) {
            const auto codec = static_cast<CompressionCodec>(index);
            const double rate = predictedLocked(codec);
            if (rate > bestRate) {
                best = codec;
                bestRate = rate;
            }
        }
        if (best.has_value()) {
            if (bestRate < options_.linkBytesPerSecond * options_.minGain) {
                final_ = true;
                enterLocked(false, CompressionReason::NoGain);
                return;
            }
            useCodecLocked(*best);
            return;
        }
    }

    useCodecLocked(preferredCodecLocked());
    const CodecState& preferred =
        codecs_[codecIndex(static_cast<CompressionCodec>(codec_.load(std::memory_order_relaxed)))];
    if (preferred.ratio > 1.0 - options_.minSavings) {
        enterLocked(false, CompressionReason::LowRatio);
    }
}

void CompressionController::inspectPool(const std::vector<CodecCalibration>& calibrations) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (final_) {
        return;
    }
    windowBytes_ = options_.minWindowBytes;
    for (const CodecCalibration& calibration : calibrations) {
        CodecState& state = codecs_[codecIndex(calibration.codec)];
        if (state.candidate) {
            state.bytesPerSecond = std::max(state.bytesPerSecond, calibration.bytesPerSecond);
        }
    }
    if (options_.linkBytesPerSecond > 0.0 && linkTooFastLocked()) {
        // Decisions taken before the first block are not switches.
        const std::uint32_t switches = switches_;
        final_ = true;
        enterLocked(false, CompressionReason::Bandwidth);
        switches_ = switches;
    }
}

std::uint32_t CompressionController::codecMask() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t mask = 0;
    for (std::size_t index = 0; index < codecs_.size(); ++index) {
        if (codecs_[index].candidate) {
            mask |= protocol::compressionCodecBit(static_cast<CompressionCodec>(index));
        }
    }
    return mask;
}

void CompressionController::recordCompressed(std::uint64_t rawBytes, std::uint64_t wireBytes,
                                             double cpuSeconds, CompressionCodec codec) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (final_ || !probing_ ||
        codec != static_cast<CompressionCodec>(codec_.load(std::memory_order_relaxed))) {
        return;
    }
    probeBlocks_ += 1;
    probeRaw_ += rawBytes;
    probeWire_ += wireBytes;
    probeCpuSeconds_ += cpuSeconds;
    if (probeBlocks_ < options_.probeBlocks) {
        return;
    }
    probing_ = false;
    CodecState& state = codecs_[codecIndex(codec)];
    if (probeCpuSeconds_ > 0.0) {
        state.bytesPerSecond = static_cast<double>(probeRaw_) / probeCpuSeconds_;
    }
    if (probeRaw_ > 0) {
        state.ratio = static_cast<double>(probeWire_) / static_cast<double>(probeRaw_);
    }
    if (static_cast<double>(probeWire_) >
        static_cast<double>(probeRaw_) * (1.0 - options_.minSavings)) {
        enterLocked(false, CompressionReason::LowRatio);
    }
}

void CompressionController::recordSent(std::uint64_t rawBytes, double waitSeconds,
                                       double sendSeconds, Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (final_) {
        return;
    }
    if (!compress_.load(std::memory_order_relaxed)) {
        bytesSinceOff_ += rawBytes;
        if (reason_ == CompressionReason::LowRatio && bytesSinceOff_ >= options_.recheckBytes) {
            enterLocked(true, CompressionReason::Active);
        }
        return;
    }
    if (!settleStart_.has_value()) {
        settleStart_ = now;
    }
    if (secondsBetween(*settleStart_, now) < options_.settleSeconds) {
        return;
    }
    if (!windowStart_.has_value()) {
        windowStart_ = now;
        return;
    }
    windowRaw_ += rawBytes;
    windowWait_ += waitSeconds;
    windowSend_ += sendSeconds;
    if (windowRaw_ >= windowBytes_ &&
        secondsBetween(*windowStart_, now) >= options_.minWindowSeconds) {
        evaluateWindowLocked(now);
    }
}

CompressionSummary CompressionController::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CompressionSummary summary;
    summary.reason = reason_;
    summary.switches = switches_;
    summary.codec = static_cast<CompressionCodec>(codec_.load(std::memory_order_relaxed));
    return summary;
}

void CompressionController::enterLocked(bool compress, CompressionReason reason) {
    if (compress_.load(std::memory_order_relaxed) != compress) {
        switches_ += 1;
    }
    compress_.store(compress, std::memory_order_relaxed);
    reason_ = reason;
    settleStart_.reset();
    windowStart_.reset();
    windowRaw_ = 0;
    windowWait_ = 0.0;
    windowSend_ = 0.0;
    bytesSinceOff_ = 0;
    probing_ = compress && !final_;
    probeBlocks_ = 0;
    probeRaw_ = 0;
    probeWire_ = 0;
    probeCpuSeconds_ = 0.0;
}

void CompressionController::useCodecLocked(CompressionCodec codec) {
    codec_.store(static_cast<std::uint8_t>(codec), std::memory_order_relaxed);
}

void CompressionController::evaluateWindowLocked(Clock::time_point now) {
    // Against wall time, not wait + send: with one block per small file most of the time
    // goes to per-file round trips and reads, which neither counts.
    const double elapsed = std::max(secondsBetween(*windowStart_, now), windowWait_ + windowSend_);
    const double waitFraction = elapsed > 0.0 ? windowWait_ / elapsed : 0.0;
    windowStart_.reset();
    windowRaw_ = 0;
    windowWait_ = 0.0;
    windowSend_ = 0.0;
    if (waitFraction <= options_.cpuBoundWaitFraction) {
        return;
    }
    const auto current = static_cast<CompressionCodec>(codec_.load(std::memory_order_relaxed));
    if (const auto faster = fasterCodecLocked(current)) {
        useCodecLocked(*faster);
        switches_ += 1;
        settleStart_.reset();
        return;
    }
    // Already on the fastest codec and still waiting on it: the link outruns compression.
    final_ = true;
    enterLocked(false, CompressionReason::CpuBound);
}

CompressionCodec CompressionController::preferredCodecLocked() const noexcept {
    for (const CompressionCodec codec : kPreferenceOrder) {
        if (codecs_[codecIndex(codec)].candidate) {
            return codec;
        }
    }
    return CompressionCodec::Zlib;
}

std::optional<CompressionCodec> CompressionController::fasterCodecLocked(
    CompressionCodec current) const noexcept {
    const double currentSpeed = codecs_[codecIndex(current)].bytesPerSecond;
    std::optional<CompressionCodec> fastest;
    double fastestSpeed = currentSpeed;
    for (std::size_t index = 0; index < codecs_.size(); ++index) {
        const auto codec = static_cast<CompressionCodec>(index);
        const CodecState& state = codecs_[index];
        if (codec != current && state.candidate && state.bytesPerSecond > fastestSpeed) {
            fastest = codec;
            fastestSpeed = state.bytesPerSecond;
        }
    }
    if (!fastest.has_value() && currentSpeed <= 0.0 && current != CompressionCodec::Lz4 &&
        codecs_[codecIndex(CompressionCodec::Lz4)].candidate) {
        return CompressionCodec::Lz4;
    }
    return fastest;
}

double CompressionController::predictedLocked(CompressionCodec codec) const noexcept {
    const CodecState& state = codecs_[codecIndex(codec)];
    if (!state.candidate || state.bytesPerSecond <= 0.0 || state.ratio <= 0.0) {
        return 0.0;
    }
    const double cpu = options_.bandwidthHeadroom * state.bytesPerSecond * workers_;
    return std::min(cpu, options_.linkBytesPerSecond / state.ratio);
}

bool CompressionController::linkTooFastLocked() const noexcept {
    double fastest = 0.0;
    for (const CodecState& state : codecs_) {
        if (state.candidate) {
            fastest = std::max(fastest, state.bytesPerSecond * workers_);
        }
    }
    return fastest > 0.0 &&
           options_.linkBytesPerSecond >= options_.bandwidthHeadroom * fastest;
}

}  // namespace cpnetflux::core::io
