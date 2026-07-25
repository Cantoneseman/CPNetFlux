#include "gridflux/config/file_transfer_options.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "gridflux/checksum/checksum.h"
#include "gridflux/core/io/tls_socket.h"
#include "gridflux/core/session/commit_sync_policy.h"
#include "gridflux/core/session/final_verify_policy.h"
#include "gridflux/core/session/manifest_flush_policy.h"
#include "gridflux/storage/file_io.h"

namespace {

gridflux::common::Result<gridflux::config::FileTransferOptions> parse(
    std::initializer_list<const char*> args, gridflux::config::FileTransferRole role) {
    return gridflux::config::parseFileTransferOptions(static_cast<int>(args.size()), args.begin(),
                                                      role);
}

}  // namespace

TEST(FileTransferOptionsTest, AppliesServerDefaults) {
    const auto result = parse({"gridflux-file-server", "--output", "/tmp/out"},
                              gridflux::config::FileTransferRole::Server);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().host, "0.0.0.0");
    EXPECT_EQ(result.value().port, 9100);
    EXPECT_EQ(result.value().connections, 1U);
    EXPECT_EQ(result.value().bufferSize, 65536U);
    EXPECT_EQ(result.value().chunkSize, 1048576U);
    EXPECT_EQ(result.value().dataFinalStatusTimeoutSeconds, 60U);
    EXPECT_EQ(result.value().path, "/tmp/out");
    EXPECT_FALSE(result.value().overwrite);
    EXPECT_FALSE(result.value().keepPartial);
    EXPECT_FALSE(result.value().resume);
    EXPECT_EQ(result.value().checksumAlgorithm, gridflux::checksum::ChecksumAlgorithm::Crc32c);
    EXPECT_EQ(result.value().checksumBackend, gridflux::checksum::ChecksumBackend::Auto);
    EXPECT_EQ(result.value().manifestFlushPolicy,
              gridflux::core::session::ManifestFlushPolicy::EveryNChunks);
    EXPECT_EQ(result.value().manifestFlushIntervalChunks, 16U);
    EXPECT_EQ(result.value().finalVerifyPolicy,
              gridflux::core::session::FinalVerifyPolicy::Full);
    EXPECT_EQ(result.value().commitSyncPolicy,
              gridflux::core::session::CommitSyncPolicy::None);
    EXPECT_EQ(result.value().preallocateMode, gridflux::storage::PreallocateMode::Off);
    EXPECT_EQ(result.value().fileIo.backend, gridflux::storage::FileIoBackendKind::Posix);
    EXPECT_EQ(result.value().fileIo.bufferSize, 0U);
    EXPECT_EQ(result.value().fileIo.advice, gridflux::storage::FileIoAdvice::Off);
    EXPECT_EQ(result.value().fileIo.posixWriteStrategy,
              gridflux::storage::PosixWriteStrategy::Auto);
    EXPECT_TRUE(result.value().eventLogPath.empty());
    EXPECT_EQ(result.value().dataTlsMode, gridflux::core::io::DataTlsMode::Off);
}

TEST(FileTransferOptionsTest, AppliesClientDefaults) {
    const auto result = parse({"gridflux-file-client", "--input", "/tmp/in"},
                              gridflux::config::FileTransferRole::Client);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().host, "127.0.0.1");
    EXPECT_EQ(result.value().path, "/tmp/in");
    EXPECT_TRUE(result.value().transferId.empty());
    EXPECT_EQ(result.value().maxChunks, 0U);
    EXPECT_EQ(result.value().checksumAlgorithm, gridflux::checksum::ChecksumAlgorithm::Crc32c);
    EXPECT_EQ(result.value().checksumBackend, gridflux::checksum::ChecksumBackend::Auto);
    EXPECT_FALSE(result.value().hasCorruptChunk);
    EXPECT_FALSE(result.value().hasDuplicateCorruptChunk);
    EXPECT_EQ(result.value().fileIo.backend, gridflux::storage::FileIoBackendKind::Posix);
    EXPECT_EQ(result.value().fileIo.bufferSize, 0U);
    EXPECT_EQ(result.value().fileIo.advice, gridflux::storage::FileIoAdvice::Off);
    EXPECT_EQ(result.value().fileIo.posixWriteStrategy,
              gridflux::storage::PosixWriteStrategy::Auto);
}

TEST(FileTransferOptionsTest, ParsesClientDataFinalStatusTimeout) {
    const auto result =
        parse({"gridflux-file-client", "--input", "/tmp/in",
               "--data-final-status-timeout-seconds", "3"},
              gridflux::config::FileTransferRole::Client);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().dataFinalStatusTimeoutSeconds, 3U);
}

