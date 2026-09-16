#include "cpnetflux/protocol/control/control_options.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cpnetflux/storage/file_io.h"
#include "cpnetflux/core/io/tls_socket.h"

namespace {

using cpnetflux::checksum::ChecksumAlgorithm;
using cpnetflux::checksum::ChecksumBackend;
using cpnetflux::core::session::CommitSyncPolicy;
using cpnetflux::core::session::FinalVerifyPolicy;
using cpnetflux::core::session::ManifestFlushPolicy;
using cpnetflux::protocol::control::ControlListEntry;
using cpnetflux::protocol::control::ControlPathKind;
using cpnetflux::protocol::control::AuthMode;
using cpnetflux::protocol::control::kMaxPassiveDataPortBase;
using cpnetflux::protocol::control::formatList;
using cpnetflux::protocol::control::formatMdtmTime;
using cpnetflux::protocol::control::formatNlst;
using cpnetflux::protocol::control::parseControlServerOptions;
using cpnetflux::protocol::control::resolveControlPath;
using cpnetflux::protocol::control::resolveRetrPath;
using cpnetflux::protocol::control::resolveStorPath;
using cpnetflux::protocol::control::resolveVirtualPath;
using cpnetflux::core::io::DataTlsMode;
using cpnetflux::core::io::TlsMode;

TEST(ControlOptionsTest, ParsesDefaultsAndRequiredRoot) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-options-root";
    std::filesystem::create_directories(root);

    const std::string rootText = root.string();
    const char* argv[] = {"cpnetflux-gridftp-server", "--root", rootText.c_str()};
    auto parsed = parseControlServerOptions(3, argv);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().host, "127.0.0.1");
    EXPECT_EQ(parsed.value().port, 2121);
    EXPECT_EQ(parsed.value().dataPortBase, 20200);
    EXPECT_EQ(parsed.value().connections, 1U);
    EXPECT_EQ(parsed.value().checksumAlgorithm, ChecksumAlgorithm::Crc32c);
    EXPECT_EQ(parsed.value().checksumBackend, ChecksumBackend::Auto);
    EXPECT_EQ(parsed.value().manifestFlushPolicy, ManifestFlushPolicy::EveryNChunks);
    EXPECT_EQ(parsed.value().manifestFlushIntervalChunks, 16U);
    EXPECT_EQ(parsed.value().finalVerifyPolicy, FinalVerifyPolicy::Full);
    EXPECT_EQ(parsed.value().commitSyncPolicy, CommitSyncPolicy::None);
    EXPECT_EQ(parsed.value().preallocateMode, cpnetflux::storage::PreallocateMode::Off);
    EXPECT_EQ(parsed.value().fileIo.backend, cpnetflux::storage::FileIoBackendKind::Posix);
    EXPECT_EQ(parsed.value().fileIo.bufferSize, 0U);
    EXPECT_EQ(parsed.value().fileIo.advice, cpnetflux::storage::FileIoAdvice::Off);
    EXPECT_EQ(parsed.value().fileIo.posixWriteStrategy,
              cpnetflux::storage::PosixWriteStrategy::Auto);
    EXPECT_EQ(parsed.value().auth.mode, AuthMode::Anonymous);
    EXPECT_EQ(parsed.value().tls.mode, TlsMode::Off);
    EXPECT_EQ(parsed.value().dataTlsMode, DataTlsMode::Off);
    EXPECT_EQ(parsed.value().user, "cpnetflux");
    EXPECT_TRUE(parsed.value().eventLogPath.empty());

    std::filesystem::remove_all(root);
}

