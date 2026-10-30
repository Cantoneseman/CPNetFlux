#include "cpnetflux/config/tree_transfer_options.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/core/io/persistent_tree_transfer.h"

TEST(TreeTransferOptionsTest, AuthProbeDoesNotRequireTransferRoots) {
    const char* argv[] = {"cpnetflux-auth-probe", "--auth-probe",
                          "--host", "127.0.0.1", "--port", "2121",
                          "--auth-mode", "anonymous", "--tls-mode", "off",
                          "--data-tls-mode", "off"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv,
        cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_TRUE(parsed.value().authProbe);
    EXPECT_TRUE(parsed.value().sourceDir.empty());
    EXPECT_TRUE(parsed.value().destDir.empty());
}

TEST(TreeTransferOptionsTest, ParsesRdmaDataTransportForV3) {
    const char* argv[] = {"cpnetflux-auth-probe", "--auth-probe", "--host", "127.0.0.1",
                          "--data-transport", "rdma", "--data-tls-mode", "off"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().dataTransport, cpnetflux::core::io::DataTransport::Rdma);
    EXPECT_EQ(parsed.value().dataTlsMode, cpnetflux::core::io::DataTlsMode::Off);
}

TEST(TreeTransferOptionsTest, RejectsRdmaDataTransportWithDataTls) {
    const char* argv[] = {"cpnetflux-auth-probe", "--auth-probe", "--host", "127.0.0.1",
                          "--data-transport", "rdma", "--data-tls-mode", "required"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_FALSE(parsed.isOk());
    EXPECT_NE(parsed.status().message().find("cannot be combined with data TLS"), std::string::npos);
}

TEST(TreeTransferOptionsTest, ParsesTlsServerName) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-tls-name";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* argv[] = {"cpnetflux-tree-upload-client", "--host", "192.0.2.10",
                          "--port", "2121", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--tls-mode", "required",
                          "--tls-server-name", "node.example"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv,
        cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().tlsServerName, "node.example");
    std::filesystem::remove_all(root);
}

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
    EXPECT_TRUE(parsed.value().requestedReuseDataSession);
    EXPECT_TRUE(parsed.value().reuseDataSession);
    EXPECT_TRUE(parsed.value().modeFallbackReason.empty());
}

TEST(TreeTransferOptionsTest, ExplicitPersistentTreePipelineUsesSharedDispatchWindow) {
    const auto root = std::filesystem::temp_directory_path() / "cpnetflux-tree-options-explicit-pipeline";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto rootText = root.string();
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--control-reuse", "worker",
                          "--control-pipeline-depth", "1", "--data-session-reuse", "tree"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_TRUE(parsed.value().reuseDataSession);
    EXPECT_TRUE(parsed.value().requestedReuseDataSession);
    EXPECT_EQ(parsed.value().controlPipelineDepth, 1U);
    EXPECT_TRUE(parsed.value().modeFallbackReason.empty());
    std::filesystem::remove_all(root);
}
TEST(TreeTransferOptionsTest, PersistentPipelineRequiresDynamicQueueScheduling) {
    const auto root = std::filesystem::temp_directory_path() / "cpnetflux-tree-options-pipeline-static";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto rootText = root.string();
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--data-session-reuse", "tree",
                          "--file-scheduling", "static", "--control-pipeline-depth", "1"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    EXPECT_FALSE(parsed.isOk());
    EXPECT_NE(parsed.status().message().find("requires dynamic scheduling"), std::string::npos);
    std::filesystem::remove_all(root);
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
    for (const char* count : {"1", "2", "4", "8", "10"}) {
        const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                              "--dest-dir", "remote", "--data-session-reuse", "tree",
                              "--file-parallelism", count};
        auto parsed = cpnetflux::config::parseTreeTransferOptions(
            static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
        ASSERT_TRUE(parsed.isOk()) << count << ": " << parsed.status().message();
        EXPECT_EQ(parsed.value().fileParallelism, static_cast<std::uint32_t>(std::stoul(count)));
    }
    const char* tooMany[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                             "--dest-dir", "remote", "--data-session-reuse", "tree",
                             "--file-parallelism", "11"};
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

    const char* validLegacyAlias[] = {"cpnetflux-tree-upload-client", "--source-dir",
                                      rootText.c_str(), "--dest-dir", "remote",
                                      "--data-session-reuse", "off",
                                      "--control-pipeline-depth", "2"};
    auto aliasParsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(validLegacyAlias)), validLegacyAlias,
        cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(aliasParsed.isOk()) << aliasParsed.status().message();
    EXPECT_TRUE(aliasParsed.value().reuseDataSession);
    EXPECT_EQ(aliasParsed.value().controlPipelineDepth, 2U);
    const char* wrongMode[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                               "--dest-dir", "remote", "--data-session-reuse", "off",
                               "--file-scheduling", "static", "--control-pipeline-depth", "1"};
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
    const std::filesystem::path cert =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-cert.pem";
    const std::filesystem::path key =
        std::filesystem::temp_directory_path() / "cpnetflux-tree-options-key.pem";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    for (const auto& path : {ca, cert, key}) {
        std::ofstream output(path);
        output << "test fixture\n";
    }
    std::filesystem::permissions(key, std::filesystem::perms::owner_read |
                                       std::filesystem::perms::owner_write);
    const std::string rootText = root.string();
    const std::string caText = ca.string();
    const std::string certText = cert.string();
    const std::string keyText = key.string();
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--tls-mode", "required",
                          "--tls-ca-file", caText.c_str(), "--tls-cert-file",
                          certText.c_str(), "--tls-key-file", keyText.c_str(),
                          "--data-tls-mode", "required"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    if (cpnetflux::core::io::tlsSupportAvailable()) {
        ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
        EXPECT_EQ(parsed.value().tls.mode, cpnetflux::core::io::TlsMode::Required);
        EXPECT_EQ(parsed.value().tls.caFile, caText);
        EXPECT_EQ(parsed.value().tls.certFile, certText);
        EXPECT_EQ(parsed.value().tls.keyFile, keyText);
        EXPECT_EQ(parsed.value().dataTlsMode, cpnetflux::core::io::DataTlsMode::Required);
    } else {
        EXPECT_FALSE(parsed.isOk());
        std::filesystem::remove(ca);
        std::filesystem::remove(cert);
        std::filesystem::remove(key);
    }

    const char* explicitTls[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                                 "--dest-dir", "remote", "--tls-mode", "explicit"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(explicitTls)), explicitTls,
                     cpnetflux::config::TreeTransferRole::Upload)
                     .isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove(ca);
    std::filesystem::remove(cert);
    std::filesystem::remove(key);
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

