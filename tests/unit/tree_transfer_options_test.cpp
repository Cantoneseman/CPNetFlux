#include "cpnetflux/config/tree_transfer_options.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "cpnetflux/core/io/tls_socket.h"

TEST(TreeTransferOptionsTest, ParsesUploadOptions) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-upload";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* argv[] = {"cpnetflux-tree-upload-client",
                          "--host",
                          "127.0.0.1",
                          "--port",
                          "2121",
                          "--source-dir",
                          rootText.c_str(),
                          "--dest-dir",
                          "remote/data",
                          "--connections",
                          "4",
                          "--file-parallelism",
                          "2",
                          "--chunk-size",
                          "4194304",
                          "--buffer-size",
                          "262144",
                          "--checksum",
                          "none",
                          "--checksum-backend",
                          "software",
                          "--max-files",
                          "1",
                          "--user",
                          "alice",
                          "--password",
                          "secret",
                          "--auth-mode",
                          "anonymous",
                          "--control-reuse",
                          "worker",
                          "--planner-preset",
                          "cpnetflux_b1_control_reuse",
                          "--json-summary",
                          "/tmp/tree-summary.json",
                          "--event-log",
                          "/tmp/tree-events.jsonl",
                          "--scheduler",
                          "global",
                          "--scheduler-policy",
                          "adaptive",
                          "--scheduler-metrics-dir",
                          "/tmp/tree-scheduler-metrics",
                          "--scheduler-link-id",
                          "wan0",
                          "--scheduler-capacity-gbps",
                          "0.01",
                          "--scheduler-workitem-min-bytes",
                          "1048576",
                          "--scheduler-workitem-max-bytes",
                          "2097152",
                          "--scheduler-default-rtt-ms",
                          "20",
                          "--scheduler-min-compress-gbps",
                          "0.5",
                          "--compression",
                          "auto"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().destDir, "remote/data");
    EXPECT_EQ(parsed.value().connections, 4U);
    EXPECT_EQ(parsed.value().fileParallelism, 2U);
    EXPECT_EQ(parsed.value().chunkSize, 4194304U);
    EXPECT_EQ(parsed.value().bufferSize, 262144U);
    EXPECT_EQ(parsed.value().checksumAlgorithm, cpnetflux::checksum::ChecksumAlgorithm::None);
    EXPECT_EQ(parsed.value().authMode, "anonymous");
    EXPECT_EQ(parsed.value().tls.mode, cpnetflux::core::io::TlsMode::Off);
    EXPECT_EQ(parsed.value().dataTlsMode, cpnetflux::core::io::DataTlsMode::Off);
    EXPECT_EQ(parsed.value().controlReuseMode, cpnetflux::config::ControlReuseMode::Worker);
    EXPECT_EQ(cpnetflux::config::controlReuseModeName(parsed.value().controlReuseMode),
              std::string("worker"));
    EXPECT_EQ(parsed.value().plannerPreset, "cpnetflux_b1_control_reuse");
    EXPECT_EQ(parsed.value().user, "alice");
    EXPECT_EQ(parsed.value().jsonSummaryPath, "/tmp/tree-summary.json");
    EXPECT_EQ(parsed.value().eventLogPath, "/tmp/tree-events.jsonl");
    EXPECT_FALSE(parsed.value().phaseTiming);
    EXPECT_EQ(parsed.value().controlPipelineDepth, 0U);
    EXPECT_EQ(parsed.value().schedulerMode, cpnetflux::config::TreeSchedulerMode::Global);
    EXPECT_EQ(parsed.value().schedulerPolicy,
              cpnetflux::core::scheduler::SchedulerPolicy::Adaptive);
    EXPECT_EQ(parsed.value().schedulerMetricsDir, "/tmp/tree-scheduler-metrics");
    EXPECT_EQ(parsed.value().schedulerLinkId, "wan0");
    EXPECT_DOUBLE_EQ(parsed.value().schedulerCapacityGbps, 0.01);
    EXPECT_EQ(parsed.value().schedulerWorkItemMinBytes, 1048576U);
    EXPECT_EQ(parsed.value().schedulerWorkItemMaxBytes, 2097152U);
    EXPECT_EQ(parsed.value().schedulerDefaultRttMs, 20U);
    EXPECT_DOUBLE_EQ(parsed.value().schedulerMinCompressGbps, 0.5);
    EXPECT_EQ(parsed.value().compressionMode, cpnetflux::config::CompressionMode::Auto);
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, AllowsConcurrentWorkersForDepthOnePipeline) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-pipeline-fp8";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* argv[] = {"cpnetflux-tree-upload-client",
                          "--host", "127.0.0.1",
                          "--port", "2121",
                          "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote/data",
                          "--file-parallelism", "8",
                          "--control-reuse", "worker",
                          "--control-pipeline-depth", "1",
                          "--scheduler", "off"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    std::filesystem::remove_all(root);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().fileParallelism, 8U);
    EXPECT_EQ(parsed.value().controlPipelineDepth, 1U);
}