TEST(ControlOptionsTest, ParsesAndValidatesDataTlsMode) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-data-tls-root";
    const std::filesystem::path cert =
        std::filesystem::temp_directory_path() / "cpnetflux-control-data-tls-cert.pem";
    const std::filesystem::path key =
        std::filesystem::temp_directory_path() / "cpnetflux-control-data-tls-key.pem";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(cert);
        output << "not-a-real-cert\n";
    }
    {
        std::ofstream output(key);
        output << "not-a-real-key\n";
    }
    std::filesystem::permissions(key, std::filesystem::perms::owner_read |
                                          std::filesystem::perms::owner_write);

    const std::string rootText = root.string();
    const std::string certText = cert.string();
    const std::string keyText = key.string();
    const char* valid[] = {"cpnetflux-gridftp-server",
                           "--root",
                           rootText.c_str(),
                           "--tls-mode",
                           "required",
                           "--tls-cert-file",
                           certText.c_str(),
                           "--tls-key-file",
                           keyText.c_str(),
                           "--data-tls-mode",
                           "required"};
    auto parsed = parseControlServerOptions(static_cast<int>(std::size(valid)), valid);
    if (cpnetflux::core::io::tlsSupportAvailable()) {
        ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
        EXPECT_EQ(parsed.value().tls.mode, TlsMode::Required);
        EXPECT_EQ(parsed.value().dataTlsMode, DataTlsMode::Required);
    } else {
        EXPECT_FALSE(parsed.isOk());
    }

    const char* missingControlTls[] = {"cpnetflux-gridftp-server", "--root", rootText.c_str(),
                                       "--data-tls-mode", "required"};
    EXPECT_FALSE(parseControlServerOptions(static_cast<int>(std::size(missingControlTls)),
                                           missingControlTls)
                     .isOk());

    const char* badDataTls[] = {"cpnetflux-gridftp-server", "--root", rootText.c_str(),
                                "--data-tls-mode", "maybe"};
    EXPECT_FALSE(parseControlServerOptions(static_cast<int>(std::size(badDataTls)), badDataTls)
                     .isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove(cert);
    std::filesystem::remove(key);
}

TEST(ControlOptionsTest, ParsesTlsOptionsAndRejectsUnsafeKeys) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-tls-root";
    const std::filesystem::path cert =
        std::filesystem::temp_directory_path() / "cpnetflux-control-tls-cert.pem";
    const std::filesystem::path key =
        std::filesystem::temp_directory_path() / "cpnetflux-control-tls-key.pem";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(cert);
        output << "not-a-real-cert\n";
    }
    {
        std::ofstream output(key);
        output << "not-a-real-key\n";
    }
    std::filesystem::permissions(key, std::filesystem::perms::owner_read |
                                          std::filesystem::perms::owner_write);

    const std::string rootText = root.string();
    const std::string certText = cert.string();
    const std::string keyText = key.string();
    const char* argv[] = {"cpnetflux-gridftp-server",
                          "--root",
                          rootText.c_str(),
                          "--tls-mode",
                          "required",
                          "--tls-cert-file",
                          certText.c_str(),
                          "--tls-key-file",
                          keyText.c_str()};
    auto parsed = parseControlServerOptions(static_cast<int>(std::size(argv)), argv);
    if (cpnetflux::core::io::tlsSupportAvailable()) {
        ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
        EXPECT_EQ(parsed.value().tls.mode, TlsMode::Required);
        EXPECT_EQ(parsed.value().tls.certFile, certText);
        EXPECT_EQ(parsed.value().tls.keyFile, keyText);
    } else {
        EXPECT_FALSE(parsed.isOk());
    }

    std::filesystem::permissions(key, std::filesystem::perms::owner_read |
                                          std::filesystem::perms::owner_write |
                                          std::filesystem::perms::group_read);
    EXPECT_FALSE(parseControlServerOptions(static_cast<int>(std::size(argv)), argv).isOk());

    const char* explicitTls[] = {"cpnetflux-gridftp-server", "--root", rootText.c_str(),
                                 "--tls-mode", "explicit"};
    EXPECT_FALSE(parseControlServerOptions(static_cast<int>(std::size(explicitTls)), explicitTls)
                     .isOk());

    const char* badTls[] = {"cpnetflux-gridftp-server", "--root", rootText.c_str(), "--tls-mode",
                            "yes"};
    EXPECT_FALSE(parseControlServerOptions(static_cast<int>(std::size(badTls)), badTls).isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove(cert);
    std::filesystem::remove(key);
}

