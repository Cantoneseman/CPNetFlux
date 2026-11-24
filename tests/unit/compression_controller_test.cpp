#include "cpnetflux/core/io/compression_controller.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using cpnetflux::core::io::CodecCalibration;
using cpnetflux::core::io::CompressionController;
using cpnetflux::core::io::CompressionPolicyOptions;
using cpnetflux::core::io::CompressionReason;
using cpnetflux::core::protocol::CompressionCodec;
using Clock = CompressionController::Clock;
namespace protocol = cpnetflux::core::protocol;

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
constexpr std::uint64_t kBlock = 64 * 1024;
constexpr std::uint32_t kAll = protocol::compressionCodecBit(CompressionCodec::Zlib) |
                               protocol::compressionCodecBit(CompressionCodec::Zstd) |
                               protocol::compressionCodecBit(CompressionCodec::Lz4);

const std::vector<std::uint8_t> kTextHead(4096, static_cast<std::uint8_t>('A'));

bool allCodecsBuiltIn() {
    return (protocol::supportedCompressionCodecMask() & kAll) == kAll;
}

CompressionPolicyOptions testOptions() {
    CompressionPolicyOptions options;
    options.minWindowBytes = kMiB;
    options.maxWindowBytes = kMiB;
    options.recheckBytes = 4 * kMiB;
    options.settleSeconds = 0.0;
    options.minWindowSeconds = 0.0;
    return options;
}

std::vector<CodecCalibration> threeCodecs() {
    return {{CompressionCodec::Zlib, 50.0 * kMiB, 0.30},
            {CompressionCodec::Zstd, 100.0 * kMiB, 0.32},
            {CompressionCodec::Lz4, 400.0 * kMiB, 0.50}};
}

void inspectText(CompressionController& controller,
                 const std::vector<CodecCalibration>& calibrations = threeCodecs()) {
    controller.inspectFile(64 * kMiB, kTextHead.data(), kTextHead.size(), calibrations);
}

bool compressing(const CompressionController& controller) {
    return controller.currentCodec().has_value();
}

// Sends one window of bytes in kBlock frames spread evenly over `seconds`.
Clock::time_point sendWindow(CompressionController& controller, Clock::time_point start,
                             double seconds, double waitFraction) {
    const std::uint64_t blocks = kMiB / kBlock;
    const double perBlock = seconds / static_cast<double>(blocks);
    Clock::time_point now = start;
    for (std::uint64_t index = 0; index < blocks; ++index) {
        now += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(perBlock));
        controller.recordSent(kBlock, perBlock * waitFraction, perBlock * (1.0 - waitFraction), now);
    }
    return now;
}

void probe(CompressionController& controller, const CompressionPolicyOptions& options,
           std::uint64_t wirePerBlock) {
    const auto codec = controller.currentCodec().value_or(CompressionCodec::Zlib);
    for (std::uint32_t index = 0; index < options.probeBlocks; ++index) {
        // 100 MiB/s, as calibrated for zstd.
        controller.recordCompressed(kBlock, wirePerBlock,
                                    static_cast<double>(kBlock) / (100.0 * kMiB), codec);
    }
}

