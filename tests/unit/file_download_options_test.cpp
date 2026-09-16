#include "cpnetflux/config/file_download_options.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "cpnetflux/core/io/tls_socket.h"
#include "cpnetflux/storage/file_io.h"

namespace {

using cpnetflux::checksum::ChecksumAlgorithm;
using cpnetflux::checksum::ChecksumBackend;
using cpnetflux::config::parseFileDownloadOptions;
using cpnetflux::core::session::CommitSyncPolicy;
using cpnetflux::core::session::FinalVerifyPolicy;
using cpnetflux::core::session::ManifestFlushPolicy;

TEST(FileDownloadOptionsTest, ParsesRequiredAndDefaults) {
    const char* argv[] = {"cpnetflux-file-download-client", "--output", "/tmp/out.bin",
                          "--transfer-id", "download-token"};
    auto parsed = parseFileDownloadOptions(static_cast<int>(std::size(argv)), argv);
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().host, "127.0.0.1");
    EXPECT_EQ(parsed.value().port, 9101);
    EXPECT_EQ(parsed.value().connections, 1U);
    EXPECT_EQ(parsed.value().bufferSize, 65536U);
    EXPECT_EQ(parsed.value().path, "/tmp/out.bin");
    EXPECT_EQ(parsed.value().transferId, "download-token");
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
    EXPECT_TRUE(parsed.value().eventLogPath.empty());
    EXPECT_EQ(parsed.value().dataTlsMode, cpnetflux::core::io::DataTlsMode::Off);
    EXPECT_FALSE(parsed.value().overwrite);
    EXPECT_FALSE(parsed.value().resume);
    EXPECT_EQ(parsed.value().maxChunks, 0U);
}

TEST(FileDownloadOptionsTest, ParsesExplicitOptions) {
    const std::filesystem::path ca =
        std::filesystem::temp_directory_path() / "cpnetflux-download-ca.pem";
    {
        std::ofstream output(ca);
        output << "not-a-real-ca\n";
    }
    const std::string caText = ca.string();
    const char* argv[] = {"cpnetflux-file-download-client",
                          "--host",
                          "<redacted>",
                          "--port",
                          "20300",
                          "--output",
                          "/tmp/out.bin",
                          "--connections",
                          "4",
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
                          "4",
                          "--file-io-batch-size",
                          "2",
                          "--file-io-advice",
                          "sequential_dontneed",
                          "--posix-write-strategy",
                          "coalesced",
                          "--event-log",
                          "/tmp/cpnetflux-download-events.jsonl",
                          "--data-tls-mode",
                          "required",
                          "--tls-ca-file",
                          caText.c_str(),
                          "--transfer-id",
                          "download-token",
                          "--resume",
                          "--max-chunks",
                          "3",
                          "--overwrite"};
    auto parsed = parseFileDownloadOptions(static_cast<int>(std::size(argv)), argv);
    if (!cpnetflux::core::io::tlsSupportAvailable()) {
        EXPECT_FALSE(parsed.isOk());
        std::filesystem::remove(ca);
        return;
    }
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().host, "<redacted>");
    EXPECT_EQ(parsed.value().port, 20300);
    EXPECT_EQ(parsed.value().connections, 4U);
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
    EXPECT_EQ(parsed.value().fileIo.queueDepth, 4U);
    EXPECT_EQ(parsed.value().fileIo.batchSize, 2U);
    EXPECT_EQ(parsed.value().fileIo.advice, cpnetflux::storage::FileIoAdvice::SequentialDontNeed);
    EXPECT_EQ(parsed.value().fileIo.posixWriteStrategy,
              cpnetflux::storage::PosixWriteStrategy::Coalesced);
    EXPECT_EQ(parsed.value().eventLogPath, "/tmp/cpnetflux-download-events.jsonl");
    EXPECT_EQ(parsed.value().dataTlsMode, cpnetflux::core::io::DataTlsMode::Required);
    EXPECT_EQ(parsed.value().dataTls.caFile, caText);
    EXPECT_TRUE(parsed.value().overwrite);
    EXPECT_TRUE(parsed.value().resume);
    EXPECT_EQ(parsed.value().maxChunks, 3U);
    std::filesystem::remove(ca);
}