TEST(TreeTransferOptionsTest, DefaultsControlReuseOff) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-default-reuse";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote/data"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().controlReuseMode, cpnetflux::config::ControlReuseMode::Off);
    EXPECT_EQ(cpnetflux::config::controlReuseModeName(parsed.value().controlReuseMode),
              std::string("off"));
    EXPECT_TRUE(parsed.value().plannerPreset.empty());
    EXPECT_EQ(parsed.value().schedulerMode, cpnetflux::config::TreeSchedulerMode::Off);
    EXPECT_EQ(parsed.value().schedulerPolicy, cpnetflux::core::scheduler::SchedulerPolicy::Fixed);
    EXPECT_TRUE(parsed.value().schedulerMetricsDir.empty());
    EXPECT_EQ(parsed.value().schedulerLinkId, "link0");
    EXPECT_DOUBLE_EQ(parsed.value().schedulerCapacityGbps, 0.01);
    EXPECT_EQ(parsed.value().schedulerWorkItemMinBytes, 64ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(parsed.value().schedulerWorkItemMaxBytes, 256ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(parsed.value().schedulerDefaultRttMs, 10U);
    EXPECT_DOUBLE_EQ(parsed.value().schedulerMinCompressGbps, 1.0);
    EXPECT_EQ(parsed.value().compressionMode, cpnetflux::config::CompressionMode::Off);
    EXPECT_FALSE(parsed.value().phaseTiming);
    EXPECT_FALSE(parsed.value().hotPathCompression.enabled);
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, ParsesPhaseTimingFlagAndDefaultsOff) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-phase-timing-root";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--phase-timing", "on"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_TRUE(parsed.value().phaseTiming);

    const char* invalid[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                             "--dest-dir", "remote", "--phase-timing", "yes"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(invalid)), invalid,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, ParsesBoundedDataPendingWindow) {
    const auto root = std::filesystem::temp_directory_path() / "cpnetflux-tree-options-window-root";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* valid[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                           "--dest-dir", "remote", "--data-session-reuse", "tree",
                           "--data-pending-window", "4"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(valid)), valid, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().dataPendingWindow, 4U);

    const char* invalid[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                             "--dest-dir", "remote", "--data-session-reuse", "tree",
                             "--data-pending-window", "17"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(invalid)), invalid,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());
    std::filesystem::remove_all(root);
}
TEST(TreeTransferOptionsTest, BoundsPersistentChannelCount) {
    const auto root = std::filesystem::temp_directory_path() / "cpnetflux-tree-options-channels";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    for (const char* count : {"1", "4", "8"}) {
        const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                              "--dest-dir", "remote", "--data-session-reuse", "tree",
                              "--file-parallelism", count};
        auto parsed = cpnetflux::config::parseTreeTransferOptions(
            static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
        ASSERT_TRUE(parsed.isOk()) << count << ": " << parsed.status().message();
        EXPECT_EQ(parsed.value().fileParallelism, static_cast<std::uint32_t>(count[0] - '0'));
    }
    const char* tooMany[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                             "--dest-dir", "remote", "--data-session-reuse", "tree",
                             "--file-parallelism", "9"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(tooMany)), tooMany,
        cpnetflux::config::TreeTransferRole::Upload).isOk());
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, ParsesControlPipelineDepthAndRejectsInvalidCombinations) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-pipeline-root";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* valid[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                           "--dest-dir", "remote", "--control-reuse", "worker",
                           "--control-pipeline-depth", "1"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(valid)), valid, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().controlPipelineDepth, 1U);

    const char* tooDeep[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                             "--dest-dir", "remote", "--control-pipeline-depth", "2"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(tooDeep)), tooDeep,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());
    const char* wrongMode[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                               "--dest-dir", "remote", "--control-pipeline-depth", "1"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(wrongMode)), wrongMode,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    for (const char* depth : {"0", "1", "2", "4"}) {
        const char* supported[] = {"cpnetflux-tree-upload-client", "--source-dir",
                                   rootText.c_str(), "--dest-dir", "remote",
                                   "--control-reuse", "worker", "--scheduler", "off",
                                   "--control-pipeline-depth", depth};
        auto supportedParsed = cpnetflux::config::parseTreeTransferOptions(
            static_cast<int>(std::size(supported)), supported,
            cpnetflux::config::TreeTransferRole::Upload);
        ASSERT_TRUE(supportedParsed.isOk()) << depth << ": "
                                            << supportedParsed.status().message();
        EXPECT_EQ(supportedParsed.value().controlPipelineDepth,
                  static_cast<std::uint32_t>(depth[0] - '0'));
    }

    const char* unsupported[] = {"cpnetflux-tree-upload-client", "--source-dir",
                                 rootText.c_str(), "--dest-dir", "remote",
                                 "--control-reuse", "worker", "--scheduler", "off",
                                 "--control-pipeline-depth", "3"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(unsupported)), unsupported,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, ParsesTlsClientOptions) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-tls-root";
    const std::filesystem::path ca =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-ca.pem";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(ca);
        output << "not-a-real-ca\n";
    }
    const std::string rootText = root.string();
    const std::string caText = ca.string();
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--tls-mode", "required",
                          "--tls-ca-file", caText.c_str(), "--data-tls-mode", "required"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    if (cpnetflux::core::io::tlsSupportAvailable()) {
        ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
        EXPECT_EQ(parsed.value().tls.mode, cpnetflux::core::io::TlsMode::Required);
        EXPECT_EQ(parsed.value().tls.caFile, caText);
        EXPECT_EQ(parsed.value().dataTlsMode, cpnetflux::core::io::DataTlsMode::Required);
    } else {
        EXPECT_FALSE(parsed.isOk());
    }

    const char* explicitTls[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                                 "--dest-dir", "remote", "--tls-mode", "explicit"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(explicitTls)), explicitTls,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove(ca);
}