TEST(TreeTransferOptionsTest, AllowsPersistentDataWithSharedPipelineDispatch) {
    const char* argv[] = {"cpnetflux-tree-upload-client", "--host", "127.0.0.1",
        "--port", "2121", "--source-dir", "/tmp", "--dest-dir", "remote",
        "--control-reuse", "worker", "--control-pipeline-depth", "2",
        "--data-session-reuse", "tree", "--file-scheduling", "dynamic"};
    auto result = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv,
        cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_TRUE(result.value().reuseDataSession);
    EXPECT_TRUE(result.value().requestedReuseDataSession);
    EXPECT_EQ(result.value().controlPipelineDepth, 2U);
    EXPECT_TRUE(result.value().dynamicFileScheduling);
    EXPECT_TRUE(result.value().modeFallbackReason.empty());
}
TEST(TreeTransferOptionsTest, DefaultsToV3PersistentDynamicDispatch) {
    const auto root = std::filesystem::temp_directory_path() / "cpnetflux-tree-options-v3-defaults";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* args[] = {"cpnetflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(args)), args, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_TRUE(parsed.value().reuseDataSession);
    EXPECT_TRUE(parsed.value().requestedReuseDataSession);
    EXPECT_TRUE(parsed.value().dynamicFileScheduling);
    EXPECT_EQ(parsed.value().dataPendingWindow, 2U);
    EXPECT_EQ(parsed.value().checksumAlgorithm, cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    EXPECT_EQ(parsed.value().controlPipelineDepth, 0U);
    EXPECT_EQ(parsed.value().schedulerMode, cpnetflux::config::TreeSchedulerMode::Off);
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, LegacyReuseOffInputMapsToV3PersistentExecutor) {
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", "/tmp",
                          "--dest-dir", "remote", "--data-session-reuse", "off",
                          "--file-scheduling", "static"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv,
        cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_TRUE(parsed.value().reuseDataSession);
    EXPECT_FALSE(parsed.value().dynamicFileScheduling);
    EXPECT_TRUE(parsed.value().modeFallbackReason.empty());
}

TEST(TreeTransferOptionsTest, RejectsLegacyPerFileMaxFilesLimit) {
    const char* argv[] = {"cpnetflux-tree-upload-client", "--source-dir", "/tmp",
                          "--dest-dir", "remote", "--max-files", "1"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv,
        cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_FALSE(parsed.isOk());
    EXPECT_NE(parsed.status().message().find("not supported by the V3 tree executor"),
              std::string::npos);
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

TEST(TreeTransferOptionsTest, AcceptsDynamicFileSchedulingForPersistentTree) {
    const char* argv[] = {"client", "--source-dir", "remote", "--dest-dir", "/tmp/cpnetflux-dynamic-option",
        "--data-session-reuse", "tree", "--checksum", "none", "--file-scheduling", "dynamic"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Download);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
}

TEST(TreeTransferOptionsTest, AllowsGlobalSchedulerWithPersistentSharedDispatch) {
    const char* argv[] = {"cpnetflux-tree-upload-client", "--host", "127.0.0.1",
        "--port", "2121", "--source-dir", "/tmp", "--dest-dir", "remote",
        "--control-reuse", "worker", "--control-pipeline-depth", "2",
        "--data-session-reuse", "tree", "--file-scheduling", "dynamic",
        "--scheduler", "global", "--scheduler-policy", "adaptive",
        "--scheduler-metrics-dir", "/var/tmp/cpnetflux-stage02-metrics"};
    auto result = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv,
        cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().schedulerMode, cpnetflux::config::TreeSchedulerMode::Global);
    EXPECT_EQ(result.value().schedulerPolicy, cpnetflux::core::scheduler::SchedulerPolicy::Adaptive);
    EXPECT_TRUE(result.value().reuseDataSession);
    EXPECT_TRUE(result.value().dynamicFileScheduling);
}

TEST(TreeTransferOptionsTest, RejectsGlobalSchedulerWithPersistentStaticDispatch) {
    const char* argv[] = {"cpnetflux-tree-upload-client", "--host", "127.0.0.1",
        "--port", "2121", "--source-dir", "/tmp", "--dest-dir", "remote",
        "--data-session-reuse", "tree", "--file-scheduling", "static",
        "--scheduler", "global", "--scheduler-metrics-dir", "/var/tmp/cpnetflux-stage02-static"};
    auto result = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv,
        cpnetflux::config::TreeTransferRole::Upload);
    EXPECT_FALSE(result.isOk());
}

TEST(TreeTransferOptionsTest, ReconnectBudgetAndTotalDeadlineAreBounded) {
    const char* valid[] = {"client", "--auth-probe", "--reconnect-attempts", "2"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(4, valid, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().reconnectAttempts, 2U);
    EXPECT_EQ(parsed.value().transferTimeoutMs, 300000U);
    for (const auto value : {"-1", "11", "abc"}) {
        const char* invalid[] = {"client", "--auth-probe", "--reconnect-attempts", value};
        EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(4, invalid, cpnetflux::config::TreeTransferRole::Upload).isOk());
    }
    for (const auto value : {"0", "86400001", "-1"}) {
        const char* invalid[] = {"client", "--auth-probe", "--transfer-timeout-ms", value};
        EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(4, invalid, cpnetflux::config::TreeTransferRole::Upload).isOk());
    }
    const char* incompatible[] = {"client", "--auth-probe", "--reconnect-attempts", "1", "--file-scheduling", "static"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(6, incompatible, cpnetflux::config::TreeTransferRole::Upload).isOk());
}

TEST(TreeTransferOptionsTest, AutomaticReconnectRejectsUnverifiedCheckpointMode) {
    const char* args[] = {"client", "--auth-probe", "--reconnect-attempts", "1", "--checksum", "none"};
    EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(6, args, cpnetflux::config::TreeTransferRole::Upload).isOk());
}

