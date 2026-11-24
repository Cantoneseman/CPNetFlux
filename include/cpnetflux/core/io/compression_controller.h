#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "cpnetflux/core/protocol/compressed_data.h"

namespace cpnetflux::core::io {

using protocol::CompressionCodec;

enum class CompressionReason {
    Active,
    AlwaysOn,
    SmallFile,
    CompressedFormat,
    Bandwidth,
    LowRatio,
    CpuBound,
    NoGain,
};

inline constexpr std::size_t kCompressionReasonCount = 8;

[[nodiscard]] const char* compressionReasonName(CompressionReason reason) noexcept;

struct CompressionPolicyOptions {
    std::uint64_t minFileBytes = 1024ULL * 1024ULL;
    std::uint32_t probeBlocks = 8;
    double minSavings = 0.05;
    // A known link only gets compressed while it is slower than headroom * compression speed.
    double bandwidthHeadroom = 0.8;
    // With a known link, compression must beat plain sending by this factor.
    double minGain = 1.05;
    // A window where the sender spends more than this share of its time waiting for the pool
    // means compression is the bottleneck: step down to a faster codec, then to plain.
    double cpuBoundWaitFraction = 0.5;
    std::uint64_t minWindowBytes = 1024ULL * 1024ULL;
    std::uint64_t maxWindowBytes = 32ULL * 1024ULL * 1024ULL;
    // After a low-ratio decision, compression is probed again every recheckBytes.
    std::uint64_t recheckBytes = 64ULL * 1024ULL * 1024ULL;
    // Rate-limited links often allow a burst at the start (a token bucket), during which the
    // socket outruns any compressor; windows only start this long after the first send or a
    // codec change.
    double settleSeconds = 1.5;
    // Shorter windows make the wait share too noisy.
    double minWindowSeconds = 1.0;
    // --compression-link-mbps; 0 means unknown, which is the normal case.
    double linkBytesPerSecond = 0.0;
};

// Single-thread speed and wire/raw ratio of one codec, measured on the head of the file.
struct CodecCalibration {
    CompressionCodec codec = CompressionCodec::Zlib;
    double bytesPerSecond = 0.0;
    double ratio = 0.0;
};

inline constexpr std::size_t kCompressionCalibrationBytes = 128 * 1024;
// Enough of a file's head for looksAlreadyCompressed().
inline constexpr std::size_t kCompressionMagicBytes = 16;

// Magic-byte check for gzip/BGZF, zstd, xz, bzip2, lz4, zip, 7z, PNG, JPEG, GIF, WebP and MP4.
[[nodiscard]] bool looksAlreadyCompressed(const std::uint8_t* head, std::size_t size) noexcept;

// Compresses head with every codec in codecMask, skipping formats that are already compressed.
[[nodiscard]] std::vector<CodecCalibration> calibrateCompressionCodecs(
    const std::uint8_t* head, std::size_t size, std::uint32_t codecMask, int level);

struct CompressionSummary {
    CompressionReason reason = CompressionReason::Active;
    std::uint32_t switches = 0;
    CompressionCodec codec = CompressionCodec::Zlib;
};

// Compression decision for one file, shared by the sender and the pool workers compressing
// its blocks. Before sending: small files and already-compressed formats go plain for the whole
// file. Otherwise compression starts with the preferred codec (zstd, then zlib, then lz4); the
// first probeBlocks blocks check the ratio, and a poor ratio turns compression off with a recheck
// every recheckBytes. While sending, the only signal is whether the sender waits for the pool:
// if it does, compression is the bottleneck and the controller steps down to the fastest codec,
// then to plain. Nothing measures the link; a configured link speed picks the codec up front.
class CompressionController {
 public:
    using Clock = std::chrono::steady_clock;

    CompressionController(CompressionPolicyOptions options, std::uint32_t workers,
                          std::uint32_t codecMask);

    void inspectFile(std::uint64_t totalSize, const std::uint8_t* head, std::size_t headSize,
                     const std::vector<CodecCalibration>& calibrations);

    // For a controller shared by many small files instead of inspectFile: only codec speeds are
    // taken, so no single file's format or ratio turns compression off for the others. The
    // caller sends already-compressed files plain without consulting the controller.
    void inspectPool(const std::vector<CodecCalibration>& calibrations);

    [[nodiscard]] std::uint32_t codecMask() const;

    // Empty while blocks should go out plain.
    [[nodiscard]] std::optional<CompressionCodec> currentCodec() const noexcept {
        if (!compress_.load(std::memory_order_relaxed)) {
            return std::nullopt;
        }
        return static_cast<CompressionCodec>(codec_.load(std::memory_order_relaxed));
    }

    // Called by pool workers after compressing one block with codec.
    void recordCompressed(std::uint64_t rawBytes, std::uint64_t wireBytes, double cpuSeconds,
                          CompressionCodec codec);

    // Called by the sender after one DATA frame went out. waitSeconds is time spent waiting for
    // the pool to finish that block, sendSeconds the time spent writing it to the socket.
    void recordSent(std::uint64_t rawBytes, double waitSeconds, double sendSeconds,
                    Clock::time_point now);

    [[nodiscard]] CompressionSummary summary() const;

 private:
    struct CodecState {
        bool candidate = false;
        double bytesPerSecond = 0.0;
        double ratio = 0.0;
    };

    void enterLocked(bool compress, CompressionReason reason);
    void useCodecLocked(CompressionCodec codec);
    void evaluateWindowLocked(Clock::time_point now);
    [[nodiscard]] CompressionCodec preferredCodecLocked() const noexcept;
    // Candidate with the highest calibrated speed, preferring lz4 when nothing is calibrated.
    [[nodiscard]] std::optional<CompressionCodec> fasterCodecLocked(
        CompressionCodec current) const noexcept;
    // Raw bytes per second codec would deliver over the configured link; 0 without estimates.
    [[nodiscard]] double predictedLocked(CompressionCodec codec) const noexcept;
    [[nodiscard]] bool linkTooFastLocked() const noexcept;

    const CompressionPolicyOptions options_;
    const std::uint32_t workers_;
    std::atomic<bool> compress_{false};
    std::atomic<std::uint8_t> codec_{0};

    mutable std::mutex mutex_;
    std::array<CodecState, protocol::kCompressionCodecCount> codecs_{};
    CompressionReason reason_ = CompressionReason::Active;
    bool final_ = false;
    std::uint64_t windowBytes_;
    std::uint32_t switches_ = 0;

    bool probing_ = false;
    std::uint32_t probeBlocks_ = 0;
    std::uint64_t probeRaw_ = 0;
    std::uint64_t probeWire_ = 0;
    double probeCpuSeconds_ = 0.0;

    // First send since the start or the last change; windows open settleSeconds later.
    std::optional<Clock::time_point> settleStart_;
    std::optional<Clock::time_point> windowStart_;
    std::uint64_t windowRaw_ = 0;
    double windowWait_ = 0.0;
    double windowSend_ = 0.0;
    std::uint64_t bytesSinceOff_ = 0;
};

}  // namespace cpnetflux::core::io