TEST(FileDownloadOptionsTest, RejectsInvalidOptions) {
    const char* missingOutput[] = {"cpnetflux-file-download-client", "--transfer-id", "id"};
    EXPECT_FALSE(parseFileDownloadOptions(3, missingOutput).isOk());

    const char* missingTransferId[] = {"cpnetflux-file-download-client", "--output", "/tmp/out"};
    EXPECT_FALSE(parseFileDownloadOptions(3, missingTransferId).isOk());

    const char* badTransferId[] = {"cpnetflux-file-download-client", "--output", "/tmp/out",
                                   "--transfer-id", "bad/id"};
    EXPECT_FALSE(parseFileDownloadOptions(5, badTransferId).isOk());

    const char* badConnections[] = {"cpnetflux-file-download-client",
                                    "--output",
                                    "/tmp/out",
                                    "--transfer-id",
                                    "id",
                                    "--connections",
                                    "65"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badConnections).isOk());

    const char* badMaxChunks[] = {"cpnetflux-file-download-client",
                                  "--output",
                                  "/tmp/out",
                                  "--transfer-id",
                                  "id",
                                  "--max-chunks",
                                  "0"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badMaxChunks).isOk());

    const char* badFlushInterval[] = {"cpnetflux-file-download-client",
                                      "--output",
                                      "/tmp/out",
                                      "--transfer-id",
                                      "id",
                                      "--manifest-flush-interval-chunks",
                                      "0"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badFlushInterval).isOk());

    const char* badFlushPolicy[] = {"cpnetflux-file-download-client",
                                    "--output",
                                    "/tmp/out",
                                    "--transfer-id",
                                    "id",
                                    "--manifest-flush-policy",
                                    "sometimes"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badFlushPolicy).isOk());

    const char* badFinalVerify[] = {"cpnetflux-file-download-client",
                                    "--output",
                                    "/tmp/out",
                                    "--transfer-id",
                                    "id",
                                    "--final-verify-policy",
                                    "trust-me"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badFinalVerify).isOk());

    const char* badCommitSync[] = {"cpnetflux-file-download-client",
                                   "--output",
                                   "/tmp/out",
                                   "--transfer-id",
                                   "id",
                                   "--commit-sync-policy",
                                   "sync_everything"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badCommitSync).isOk());

    const char* badPreallocate[] = {"cpnetflux-file-download-client",
                                    "--output",
                                    "/tmp/out",
                                    "--transfer-id",
                                    "id",
                                    "--preallocate",
                                    "yes"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badPreallocate).isOk());

    const char* badFileIoBackend[] = {"cpnetflux-file-download-client",
                                      "--output",
                                      "/tmp/out",
                                      "--transfer-id",
                                      "id",
                                      "--file-io-backend",
                                      "uring"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badFileIoBackend).isOk());

    const char* badFileIoBuffer[] = {"cpnetflux-file-download-client",
                                     "--output",
                                     "/tmp/out",
                                     "--transfer-id",
                                     "id",
                                     "--file-io-buffer-size",
                                     "67108865"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badFileIoBuffer).isOk());

    const char* badFileIoQueueDepth[] = {"cpnetflux-file-download-client",
                                         "--output",
                                         "/tmp/out",
                                         "--transfer-id",
                                         "id",
                                         "--file-io-queue-depth",
                                         "0"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badFileIoQueueDepth).isOk());

    const char* badFileIoBatchSize[] = {"cpnetflux-file-download-client",
                                        "--output",
                                        "/tmp/out",
                                        "--transfer-id",
                                        "id",
                                        "--file-io-batch-size",
                                        "257"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badFileIoBatchSize).isOk());

    const char* badFileIoAdvice[] = {"cpnetflux-file-download-client",
                                     "--output",
                                     "/tmp/out",
                                     "--transfer-id",
                                     "id",
                                     "--file-io-advice",
                                     "random"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badFileIoAdvice).isOk());

    const char* badPosixWriteStrategy[] = {"cpnetflux-file-download-client",
                                           "--output",
                                           "/tmp/out",
                                           "--transfer-id",
                                           "id",
                                           "--posix-write-strategy",
                                           "buffered"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badPosixWriteStrategy).isOk());

    const char* badCoalescedWithoutBuffer[] = {"cpnetflux-file-download-client",
                                               "--output",
                                               "/tmp/out",
                                               "--transfer-id",
                                               "id",
                                               "--posix-write-strategy",
                                               "coalesced"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badCoalescedWithoutBuffer).isOk());

    const char* badDataTls[] = {"cpnetflux-file-download-client",
                                "--output",
                                "/tmp/out",
                                "--transfer-id",
                                "id",
                                "--data-tls-mode",
                                "sometimes"};
    EXPECT_FALSE(parseFileDownloadOptions(7, badDataTls).isOk());
}

}  // namespace