TEST(TreeTransferOptionsTest, ParsesTokenAuthOptions) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-token-root";
    const std::filesystem::path token =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-token.txt";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(token);
        output << "tree-token\n";
    }
    std::filesystem::permissions(token, std::filesystem::perms::owner_read |
                                            std::filesystem::perms::owner_write);
    const std::string rootText = root.string();
    const std::string tokenText = token.string();
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--auth-mode", "token",
                          "--auth-token-file", tokenText.c_str()};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().authMode, "token");
    EXPECT_EQ(parsed.value().authTokenFile, tokenText);

    const char* missingToken[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                                  "--dest-dir", "remote", "--auth-mode", "token"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(missingToken)), missingToken,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove(token);
}

TEST(TreeTransferOptionsTest, ParsesDownloadOptionsAndCreatesDestination) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-download";
    std::filesystem::remove_all(root);
    const std::string rootText = root.string();
    const char* argv[] = {"cpnetflux-tree-download-client",
                          "--source-dir",
                          "remote/data",
                          "--dest-dir",
                          rootText.c_str(),
                          "--resume",
                          "--summary-json",
                          "/tmp/tree-download-summary.json"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Download);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_TRUE(parsed.value().resume);
    EXPECT_EQ(parsed.value().jsonSummaryPath, "/tmp/tree-download-summary.json");
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, RejectsInvalidOptions) {
    const char* missing[] = {"cpnetflux-tree-upload-client"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     1, missing, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badParallelism[] = {"cpnetflux-tree-upload-client", "--source-dir", "/tmp",
                                    "--dest-dir", "remote", "--file-parallelism", "0"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     7, badParallelism, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badRemote[] = {"cpnetflux-tree-download-client", "--source-dir", "../escape",
                               "--dest-dir", "/tmp/out"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     5, badRemote, cpnetflux::config::TreeTransferRole::Download)
                     .isOk());

    const char* missingSummary[] = {"cpnetflux-tree-upload-client",
                                    "--source-dir",
                                    "/tmp",
                                    "--dest-dir",
                                    "remote",
                                    "--json-summary"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     6, missingSummary, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* missingEventLog[] = {"cpnetflux-tree-upload-client",
                                     "--source-dir",
                                     "/tmp",
                                     "--dest-dir",
                                     "remote",
                                     "--event-log"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     6, missingEventLog, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badAuth[] = {"cpnetflux-tree-upload-client", "--source-dir", "/tmp",
                             "--dest-dir", "remote", "--auth-mode", "oauth"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     7, badAuth, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badDataTls[] = {"cpnetflux-tree-upload-client", "--source-dir", "/tmp",
                                "--dest-dir", "remote", "--data-tls-mode", "maybe"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     7, badDataTls, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badReuse[] = {"cpnetflux-tree-upload-client", "--source-dir", "/tmp",
                              "--dest-dir", "remote", "--control-reuse", "session"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     7, badReuse, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* missingPreset[] = {"cpnetflux-tree-upload-client",
                                   "--source-dir",
                                   "/tmp",
                                   "--dest-dir",
                                   "remote",
                                   "--planner-preset"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     6, missingPreset, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badSchedulerMode[] = {"cpnetflux-tree-upload-client", "--source-dir", "/tmp",
                                      "--dest-dir", "remote", "--scheduler", "cluster"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     7, badSchedulerMode, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badSchedulerPolicy[] = {"cpnetflux-tree-upload-client", "--source-dir", "/tmp",
                                        "--dest-dir", "remote", "--scheduler-policy", "auto"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     7, badSchedulerPolicy, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badSchedulerBounds[] = {"cpnetflux-tree-upload-client",
                                        "--source-dir",
                                        "/tmp",
                                        "--dest-dir",
                                        "remote",
                                        "--scheduler-workitem-min-bytes",
                                        "16",
                                        "--scheduler-workitem-max-bytes",
                                        "8"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     9, badSchedulerBounds, cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());
}

TEST(TreeTransferOptionsTest, RejectsSchedulerMetricsInsideLocalRoot) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-scheduler-root";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path metrics = root / "scheduler-metrics";
    const std::string rootText = root.string();
    const std::string metricsText = metrics.string();
    const char* argv[] = {"cpnetflux-tree-upload-client",
                          "--source-dir",
                          rootText.c_str(),
                          "--dest-dir",
                          "remote",
                          "--scheduler",
                          "global",
                          "--scheduler-metrics-dir",
                          metricsText.c_str()};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(argv)), argv,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, RejectsPersistentDataCombinedWithControlPipeline) {
    const char* argv[] = {"cpnetflux-tree-upload-client", "--host", "127.0.0.1",
        "--port", "2121", "--source-dir", "/tmp", "--dest-dir", "remote",
        "--control-reuse", "worker", "--control-pipeline-depth", "1",
        "--data-session-reuse", "tree"};
    auto result = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv,
        cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.status().message().find("cannot combine"), std::string::npos);
}
TEST(TreeTransferOptionsTest, DefaultsPersistentDataPendingWindowToTwo) {
    const auto root = std::filesystem::temp_directory_path() / "cpnetflux-tree-options-default-window-root";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* args[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--data-session-reuse", "tree"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(args)), args, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().dataPendingWindow, 2U);
    std::filesystem::remove_all(root);
}