TEST(ControlOptionsTest, ParsesTokenAuthAndRejectsBadTokenFiles) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-token-root";
    const std::filesystem::path token =
        std::filesystem::temp_directory_path() / "cpnetflux-control-token.txt";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(token);
        output << "alpha-token\n";
    }
    std::filesystem::permissions(token, std::filesystem::perms::owner_read |
                                            std::filesystem::perms::owner_write);
    const std::string rootText = root.string();
    const std::string tokenText = token.string();
    const char* argv[] = {"cpnetflux-gridftp-server", "--root", rootText.c_str(),
                          "--auth-mode", "token", "--auth-token-file", tokenText.c_str()};
    auto parsed = parseControlServerOptions(static_cast<int>(std::size(argv)), argv);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().auth.mode, AuthMode::Token);
    EXPECT_EQ(parsed.value().auth.token, "alpha-token");
    EXPECT_EQ(parsed.value().user, "token");

    const char* missingFile[] = {"cpnetflux-gridftp-server", "--root", rootText.c_str(),
                                 "--auth-mode", "token"};
    EXPECT_FALSE(parseControlServerOptions(static_cast<int>(std::size(missingFile)), missingFile)
                     .isOk());

    {
        std::ofstream output(token, std::ios::trunc);
        output << "open-token";
    }
    std::filesystem::permissions(token, std::filesystem::perms::owner_read |
                                            std::filesystem::perms::owner_write |
                                            std::filesystem::perms::group_read);
    EXPECT_FALSE(parseControlServerOptions(static_cast<int>(std::size(argv)), argv).isOk());

    {
        std::ofstream output(token, std::ios::trunc);
        output << "\n";
    }
    std::filesystem::permissions(token, std::filesystem::perms::owner_read |
                                            std::filesystem::perms::owner_write);
    EXPECT_FALSE(parseControlServerOptions(static_cast<int>(std::size(argv)), argv).isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove(token);
}

TEST(ControlOptionsTest, ParsesExplicitOptions) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-options-explicit";
    std::filesystem::create_directories(root);
    const std::string rootText = root.string();
    const char* argv[] = {"cpnetflux-gridftp-server",
                          "--root",
                          rootText.c_str(),
                          "--host",
                          "0.0.0.0",
                          "--port",
                          "2221",
                          "--data-port-base",
                          "20300",
                          "--connections",
                          "8",
                          "--chunk-size",
                          "4194304",
                          "--buffer-size",
                          "131072",
                          "--checksum",
                          "none",
                          "--checksum-backend",
                          "software",
                          "--manifest-flush-policy",
                          "final_only",
                          "--manifest-flush-interval-chunks",
                          "32",
                          "--final-verify-policy",
                          "verified_chunks",
                          "--commit-sync-policy",
                          "fsync_file",
                          "--preallocate",
                          "full",
                          "--file-io-backend",
                          "io_uring",
                          "--file-io-buffer-size",
                          "1048576",
                          "--file-io-queue-depth",
                          "8",
                          "--file-io-advice",
                          "noreuse",
                          "--posix-write-strategy",
                          "coalesced",
                          "--event-log",
                          "/tmp/cpnetflux-control-events.jsonl",
                          "--user",
                          "alice",
                          "--password",
                          "secret"};
    auto parsed = parseControlServerOptions(static_cast<int>(std::size(argv)), argv);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().host, "0.0.0.0");
    EXPECT_EQ(parsed.value().port, 2221);
    EXPECT_EQ(parsed.value().dataPortBase, 20300);
    EXPECT_EQ(parsed.value().connections, 8U);
    EXPECT_EQ(parsed.value().chunkSize, 4194304U);
    EXPECT_EQ(parsed.value().bufferSize, 131072U);
    EXPECT_EQ(parsed.value().checksumAlgorithm, ChecksumAlgorithm::None);
    EXPECT_EQ(parsed.value().checksumBackend, ChecksumBackend::Software);
    EXPECT_EQ(parsed.value().manifestFlushPolicy, ManifestFlushPolicy::FinalOnly);
    EXPECT_EQ(parsed.value().manifestFlushIntervalChunks, 32U);
    EXPECT_EQ(parsed.value().finalVerifyPolicy, FinalVerifyPolicy::VerifiedChunks);
    EXPECT_EQ(parsed.value().commitSyncPolicy, CommitSyncPolicy::FsyncFile);
    EXPECT_EQ(parsed.value().preallocateMode, cpnetflux::storage::PreallocateMode::Full);
    EXPECT_EQ(parsed.value().fileIo.backend, cpnetflux::storage::FileIoBackendKind::IoUring);
    EXPECT_EQ(parsed.value().fileIo.bufferSize, 1048576U);
    EXPECT_EQ(parsed.value().fileIo.queueDepth, 8U);
    EXPECT_EQ(parsed.value().fileIo.batchSize, 8U);
    EXPECT_EQ(parsed.value().fileIo.advice, cpnetflux::storage::FileIoAdvice::Noreuse);
    EXPECT_EQ(parsed.value().fileIo.posixWriteStrategy,
              cpnetflux::storage::PosixWriteStrategy::Coalesced);
    EXPECT_EQ(parsed.value().user, "alice");
    EXPECT_EQ(parsed.value().eventLogPath, "/tmp/cpnetflux-control-events.jsonl");

    std::filesystem::remove_all(root);
}