TEST(CompressionControllerTest, DetectsCompressedFormats) {
    const auto check = [](const std::string& bytes) {
        return cpnetflux::core::io::looksAlreadyCompressed(
            reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    };
    EXPECT_TRUE(check(std::string("\x1F\x8B\x08\x04", 4)));
    EXPECT_TRUE(check(std::string("\x28\xB5\x2F\xFD\x00", 5)));
    EXPECT_TRUE(check(std::string("\xFD" "7zXZ\x00\x00", 7)));
    EXPECT_TRUE(check("BZh91AY"));
    EXPECT_TRUE(check(std::string("PK\x03\x04\x14\x00", 6)));
    EXPECT_TRUE(check("\x89PNG\r\n\x1A\n"));
    EXPECT_TRUE(check("\xFF\xD8\xFF\xE0"));
    EXPECT_TRUE(check(std::string("RIFF\x10\x00\x00\x00WEBPVP8 ", 16)));
    EXPECT_TRUE(check(std::string("\x00\x00\x00\x20" "ftypisom", 12)));
    EXPECT_FALSE(check("chr1\t12345\tACGT\n"));
    EXPECT_FALSE(check(std::string("RIFF\x10\x00\x00\x00WAVEfmt ", 16)));
    EXPECT_FALSE(check(std::string("\x1F", 1)));
    EXPECT_FALSE(cpnetflux::core::io::looksAlreadyCompressed(nullptr, 0));
}

TEST(CompressionControllerTest, SmallFileIsFinalOff) {
    CompressionController controller(testOptions(), 4, kAll);
    controller.inspectFile(512 * 1024, kTextHead.data(), kTextHead.size(), threeCodecs());
    EXPECT_FALSE(compressing(controller));
    sendWindow(controller, Clock::now(), 0.001, 0.0);
    EXPECT_FALSE(compressing(controller));
    EXPECT_EQ(controller.summary().reason, CompressionReason::SmallFile);
    EXPECT_EQ(controller.summary().switches, 0U);
}

TEST(CompressionControllerTest, CompressedFormatIsFinalOff) {
    CompressionController controller(testOptions(), 4, kAll);
    const std::string gzip("\x1F\x8B\x08\x00", 4);
    controller.inspectFile(64 * kMiB, reinterpret_cast<const std::uint8_t*>(gzip.data()),
                           gzip.size(), {});
    EXPECT_FALSE(compressing(controller));
    EXPECT_EQ(controller.summary().reason, CompressionReason::CompressedFormat);
}

TEST(CompressionControllerTest, PrefersZstdThenZlibThenLz4) {
    if (!allCodecsBuiltIn()) GTEST_SKIP() << "zstd or lz4 not built in";
    CompressionController all(testOptions(), 4, kAll);
    inspectText(all);
    EXPECT_EQ(all.currentCodec(), CompressionCodec::Zstd);

    CompressionController noZstd(testOptions(), 4,
                                 protocol::compressionCodecBit(CompressionCodec::Zlib) |
                                     protocol::compressionCodecBit(CompressionCodec::Lz4));
    inspectText(noZstd);
    EXPECT_EQ(noZstd.currentCodec(), CompressionCodec::Zlib);

    CompressionController lz4Only(testOptions(), 4, protocol::compressionCodecBit(CompressionCodec::Lz4));
    EXPECT_EQ(lz4Only.codecMask(), protocol::compressionCodecBit(CompressionCodec::Lz4));
    EXPECT_EQ(lz4Only.currentCodec(), CompressionCodec::Lz4);
}

TEST(CompressionControllerTest, IncompressibleHeadStartsOffAndRechecks) {
    if (!allCodecsBuiltIn()) GTEST_SKIP() << "zstd or lz4 not built in";
    const CompressionPolicyOptions options = testOptions();
    CompressionController controller(options, 4, kAll);
    inspectText(controller, {{CompressionCodec::Zstd, 100.0 * kMiB, 0.99}});
    EXPECT_FALSE(compressing(controller));
    EXPECT_EQ(controller.summary().reason, CompressionReason::LowRatio);
    Clock::time_point now = Clock::now();
    for (int window = 0; window < 4; ++window) {
        now = sendWindow(controller, now, 0.1, 0.0);
    }
    EXPECT_TRUE(compressing(controller)) << "rechecked after recheckBytes";
}

TEST(CompressionControllerTest, LowRatioProbeTurnsOff) {
    const CompressionPolicyOptions options = testOptions();
    CompressionController controller(options, 4, kAll);
    inspectText(controller);
    ASSERT_TRUE(compressing(controller));
    probe(controller, options, kBlock - 16);
    EXPECT_FALSE(compressing(controller));
    EXPECT_EQ(controller.summary().reason, CompressionReason::LowRatio);
}

TEST(CompressionControllerTest, SocketBoundStaysOnWithoutSwitching) {
    if (!allCodecsBuiltIn()) GTEST_SKIP() << "zstd or lz4 not built in";
    const CompressionPolicyOptions options = testOptions();
    CompressionController controller(options, 4, kAll);
    inspectText(controller);
    probe(controller, options, kBlock / 4);
    Clock::time_point now = Clock::now();
    for (int window = 0; window < 6; ++window) {
        now = sendWindow(controller, now, 1.0, 0.1);
    }
    EXPECT_EQ(controller.currentCodec(), CompressionCodec::Zstd);
    const auto summary = controller.summary();
    EXPECT_EQ(summary.reason, CompressionReason::Active);
    EXPECT_EQ(summary.switches, 0U);
}

TEST(CompressionControllerTest, CpuBoundStepsDownToLz4ThenOff) {
    if (!allCodecsBuiltIn()) GTEST_SKIP() << "zstd or lz4 not built in";
    const CompressionPolicyOptions options = testOptions();
    CompressionController controller(options, 4, kAll);
    inspectText(controller);
    probe(controller, options, kBlock / 4);
    // The first send opens the window; two windows' worth makes sure one fills.
    Clock::time_point now = sendWindow(controller, Clock::now(), 0.5, 0.9);
    now = sendWindow(controller, now, 0.5, 0.9);
    EXPECT_EQ(controller.currentCodec(), CompressionCodec::Lz4);
    EXPECT_EQ(controller.summary().switches, 1U);

    now = sendWindow(controller, now, 0.5, 0.9);
    now = sendWindow(controller, now, 0.5, 0.9);
    EXPECT_FALSE(compressing(controller));
    EXPECT_EQ(controller.summary().reason, CompressionReason::CpuBound);
    sendWindow(controller, now, 0.5, 0.0);
    EXPECT_FALSE(compressing(controller)) << "cpu_bound is final";
}

TEST(CompressionControllerTest, PerFileGapsBetweenBlocksAreNotCpuBound) {
    if (!allCodecsBuiltIn()) GTEST_SKIP() << "zstd or lz4 not built in";
    const CompressionPolicyOptions options = testOptions();
    CompressionController controller(options, 4, kAll);
    inspectText(controller);
    probe(controller, options, kBlock / 4);
    // One block per small file: the sender waits 1 ms for it and the socket takes it at once,
    // but each file also spends 10 ms on its begin/commit round trip and on reading.
    Clock::time_point now = Clock::now();
    for (std::uint64_t block = 0; block < 4 * kMiB / kBlock; ++block) {
        now += std::chrono::milliseconds(10);
        controller.recordSent(kBlock, 0.001, 0.00001, now);
    }
    EXPECT_EQ(controller.currentCodec(), CompressionCodec::Zstd);
    EXPECT_EQ(controller.summary().reason, CompressionReason::Active);
    EXPECT_EQ(controller.summary().switches, 0U);
}

TEST(CompressionControllerTest, WindowsWaitOutTheStartBurst) {
    if (!allCodecsBuiltIn()) GTEST_SKIP() << "zstd or lz4 not built in";
    CompressionPolicyOptions options = testOptions();
    options.settleSeconds = 1.5;
    options.minWindowSeconds = 1.0;
    CompressionController controller(options, 4, kAll);
    inspectText(controller);
    probe(controller, options, kBlock / 4);
    // A token bucket lets the start outrun any compressor; those windows are ignored.
    Clock::time_point now = Clock::now();
    for (int window = 0; window < 3; ++window) {
        now = sendWindow(controller, now, 0.5, 0.9);
    }
    EXPECT_EQ(controller.currentCodec(), CompressionCodec::Zstd) << "still settling";
    for (int window = 0; window < 3; ++window) {
        now = sendWindow(controller, now, 0.5, 0.9);
    }
    EXPECT_EQ(controller.currentCodec(), CompressionCodec::Lz4);
}

TEST(CompressionControllerTest, ConfiguredSlowLinkPicksBestRatio) {
    if (!allCodecsBuiltIn()) GTEST_SKIP() << "zstd or lz4 not built in";
    CompressionPolicyOptions options = testOptions();
    options.linkBytesPerSecond = 5.0 * kMiB;
    CompressionController controller(options, 1, kAll);
    inspectText(controller);
    EXPECT_EQ(controller.currentCodec(), CompressionCodec::Zlib);
}

TEST(CompressionControllerTest, ConfiguredFastLinkPicksCodecThatKeepsUp) {
    if (!allCodecsBuiltIn()) GTEST_SKIP() << "zstd or lz4 not built in";
    CompressionPolicyOptions options = testOptions();
    options.linkBytesPerSecond = 100.0 * kMiB;
    CompressionController controller(options, 1, kAll);
    inspectText(controller);
    EXPECT_EQ(controller.currentCodec(), CompressionCodec::Lz4);
}

TEST(CompressionControllerTest, ConfiguredLinkFasterThanEveryCodecIsFinalOff) {
    CompressionPolicyOptions options = testOptions();
    options.linkBytesPerSecond = 1000.0 * kMiB;
    CompressionController controller(options, 1, kAll);
    inspectText(controller);
    EXPECT_FALSE(compressing(controller));
    EXPECT_EQ(controller.summary().reason, CompressionReason::Bandwidth);
}

TEST(CompressionControllerTest, CalibratesBuiltInCodecsAndSkipsCompressedHeads) {
    const std::vector<std::uint8_t> text(64 * 1024, static_cast<std::uint8_t>('A'));
    const auto calibrations = cpnetflux::core::io::calibrateCompressionCodecs(
        text.data(), text.size(), protocol::supportedCompressionCodecMask(),
        protocol::kDefaultCompressionLevel);
    ASSERT_FALSE(calibrations.empty());
    for (const auto& calibration : calibrations) {
        EXPECT_GT(calibration.bytesPerSecond, 0.0);
        EXPECT_LT(calibration.ratio, 0.1);
    }
    std::vector<std::uint8_t> gzip(text);
    gzip[0] = 0x1F;
    gzip[1] = 0x8B;
    EXPECT_TRUE(cpnetflux::core::io::calibrateCompressionCodecs(
                    gzip.data(), gzip.size(), protocol::supportedCompressionCodecMask(),
                    protocol::kDefaultCompressionLevel)
                    .empty());
}

}  // namespace
