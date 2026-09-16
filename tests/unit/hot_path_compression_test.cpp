#include "cpnetflux/config/file_download_options.h"
#include "cpnetflux/config/file_transfer_options.h"
#include "cpnetflux/core/io/file_download_client.h"
#include "cpnetflux/core/io/file_download_sender.h"
#include "cpnetflux/core/io/file_transfer_client.h"
#include "cpnetflux/core/io/file_transfer_server.h"
#include "cpnetflux/core/io/socket_utils.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

std::uint16_t listenerPort(int fd) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length), 0);
    return ntohs(address.sin_port);
}

}  // namespace

TEST(HotPathCompressionTest, DirectRetrCompressedDataRoundTripsThroughReceiver) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-hot-path-retr-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path source = root / "source.bin";
    const std::filesystem::path destination = root / "destination.bin";

    {
        std::ofstream output(source, std::ios::binary);
        ASSERT_TRUE(output);
        const std::string pattern = "CPNetFlux compressed RETR integration pattern\n";
        for (std::size_t index = 0; index < 256 * 1024 / pattern.size() + 1; ++index) {
            output.write(pattern.data(), static_cast<std::streamsize>(pattern.size()));
        }
    }

    auto listenerResult = cpnetflux::core::io::createListener("127.0.0.1", 0, 1);
    ASSERT_TRUE(listenerResult.isOk()) << listenerResult.status().message();
    cpnetflux::core::io::UniqueFd listener = std::move(listenerResult.value());
    const std::uint16_t port = listenerPort(listener.get());

    cpnetflux::core::io::FileDownloadSenderOptions sender;
    sender.path = source.string();
    sender.transferId = "phase3-direct-retr";
    sender.connections = 1;
    sender.chunkSize = 64 * 1024;
    sender.bufferSize = 16 * 1024;
    sender.checksumAlgorithm = cpnetflux::checksum::ChecksumAlgorithm::Crc32c;
    sender.sourcePath = "source.bin";
    sender.hotPathCompression.enabled = true;
    sender.hotPathCompression.candidate = true;
    sender.hotPathCompression.maxPayloadBytes = sender.bufferSize;
    cpnetflux::core::io::TransferRuntimeMetrics senderMetrics;
    sender.runtimeMetrics = &senderMetrics;

    cpnetflux::config::FileDownloadOptions receiver;
    receiver.host = "127.0.0.1";
    receiver.port = port;
    receiver.connections = 1;
    receiver.bufferSize = sender.bufferSize;
    receiver.path = destination.string();
    receiver.transferId = sender.transferId;
    receiver.checksumAlgorithm = sender.checksumAlgorithm;
    receiver.overwrite = true;
    cpnetflux::core::io::TransferRuntimeMetrics receiverMetrics;
    receiver.runtimeMetrics = &receiverMetrics;

    cpnetflux::common::Status senderStatus;
    std::thread senderThread([&]() {
        senderStatus = cpnetflux::core::io::runFramedFileSenderOnListener(
            sender, std::move(listener));
    });
    const cpnetflux::common::Status receiverStatus =
        cpnetflux::core::io::runFileDownloadClient(receiver);
    senderThread.join();

    ASSERT_TRUE(receiverStatus.isOk()) << receiverStatus.message();
    ASSERT_TRUE(senderStatus.isOk()) << senderStatus.message();
    EXPECT_GT(senderMetrics.compressedFrames, 0U);
    EXPECT_GT(receiverMetrics.compressedFrames, 0U);
    EXPECT_LT(senderMetrics.wireBytes, senderMetrics.logicalBytes);

    std::ifstream expected(source, std::ios::binary);
    std::ifstream actual(destination, std::ios::binary);
    ASSERT_TRUE(expected);
    ASSERT_TRUE(actual);
    EXPECT_EQ(std::vector<char>((std::istreambuf_iterator<char>(expected)),
                                std::istreambuf_iterator<char>()),
              std::vector<char>((std::istreambuf_iterator<char>(actual)),
                                std::istreambuf_iterator<char>()));
    std::filesystem::remove_all(root);
}

