#include "cpnetflux/core/scheduler/compression_advisor.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path writeRepeatedFile(const std::filesystem::path& root,
                                        const std::string& name, std::size_t bytes) {
    const std::filesystem::path path = root / name;
    const std::string pattern = "CPNetFluxSchedulerCoreMVP";
    std::string payload;
    payload.reserve(bytes);
    while (payload.size() < bytes) {
        payload += pattern;
    }
    payload.resize(bytes);
    std::ofstream output(path, std::ios::binary);
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return path;
}

}  // namespace

TEST(SchedulerCompressionAdvisorTest, FallsBackToRawWhenSamplingUnavailable) {
    cpnetflux::core::scheduler::CompressionAdvisor advisor;
    auto record = advisor.analyzeFile("/tmp/does-not-exist", "file-1", 1234, 7, 0.0, 0.0, false);
    ASSERT_TRUE(record.isOk()) << record.status().message();
    EXPECT_EQ(record.value().decision, "raw");
    EXPECT_EQ(record.value().reason, "sample_unavailable");
    EXPECT_EQ(record.value().sampleRawBytes, 0U);
    EXPECT_EQ(record.value().sampleCompressedBytes, 0U);
}

TEST(SchedulerCompressionAdvisorTest, DetectsCompressionCandidateFromSample) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-scheduler-compression-advisor";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path path = writeRepeatedFile(root, "repeat.bin", 2ULL * 1024ULL * 1024ULL);

    cpnetflux::core::scheduler::CompressionAdvisorConfig config;
    config.poorRatioThreshold = 0.99;
    config.cpuHighThreshold = 99.0;
    config.lowReadyRatio = 0.0;
    config.minCompressGbps = 0.000001;
    config.sampleBlockBytes = 4096;
    cpnetflux::core::scheduler::CompressionAdvisor advisor(config);

    auto record = advisor.analyzeFile(path, "file-2", 2ULL * 1024ULL * 1024ULL, 11, 5.0, 1.0, true);
    ASSERT_TRUE(record.isOk()) << record.status().message();
    EXPECT_EQ(record.value().decision, "compression_candidate");
    EXPECT_EQ(record.value().reason, "worthwhile");
    EXPECT_GT(record.value().sampleRawBytes, record.value().sampleCompressedBytes);
    EXPECT_GT(record.value().sampleCompGbps, 0.0);

    std::filesystem::remove_all(root);
}
