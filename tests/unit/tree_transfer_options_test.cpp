#include "gridflux/config/tree_transfer_options.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "gridflux/core/io/tls_socket.h"

TEST(TreeTransferOptionsTest, ParsesUploadOptions) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gridflux-tree-options-upload";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* argv[] = {"gridflux-tree-upload-client",
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
                          "--data-final-status-timeout-seconds",
                          "7",
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
                          "gridflux_b1_control_reuse",
                          "--json-summary",
                          "/tmp/tree-summary.json",
                          "--event-log",
                          "/tmp/tree-events.jsonl"};
    auto parsed = gridflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, gridflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().destDir, "remote/data");
    EXPECT_EQ(parsed.value().connections, 4U);
    EXPECT_EQ(parsed.value().fileParallelism, 2U);
    EXPECT_EQ(parsed.value().chunkSize, 4194304U);
    EXPECT_EQ(parsed.value().dataFinalStatusTimeoutSeconds, 7U);
    EXPECT_EQ(parsed.value().bufferSize, 262144U);
    EXPECT_EQ(parsed.value().checksumAlgorithm, gridflux::checksum::ChecksumAlgorithm::None);
    EXPECT_EQ(parsed.value().authMode, "anonymous");
    EXPECT_EQ(parsed.value().tls.mode, gridflux::core::io::TlsMode::Off);
    EXPECT_EQ(parsed.value().dataTlsMode, gridflux::core::io::DataTlsMode::Off);
    EXPECT_EQ(parsed.value().controlReuseMode, gridflux::config::ControlReuseMode::Worker);
    EXPECT_EQ(gridflux::config::controlReuseModeName(parsed.value().controlReuseMode),
              std::string("worker"));
    EXPECT_EQ(parsed.value().plannerPreset, "gridflux_b1_control_reuse");
    EXPECT_EQ(parsed.value().user, "alice");
    EXPECT_EQ(parsed.value().jsonSummaryPath, "/tmp/tree-summary.json");
    EXPECT_EQ(parsed.value().eventLogPath, "/tmp/tree-events.jsonl");
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, DefaultsControlReuseOff) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gridflux-tree-options-default-reuse";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* argv[] = {"gridflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote/data"};
    auto parsed = gridflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, gridflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().controlReuseMode, gridflux::config::ControlReuseMode::Off);
    EXPECT_EQ(parsed.value().dataFinalStatusTimeoutSeconds, 60U);
    EXPECT_EQ(gridflux::config::controlReuseModeName(parsed.value().controlReuseMode),
              std::string("off"));
    EXPECT_TRUE(parsed.value().plannerPreset.empty());
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, ParsesTlsClientOptions) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gridflux-tree-options-tls-root";
    const std::filesystem::path ca =
        std::filesystem::temp_directory_path() / "gridflux-tree-options-ca.pem";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(ca);
        output << "not-a-real-ca\n";
    }
    const std::string rootText = root.string();
    const std::string caText = ca.string();
    const char* argv[] = {"gridflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--tls-mode", "required",
                          "--tls-ca-file", caText.c_str(), "--data-tls-mode", "required"};
    auto parsed = gridflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, gridflux::config::TreeTransferRole::Upload);
    if (gridflux::core::io::tlsSupportAvailable()) {
        ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
        EXPECT_EQ(parsed.value().tls.mode, gridflux::core::io::TlsMode::Required);
        EXPECT_EQ(parsed.value().tls.caFile, caText);
        EXPECT_EQ(parsed.value().dataTlsMode, gridflux::core::io::DataTlsMode::Required);
    } else {
        EXPECT_FALSE(parsed.isOk());
    }

    const char* explicitTls[] = {"gridflux-tree-upload-client", "--source-dir", rootText.c_str(),
                                 "--dest-dir", "remote", "--tls-mode", "explicit"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(explicitTls)), explicitTls,
                     gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove(ca);
}