TEST(ControlOptionsTest, RejectsInvalidOptions) {
    const char* missingRoot[] = {"cpnetflux-gridftp-server"};
    EXPECT_FALSE(parseControlServerOptions(1, missingRoot).isOk());

    const char* badConnections[] = {"cpnetflux-gridftp-server", "--root", "/tmp", "--connections",
                                    "65"};
    EXPECT_FALSE(parseControlServerOptions(5, badConnections).isOk());

    const std::string maxDataPortBase = std::to_string(kMaxPassiveDataPortBase);
    const char* maxPassiveWindowBase[] = {"cpnetflux-gridftp-server",
                                          "--root",
                                          "/tmp",
                                          "--data-port-base",
                                          maxDataPortBase.c_str()};
    auto maxPassive = parseControlServerOptions(static_cast<int>(std::size(maxPassiveWindowBase)),
                                                maxPassiveWindowBase);
    ASSERT_TRUE(maxPassive.isOk()) << maxPassive.status().message();
    EXPECT_EQ(maxPassive.value().dataPortBase, kMaxPassiveDataPortBase);

    const char* badDataPortBase[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                     "--data-port-base", "65025"};
    EXPECT_FALSE(parseControlServerOptions(5, badDataPortBase).isOk());

    const char* badBackend[] = {"cpnetflux-gridftp-server", "--root", "/tmp", "--checksum-backend",
                                "fast"};
    EXPECT_FALSE(parseControlServerOptions(5, badBackend).isOk());

    const char* badFinalVerify[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                    "--final-verify-policy", "fast"};
    EXPECT_FALSE(parseControlServerOptions(5, badFinalVerify).isOk());

    const char* badFlushPolicy[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                    "--manifest-flush-policy", "sometimes"};
    EXPECT_FALSE(parseControlServerOptions(5, badFlushPolicy).isOk());

    const char* badCommitSync[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                   "--commit-sync-policy", "sync_everything"};
    EXPECT_FALSE(parseControlServerOptions(5, badCommitSync).isOk());

    const char* badPreallocate[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                    "--preallocate", "yes"};
    EXPECT_FALSE(parseControlServerOptions(5, badPreallocate).isOk());

    const char* badFileIoBackend[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                      "--file-io-backend", "uring"};
    EXPECT_FALSE(parseControlServerOptions(5, badFileIoBackend).isOk());

    const char* badFileIoBuffer[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                     "--file-io-buffer-size", "67108865"};
    EXPECT_FALSE(parseControlServerOptions(5, badFileIoBuffer).isOk());

    const char* badFileIoQueueDepth[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                         "--file-io-queue-depth", "0"};
    EXPECT_FALSE(parseControlServerOptions(5, badFileIoQueueDepth).isOk());

    const char* badFileIoBatchSize[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                        "--file-io-batch-size", "257"};
    EXPECT_FALSE(parseControlServerOptions(5, badFileIoBatchSize).isOk());

    const char* badFileIoAdvice[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                     "--file-io-advice", "random"};
    EXPECT_FALSE(parseControlServerOptions(5, badFileIoAdvice).isOk());

    const char* badPosixWriteStrategy[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                           "--posix-write-strategy", "buffered"};
    EXPECT_FALSE(parseControlServerOptions(5, badPosixWriteStrategy).isOk());

    const char* badCoalescedWithoutBuffer[] = {"cpnetflux-gridftp-server", "--root", "/tmp",
                                               "--posix-write-strategy", "coalesced"};
    EXPECT_FALSE(parseControlServerOptions(5, badCoalescedWithoutBuffer).isOk());
}

TEST(ControlOptionsTest, ResolvesStorPathInsideRoot) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-path-root";
    std::filesystem::create_directories(root / "subdir");

    auto resolved = resolveStorPath(root.string(), "subdir/file.bin");
    ASSERT_TRUE(resolved.isOk()) << resolved.status().message();
    EXPECT_EQ(resolved.value(), (root / "subdir/file.bin").string());

    EXPECT_FALSE(resolveStorPath(root.string(), "../escape.bin").isOk());
    EXPECT_FALSE(resolveStorPath(root.string(), "/tmp/escape.bin").isOk());
    EXPECT_FALSE(resolveStorPath(root.string(), "").isOk());
    auto nested = resolveStorPath(root.string(), "missing/file.bin");
    ASSERT_TRUE(nested.isOk()) << nested.status().message();
    EXPECT_EQ(nested.value(), (root / "missing/file.bin").string());
    EXPECT_TRUE(std::filesystem::is_directory(root / "missing"));

    std::filesystem::remove_all(root);
}