TEST(TreeTransferOptionsTest, ParsesExplicitLinkSetNegotiationOptions) {
    const char* args[] = {"client", "--auth-probe", "--link-set-links", "4",
                          "--link-set-capabilities", "9"};
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(args)), args, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().linkSetLinks, 4U);
    EXPECT_EQ(parsed.value().linkSetCapabilityBits, 9U);

    for (const auto value : {"0", "11", "abc"}) {
        const char* invalid[] = {"client", "--auth-probe", "--link-set-links", value};
        EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
            4, invalid, cpnetflux::config::TreeTransferRole::Upload).isOk());
    }
}

TEST(TreeTransferOptionsTest, AcceptsTenPersistentChannelsAndRejectsEleven) {
    for (const auto role : {cpnetflux::config::TreeTransferRole::Upload,
                            cpnetflux::config::TreeTransferRole::Download}) {
        const char* args[] = {"client", "--auth-probe", "--data-session-reuse", "tree",
                              "--file-parallelism", "10", "--link-set-links", "10"};
        auto parsed = cpnetflux::config::parseTreeTransferOptions(
            static_cast<int>(std::size(args)), args, role);
        ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
        EXPECT_EQ(parsed.value().fileParallelism, 10U);
        EXPECT_EQ(parsed.value().linkSetLinks, 10U);
        EXPECT_TRUE(parsed.value().reuseDataSession);
        for (const auto count : {"0", "11"}) {
            const char* invalid[] = {"client", "--auth-probe",
                                     "--file-parallelism", count};
            EXPECT_FALSE(cpnetflux::config::parseTreeTransferOptions(
                static_cast<int>(std::size(invalid)), invalid, role).isOk());
        }
    }
}