TEST(HotPathCompressionTest, CorruptCompressedStorResumesWithRawPayload) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-hot-path-stor-retry-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path source = root / "source.bin";
    const std::filesystem::path destination = root / "destination.bin";
    {
        std::ofstream output(source, std::ios::binary);
        ASSERT_TRUE(output);
        const std::string pattern = "CPNetFlux compressed STOR retry pattern\n";
        for (std::size_t index = 0; index < 128 * 1024 / pattern.size() + 1; ++index) {
            output.write(pattern.data(), static_cast<std::streamsize>(pattern.size()));
        }
    }

    const std::string transferId = "phase3-stor-raw-retry";
    cpnetflux::config::FileTransferOptions server;
    server.host = "127.0.0.1";
    server.connections = 1;
    server.bufferSize = 16 * 1024;
    server.chunkSize = 64 * 1024;
    server.path = destination.string();
    server.transferId = transferId;
    server.checksumAlgorithm = cpnetflux::checksum::ChecksumAlgorithm::Crc32c;

    cpnetflux::config::FileTransferOptions client;
    client.host = "127.0.0.1";
    client.connections = 1;
    client.bufferSize = server.bufferSize;
    client.chunkSize = server.chunkSize;
    client.path = source.string();
    client.transferId = transferId;
    client.checksumAlgorithm = server.checksumAlgorithm;
    client.hotPathCompression.enabled = true;
    client.hotPathCompression.candidate = true;
    client.hotPathCompression.maxPayloadBytes = client.bufferSize;
    client.hotPathCompression.testCorruptCompressedPayload = true;

    auto firstListenerResult = cpnetflux::core::io::createListener("127.0.0.1", 0, 1);
    ASSERT_TRUE(firstListenerResult.isOk()) << firstListenerResult.status().message();
    cpnetflux::core::io::UniqueFd firstListener = std::move(firstListenerResult.value());
    const std::uint16_t firstPort = listenerPort(firstListener.get());
    server.port = firstPort;
    client.port = firstPort;

    cpnetflux::common::Status firstServerStatus;
    std::thread firstServer([&]() {
        firstServerStatus = cpnetflux::core::io::runFileTransferServerOnListener(
            server, std::move(firstListener));
    });
    const cpnetflux::common::Status firstClientStatus =
        cpnetflux::core::io::runFileTransferClient(client);
    firstServer.join();
    EXPECT_FALSE(firstClientStatus.isOk());
    EXPECT_FALSE(firstServerStatus.isOk());
    EXPECT_TRUE(std::filesystem::exists(
        cpnetflux::checkpoint::manifestPathForOutput(destination.string())));

    auto secondListenerResult = cpnetflux::core::io::createListener("127.0.0.1", 0, 1);
    ASSERT_TRUE(secondListenerResult.isOk()) << secondListenerResult.status().message();
    cpnetflux::core::io::UniqueFd secondListener = std::move(secondListenerResult.value());
    const std::uint16_t secondPort = listenerPort(secondListener.get());
    server.resume = true;
    server.port = secondPort;
    client.resume = true;
    client.port = secondPort;
    client.hotPathCompression.testCorruptCompressedPayload = false;
    client.hotPathCompression.forceRaw = true;

    cpnetflux::common::Status secondServerStatus;
    std::thread secondServer([&]() {
        secondServerStatus = cpnetflux::core::io::runFileTransferServerOnListener(
            server, std::move(secondListener));
    });
    const cpnetflux::common::Status secondClientStatus =
        cpnetflux::core::io::runFileTransferClient(client);
    secondServer.join();

    ASSERT_TRUE(secondClientStatus.isOk()) << secondClientStatus.message();
    ASSERT_TRUE(secondServerStatus.isOk()) << secondServerStatus.message();
    std::ifstream expected(source, std::ios::binary);
    std::ifstream actual(destination, std::ios::binary);
    ASSERT_TRUE(expected);
    ASSERT_TRUE(actual);
    EXPECT_EQ(std::vector<char>((std::istreambuf_iterator<char>(expected)),
                                std::istreambuf_iterator<char>()),
              std::vector<char>((std::istreambuf_iterator<char>(actual)),
                                std::istreambuf_iterator<char>()));
    std::filesystem::remove_all(root);
}