TEST(TreeTransferOptionsTest, ParsesTokenAuthOptions) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gridflux-tree-options-token-root";
    const std::filesystem::path token =
        std::filesystem::temp_directory_path() / "gridflux-tree-options-token.txt";
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
    const char* argv[] = {"gridflux-tree-upload-client", "--source-dir", rootText.c_str(),
                          "--dest-dir", "remote", "--auth-mode", "token",
                          "--auth-token-file", tokenText.c_str()};
    auto parsed = gridflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, gridflux::config::TreeTransferRole::Upload);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().authMode, "token");
    EXPECT_EQ(parsed.value().authTokenFile, tokenText);

    const char* missingToken[] = {"gridflux-tree-upload-client", "--source-dir", rootText.c_str(),
                                  "--dest-dir", "remote", "--auth-mode", "token"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(missingToken)), missingToken,
                     gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove(token);
}

TEST(TreeTransferOptionsTest, ParsesDownloadOptionsAndCreatesDestination) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gridflux-tree-options-download";
    std::filesystem::remove_all(root);
    const std::string rootText = root.string();
    const char* argv[] = {"gridflux-tree-download-client",
                          "--source-dir",
                          "remote/data",
                          "--dest-dir",
                          rootText.c_str(),
                          "--resume",
                          "--summary-json",
                          "/tmp/tree-download-summary.json"};
    auto parsed = gridflux::config::parseTreeTransferOptions(
        static_cast<int>(std::size(argv)), argv, gridflux::config::TreeTransferRole::Download);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_TRUE(parsed.value().resume);
    EXPECT_EQ(parsed.value().jsonSummaryPath, "/tmp/tree-download-summary.json");
    std::filesystem::remove_all(root);
}

TEST(TreeTransferOptionsTest, RejectsInvalidOptions) {
    const char* missing[] = {"gridflux-tree-upload-client"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     1, missing, gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badParallelism[] = {"gridflux-tree-upload-client", "--source-dir", "/tmp",
                                    "--dest-dir", "remote", "--file-parallelism", "0"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     7, badParallelism, gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badRemote[] = {"gridflux-tree-download-client", "--source-dir", "../escape",
                               "--dest-dir", "/tmp/out"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     5, badRemote, gridflux::config::TreeTransferRole::Download)
                     .isOk());

    const char* missingSummary[] = {"gridflux-tree-upload-client",
                                    "--source-dir",
                                    "/tmp",
                                    "--dest-dir",
                                    "remote",
                                    "--json-summary"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     6, missingSummary, gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* missingEventLog[] = {"gridflux-tree-upload-client",
                                     "--source-dir",
                                     "/tmp",
                                     "--dest-dir",
                                     "remote",
                                     "--event-log"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     6, missingEventLog, gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badAuth[] = {"gridflux-tree-upload-client", "--source-dir", "/tmp",
                             "--dest-dir", "remote", "--auth-mode", "oauth"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     7, badAuth, gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badDataTls[] = {"gridflux-tree-upload-client", "--source-dir", "/tmp",
                                "--dest-dir", "remote", "--data-tls-mode", "maybe"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     7, badDataTls, gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badReuse[] = {"gridflux-tree-upload-client", "--source-dir", "/tmp",
                              "--dest-dir", "remote", "--control-reuse", "session"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     7, badReuse, gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* badFinalStatusTimeout[] = {
        "gridflux-tree-upload-client", "--source-dir", "/tmp", "--dest-dir", "remote",
        "--data-final-status-timeout-seconds", "0"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     static_cast<int>(std::size(badFinalStatusTimeout)),
                     badFinalStatusTimeout, gridflux::config::TreeTransferRole::Upload)
                     .isOk());

    const char* missingPreset[] = {"gridflux-tree-upload-client",
                                   "--source-dir",
                                   "/tmp",
                                   "--dest-dir",
                                   "remote",
                                   "--planner-preset"};
    EXPECT_FALSE(gridflux::config::parseTreeTransferOptions(
                     6, missingPreset, gridflux::config::TreeTransferRole::Upload)
                     .isOk());
}