TEST(TreeTransferOptionsTest, ParsesNumericPeerAddresses) {
    const char* argv[] = {"client", "--auth-probe", "--link-set-links", "2",
                          "--peer-addresses", "127.0.0.2,::1"};
    auto result = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().peerAddresses, (std::vector<std::string>{"127.0.0.2", "::1"}));
}

TEST(TreeTransferOptionsTest, RejectsInvalidPeerAddresses) {
    for (const char* value : {"", "peer.example", "127.0.0.1,", "127.0.0.1,,127.0.0.2",
                              "127.0.0.1,127.0.0.2,127.0.0.3"}) {
        const char* argv[] = {"client", "--auth-probe", "--link-set-links", "2",
                              "--peer-addresses", value};
        auto result = cpnetflux::config::parseTreeTransferOptions(
            static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
        EXPECT_FALSE(result.isOk()) << value;
    }
}

TEST(TreeTransferOptionsTest, ParsesNumericLocalBindAddresses) {
    const char* argv[] = {
        "cpnetflux-tree-upload-client",
        "--source-dir", "/tmp",
        "--dest-dir", "remote",
        "--local-bind-addresses", "192.0.2.10,2001:db8::10",
        "--link-set-links", "2"
    };
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    ASSERT_EQ(parsed.value().localBindAddresses.size(), 2U);
    EXPECT_EQ(parsed.value().localBindAddresses[0], "192.0.2.10");
    EXPECT_EQ(parsed.value().localBindAddresses[1], "2001:db8::10");
}

TEST(TreeTransferOptionsTest, RejectsNonNumericLocalBindAddress) {
    const char* argv[] = {
        "cpnetflux-tree-upload-client",
        "--source-dir", "/tmp",
        "--dest-dir", "remote",
        "--local-bind-address", "eth1"
    };
    auto parsed = cpnetflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, cpnetflux::config::TreeTransferRole::Upload);
    ASSERT_FALSE(parsed.isOk());
}

TEST(TreeTransferOptionsTest, ExportsServerRdmaReceiveQueueObservations) {
    cpnetflux::core::io::PersistentFileTiming timing;
    timing.rdmaReceiveObservationAvailable = true;
    timing.rdmaTransportStats.receiveBytes = 65536;
    timing.rdmaTransportStats.negotiatedReceiveQueueDepth = 128;
    timing.rdmaTransportStats.negotiatedPathMtuBytes = 1024;
    timing.rdmaTransportStats.negotiatedMinRnrTimer = 12;
    timing.rdmaTransportStats.negotiatedRnrRetry = 7;
    timing.rdmaTransportStats.receiveCompletions = 1;
    timing.rdmaTransportStats.receivePostCalls = 129;
    timing.rdmaTransportStats.receiveReposts = 1;
    timing.rdmaTransportStats.receiveCompletionPollCalls = 8193;
    timing.rdmaTransportStats.receiveCompletionEmptyPollCalls = 8192;
    timing.rdmaTransportStats.receiveCompletionEventWaits = 12;
    timing.rdmaTransportStats.receiveCompletionWaitCount = 1;
    timing.rdmaTransportStats.receiveCompletionWaitNanoseconds = 250000;
    std::ostringstream json;
    cpnetflux::core::io::appendPersistentFileTimings(
        json, std::vector<cpnetflux::core::io::PersistentFileTiming>{timing});
    EXPECT_NE(json.str().find("\"rdma_receive_observation\""), std::string::npos);
    EXPECT_NE(json.str().find("\"negotiated_receive_queue_depth\":128"), std::string::npos);
    EXPECT_NE(json.str().find("\"negotiated_path_mtu_bytes\":1024"), std::string::npos);
    EXPECT_NE(json.str().find("\"negotiated_min_rnr_timer\":12"), std::string::npos);
    EXPECT_NE(json.str().find("\"negotiated_rnr_retry\":7"), std::string::npos);
    EXPECT_NE(json.str().find("\"receive_completion_empty_poll_calls\":8192"), std::string::npos);
    EXPECT_NE(json.str().find("\"receive_completion_event_waits\":12"), std::string::npos);
    EXPECT_NE(json.str().find("\"receive_completion_wait_seconds\":0.00025"), std::string::npos);
}