TEST(HotPathCompressionTest, CompressionFailureFallsBackToRawData) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cpnetflux-hot-path-stor-fallback-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path source = root / "source.bin";
    const std::filesystem::path destination = root / "destination.bin";
    {
        std::ofstream output(source, std::ios::binary);
        ASSERT_TRUE(output);
        const std::string pattern = "CPNetFlux forced compression failure fallback\n";
        for (std::size_t index = 0; index < 96 * 1024 / pattern.size() + 1; ++index) {
            output.write(pattern.data(), static_cast<std::streamsize>(pattern.size()));
        }
    }

    cpnetflux::config::FileTransferOptions server;
    server.host = "127.0.0.1";
    server.connections = 1;
    server.bufferSize = 16 * 1024;
    server.chunkSize = 64 * 1024;
    server.path = destination.string();
    server.transferId = "phase3-stor-force-raw";
    server.checksumAlgorithm = cpnetflux::checksum::ChecksumAlgorithm::Crc32c;

    cpnetflux::config::FileTransferOptions client;
    client.host = "127.0.0.1";
    client.connections = 1;
    client.bufferSize = server.bufferSize;
    client.chunkSize = server.chunkSize;
    client.path = source.string();
    client.port = 0;
    client.transferId = server.transferId;
    client.checksumAlgorithm = server.checksumAlgorithm;
    client.hotPathCompression.enabled = true;
    client.hotPathCompression.candidate = true;
    client.hotPathCompression.maxPayloadBytes = client.bufferSize;
    client.hotPathCompression.testForceCompressFailure = true;
    cpnetflux::core::io::TransferRuntimeMetrics clientMetrics;
    client.runtimeMetrics = &clientMetrics;

    auto listenerResult = cpnetflux::core::io::createListener("127.0.0.1", 0, 1);
    ASSERT_TRUE(listenerResult.isOk()) << listenerResult.status().message();
    cpnetflux::core::io::UniqueFd listener = std::move(listenerResult.value());
    const std::uint16_t port = listenerPort(listener.get());
    server.port = port;
    client.port = port;

    cpnetflux::common::Status serverStatus;
    std::thread serverThread([&]() {
        serverStatus = cpnetflux::core::io::runFileTransferServerOnListener(
            server, std::move(listener));
    });
    const cpnetflux::common::Status clientStatus =
        cpnetflux::core::io::runFileTransferClient(client);
    serverThread.join();

    ASSERT_TRUE(clientStatus.isOk()) << clientStatus.message();
    ASSERT_TRUE(serverStatus.isOk()) << serverStatus.message();
    EXPECT_GT(clientMetrics.compressionAttempts, 0U);
    EXPECT_EQ(clientMetrics.compressedFrames, 0U);
    EXPECT_GT(clientMetrics.rawFallbackFrames, 0U);
    EXPECT_GT(clientMetrics.compressionFailures, 0U);
    EXPECT_EQ(clientMetrics.compressionFallbackReason, "compression_failure");

    std::ifstream expected(source, std::ios::binary);
    std::ifstream actual(destination, std::ios::binary);
    ASSERT_TRUE(expected);
    ASSERT_TRUE(actual);
    EXPECT_EQ(std::vector<char>((std::istreambuf_iterator<char>(expected)),
                                std::istreambuf_iterator<char>()),
              std::vector<char>((std::istreambuf_iterator<char>(actual)),
                                std::istreambuf_iterator<char>()));
    std::filesystem::remove_all(root);
}