TEST(FileTransferOptionsTest, ParsesClientOptions) {
    const std::filesystem::path ca =
        std::filesystem::temp_directory_path() / "gridflux-file-client-ca.pem";
    {
        std::ofstream output(ca);
        output << "not-a-real-ca\n";
    }
    const std::string caText = ca.string();
    const auto result = parse({"gridflux-file-client",
                               "--host",
                               "<redacted>",
                               "--port",
                               "19310",
                               "--input",
                               "/tmp/in",
                               "--connections",
                               "4",
                               "--chunk-size",
                               "1048576",
                               "--buffer-size",
                               "262144",
                               "--checksum",
                               "none",
                               "--transfer-id",
                               "phase2a",
                               "--max-chunks",
                               "8",
                               "--checksum-backend",
                               "software",
                               "--file-io-backend",
                               "io_uring",
                               "--file-io-buffer-size",
                               "1048576",
                               "--file-io-queue-depth",
                               "4",
                               "--file-io-advice",
                               "sequential",
                               "--posix-write-strategy",
                               "direct",
                               "--event-log",
                               "/tmp/gridflux-file-client-events.jsonl",
                               "--data-tls-mode",
                               "required",
                               "--tls-ca-file",
                               caText.c_str(),
                               "--corrupt-chunk",
                               "0",
                              "--duplicate-corrupt-chunk",
                              "3"},
                             gridflux::config::FileTransferRole::Client);

    if (!gridflux::core::io::tlsSupportAvailable()) {
        EXPECT_FALSE(result.isOk());
        std::filesystem::remove(ca);
        return;
    }
    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().host, "<redacted>");
    EXPECT_EQ(result.value().port, 19310);
    EXPECT_EQ(result.value().connections, 4U);
    EXPECT_EQ(result.value().chunkSize, 1048576U);
    EXPECT_EQ(result.value().bufferSize, 262144U);
    EXPECT_EQ(result.value().checksumAlgorithm, gridflux::checksum::ChecksumAlgorithm::None);
    EXPECT_EQ(result.value().checksumBackend, gridflux::checksum::ChecksumBackend::Software);
    EXPECT_EQ(result.value().transferId, "phase2a");
    EXPECT_EQ(result.value().maxChunks, 8U);
    EXPECT_TRUE(result.value().hasCorruptChunk);
    EXPECT_EQ(result.value().corruptChunk, 0U);
    EXPECT_TRUE(result.value().hasDuplicateCorruptChunk);
    EXPECT_EQ(result.value().duplicateCorruptChunk, 3U);
    EXPECT_EQ(result.value().fileIo.backend, gridflux::storage::FileIoBackendKind::IoUring);
    EXPECT_EQ(result.value().fileIo.bufferSize, 1048576U);
    EXPECT_EQ(result.value().fileIo.queueDepth, 4U);
    EXPECT_EQ(result.value().fileIo.batchSize, 4U);
    EXPECT_EQ(result.value().fileIo.advice, gridflux::storage::FileIoAdvice::Sequential);
    EXPECT_EQ(result.value().fileIo.posixWriteStrategy,
              gridflux::storage::PosixWriteStrategy::Direct);
    EXPECT_EQ(result.value().eventLogPath, "/tmp/gridflux-file-client-events.jsonl");
    EXPECT_EQ(result.value().dataTlsMode, gridflux::core::io::DataTlsMode::Required);
    EXPECT_EQ(result.value().dataTls.caFile, caText);
    std::filesystem::remove(ca);
}