TEST(ControlOptionsTest, ResolvesRetrPathInsideRoot) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-retr-path-root";
    std::filesystem::create_directories(root / "subdir");
    const std::filesystem::path file = root / "subdir/source.bin";
    std::filesystem::path directory = root / "subdir";
    {
        std::ofstream output(file);
        output << "hello";
    }

    auto resolved = resolveRetrPath(root.string(), "subdir/source.bin");
    ASSERT_TRUE(resolved.isOk()) << resolved.status().message();
    EXPECT_EQ(resolved.value(), file.string());

    EXPECT_FALSE(resolveRetrPath(root.string(), "../escape.bin").isOk());
    EXPECT_FALSE(resolveRetrPath(root.string(), "/tmp/escape.bin").isOk());
    EXPECT_FALSE(resolveRetrPath(root.string(), "").isOk());
    EXPECT_FALSE(resolveRetrPath(root.string(), "missing.bin").isOk());
    EXPECT_FALSE(resolveRetrPath(root.string(), "subdir").isOk());

    std::filesystem::remove_all(root);
}

TEST(ControlOptionsTest, ResolvesPathsRelativeToWorkingDirectory) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-cwd-path-root";
    std::filesystem::create_directories(root / "subdir" / "nested");
    const std::filesystem::path file = root / "subdir" / "nested" / "source.bin";
    {
        std::ofstream output(file);
        output << "hello";
    }

    auto virtualPath = resolveVirtualPath("/subdir", "nested/source.bin", false);
    ASSERT_TRUE(virtualPath.isOk()) << virtualPath.status().message();
    EXPECT_EQ(virtualPath.value(), "/subdir/nested/source.bin");

    auto resolved = resolveRetrPath(root.string(), "/subdir", "nested/source.bin");
    ASSERT_TRUE(resolved.isOk()) << resolved.status().message();
    EXPECT_EQ(resolved.value(), file.string());

    auto directory = resolveControlPath(root.string(), "/subdir", "nested",
                                        ControlPathKind::ExistingDirectory, "CWD");
    ASSERT_TRUE(directory.isOk()) << directory.status().message();
    EXPECT_EQ(directory.value().virtualPath, "/subdir/nested");

    directory = resolveControlPath(root.string(), "/subdir/nested", "..",
                                   ControlPathKind::ExistingDirectory, "CWD");
    ASSERT_TRUE(directory.isOk()) << directory.status().message();
    EXPECT_EQ(directory.value().virtualPath, "/subdir");

    std::filesystem::remove_all(root);
}

TEST(ControlOptionsTest, RejectsSymlinkEscape) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-control-symlink-root";
    const std::filesystem::path outside =
        std::filesystem::temp_directory_path() / "cpnetflux-control-symlink-outside";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside);
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(outside);
    {
        std::ofstream output(outside / "secret.bin");
        output << "secret";
    }
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, root / "escape", error);
    if (error) {
        GTEST_SKIP() << "symlink creation is unavailable: " << error.message();
    }

    EXPECT_FALSE(resolveControlPath(root.string(), "/", "escape/secret.bin",
                                    ControlPathKind::ExistingFile, "SIZE")
                     .isOk());
    EXPECT_FALSE(
        resolveControlPath(root.string(), "/", "escape", ControlPathKind::ExistingDirectory, "LIST")
            .isOk());

    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside);
}

TEST(ControlOptionsTest, FormatsMdtmAndListings) {
    EXPECT_EQ(formatMdtmTime(0), "19700101000000");

    std::vector<ControlListEntry> entries;
    entries.push_back(ControlListEntry{"alpha.bin", false, 5, 0});
    entries.push_back(ControlListEntry{"subdir", true, 0, 0});

    EXPECT_EQ(formatNlst(entries), "alpha.bin\r\nsubdir\r\n");
    const std::string list = formatList(entries);
    EXPECT_NE(list.find("- 5 19700101000000 alpha.bin\r\n"), std::string::npos);
    EXPECT_NE(list.find("d 0 19700101000000 subdir\r\n"), std::string::npos);
    EXPECT_EQ(list.find("/tmp/"), std::string::npos);
}

}  // namespace