TEST(FileTransferOptionsTest, ParsesServerFlags) {
    const auto result =
        parse({"gridflux-file-server", "--output", "/tmp/out", "--overwrite", "--keep-partial",
               "--resume", "--checksum", "crc32c", "--checksum-backend", "software",
               "--manifest-flush-policy", "final_only", "--manifest-flush-interval-chunks", "32",
               "--final-verify-policy", "verified_chunks", "--commit-sync-policy",
               "fsync_file_and_dir", "--preallocate", "full", "--file-io-buffer-size", "2097152",
               "--file-io-backend", "io_uring", "--file-io-queue-depth", "8",
               "--file-io-batch-size", "2", "--file-io-advice", "dontneed",
               "--posix-write-strategy", "coalesced", "--event-log",
               "/tmp/gridflux-file-server-events.jsonl"},
              gridflux::config::FileTransferRole::Server);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_TRUE(result.value().overwrite);
    EXPECT_TRUE(result.value().keepPartial);
    EXPECT_TRUE(result.value().resume);
    EXPECT_EQ(result.value().checksumAlgorithm, gridflux::checksum::ChecksumAlgorithm::Crc32c);
    EXPECT_EQ(result.value().checksumBackend, gridflux::checksum::ChecksumBackend::Software);
    EXPECT_EQ(result.value().manifestFlushPolicy,
              gridflux::core::session::ManifestFlushPolicy::FinalOnly);
    EXPECT_EQ(result.value().manifestFlushIntervalChunks, 32U);
    EXPECT_EQ(result.value().finalVerifyPolicy,
              gridflux::core::session::FinalVerifyPolicy::VerifiedChunks);
    EXPECT_EQ(result.value().commitSyncPolicy,
              gridflux::core::session::CommitSyncPolicy::FsyncFileAndDir);
    EXPECT_EQ(result.value().preallocateMode, gridflux::storage::PreallocateMode::Full);
    EXPECT_EQ(result.value().fileIo.backend, gridflux::storage::FileIoBackendKind::IoUring);
    EXPECT_EQ(result.value().fileIo.bufferSize, 2097152U);
    EXPECT_EQ(result.value().fileIo.queueDepth, 8U);
    EXPECT_EQ(result.value().fileIo.batchSize, 2U);
    EXPECT_EQ(result.value().fileIo.advice, gridflux::storage::FileIoAdvice::DontNeed);
    EXPECT_EQ(result.value().fileIo.posixWriteStrategy,
              gridflux::storage::PosixWriteStrategy::Coalesced);
    EXPECT_EQ(result.value().eventLogPath, "/tmp/gridflux-file-server-events.jsonl");
}

TEST(FileTransferOptionsTest, ParsesClientResume) {
    const auto result =
        parse({"gridflux-file-client", "--input", "/tmp/in", "--resume", "--transfer-id", "abc"},
              gridflux::config::FileTransferRole::Client);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_TRUE(result.value().resume);
    EXPECT_EQ(result.value().transferId, "abc");
}

TEST(FileTransferOptionsTest, RejectsMissingPath) {
    EXPECT_FALSE(
        parse({"gridflux-file-server"}, gridflux::config::FileTransferRole::Server).isOk());
    EXPECT_FALSE(
        parse({"gridflux-file-client"}, gridflux::config::FileTransferRole::Client).isOk());
}

TEST(FileTransferOptionsTest, RejectsWrongRolePathOptions) {
    EXPECT_FALSE(parse({"gridflux-file-server", "--input", "/tmp/in"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--output", "/tmp/out"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
}

TEST(FileTransferOptionsTest, RejectsServerOnlyFlagsForClient) {
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--overwrite"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--keep-partial"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--transfer-id", "abc"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--max-chunks", "1"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--corrupt-chunk", "0"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(
        parse({"gridflux-file-server", "--output", "/tmp/out", "--duplicate-corrupt-chunk", "0"},
              gridflux::config::FileTransferRole::Server)
            .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in",
                        "--manifest-flush-policy", "final_only"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in",
                        "--manifest-flush-interval-chunks", "1"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--final-verify-policy",
                        "verified_chunks"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--commit-sync-policy",
                        "fsync_file"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--preallocate", "full"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
}

TEST(FileTransferOptionsTest, RejectsInvalidNumericOptions) {
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--port", "70000"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--connections", "65"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--buffer-size", "0"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--chunk-size", "0"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--max-chunks", "0"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--checksum", "sha256"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--checksum-backend", "fast"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out",
                        "--manifest-flush-interval-chunks", "0"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out",
                        "--manifest-flush-policy", "sometimes"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--final-verify-policy",
                        "fast"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--commit-sync-policy",
                        "sync_everything"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--preallocate", "yes"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--file-io-backend",
                        "uring"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--file-io-buffer-size",
                        "67108865"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--file-io-queue-depth",
                        "0"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--file-io-batch-size",
                        "257"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--file-io-advice",
                        "random"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--posix-write-strategy",
                        "buffered"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--posix-write-strategy",
                        "coalesced"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--data-tls-mode",
                        "sometimes"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--data-tls-mode"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in",
                        "--data-final-status-timeout-seconds", "0"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out",
                        "--data-final-status-timeout-seconds", "1"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
    EXPECT_FALSE(parse({"gridflux-file-server", "--output", "/tmp/out", "--data-tls-mode",
                        "required"},
                       gridflux::config::FileTransferRole::Server)
                     .isOk());
}

TEST(FileTransferOptionsTest, RejectsResumeWithoutTransferIdForClient) {
    EXPECT_FALSE(parse({"gridflux-file-client", "--input", "/tmp/in", "--resume"},
                       gridflux::config::FileTransferRole::Client)
                     .isOk());
}
