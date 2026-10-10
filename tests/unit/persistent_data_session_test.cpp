#include "cpnetflux/core/io/persistent_data_session.h"

#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>
#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/core/session/transfer_session.h"
#include "cpnetflux/core/session/download_session.h"

using namespace cpnetflux::core::io;
using namespace cpnetflux::core::protocol;

namespace {
struct SocketPair {
    FramedDataSocket sender, receiver;
    SocketPair() {
        int fds[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) throw std::runtime_error("socketpair");
        sender = FramedDataSocket(UniqueFd(fds[0]));
        receiver = FramedDataSocket(UniqueFd(fds[1]));
    }
};
struct TempRoot {
    std::filesystem::path path;
    TempRoot() {
        char name[] = "/tmp/cpnetflux-persistent-unit-XXXXXX";
        const auto* directory = ::mkdtemp(name);
        if (!directory) throw std::runtime_error("mkdtemp");
        path = directory;
    }
    ~TempRoot() { std::error_code error; std::filesystem::remove_all(path, error); }
};
}

using namespace cpnetflux::core::io;
using namespace cpnetflux::core::protocol;

static FrameHeader beginHeader(std::uint64_t size) {
    FrameHeader h;
    h.type = FrameType::FileBegin;
    h.streamId = 1;
    h.chunkId = 1;
    h.totalSize = size;
    h.payloadSize = 1;
    return h;
}

TEST(PersistentSessionTest, RejectsWrongDataOffsetWithoutAdvancing) {
    PersistentDataSession session;
    ASSERT_TRUE(session.begin(beginHeader(3), "a").isOk());
    auto data = beginHeader(3);
    data.type = FrameType::Data;
    data.payloadSize = 1;
    data.offset = 1;
    EXPECT_FALSE(session.data(data, 1).isOk());
    EXPECT_EQ(session.receivedBytes(), 0U);
}

TEST(PersistentSessionTest, RejectsCompletedGenerationReplay) {
    PersistentDataSession session;
    auto h = beginHeader(0);
    ASSERT_TRUE(session.begin(h, "a").isOk());
    h.type = FrameType::FileEnd;
    h.payloadSize = 0;
    ASSERT_TRUE(session.end(h).isOk());
    EXPECT_FALSE(session.begin(beginHeader(0), "a").isOk());
}

TEST(PersistentSessionTest, RejectsNulInFilePath) {
    PersistentDataSession session;
    EXPECT_FALSE(session.begin(beginHeader(0), std::string("a\0b", 3)).isOk());
}

TEST(PersistentSessionTest, SuccessResultMustMatchGenerationAndTotalSize) {
    SocketPair pair;
    PersistentFileIdentity id{1, 4, 3, "a", "result"};
    ASSERT_TRUE(PersistentDataSession::writeResult(&pair.receiver, id, FrameStatusCode::Ok).isOk());
    auto result = PersistentDataSession::readResult(&pair.sender, id);
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), FrameStatusCode::Ok);
    ASSERT_TRUE(PersistentDataSession::writeResult(&pair.receiver, id, FrameStatusCode::Ok).isOk());
    auto wrong = id;
    ++wrong.generation;
    EXPECT_FALSE(PersistentDataSession::readResult(&pair.sender, wrong).isOk());
    ASSERT_TRUE(PersistentDataSession::writeResult(&pair.receiver, id, FrameStatusCode::Ok).isOk());
    ++id.totalSize;
    EXPECT_FALSE(PersistentDataSession::readResult(&pair.sender, id).isOk());
}

TEST(PersistentSessionTest, CarriesChecksumAlgorithmAndFileEndDigest) {
    SocketPair pair;
    PersistentFileIdentity id{1, 1, 4, "checksum.bin", "checksum-transfer"};
    id.checksumAlgorithm = cpnetflux::checksum::ChecksumAlgorithm::Crc32c;
    const std::uint8_t bytes[] = {1, 2, 3, 4};
    cpnetflux::checksum::ChecksumComputer computer(id.checksumAlgorithm);
    computer.update(bytes, sizeof(bytes));
    const auto expected = computer.finalize();

    ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, id).isOk());
    auto begin = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(begin.isOk()) << begin.status().message();
    auto decoded = PersistentDataSession::decodeBegin(begin.value());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value().checksumAlgorithm, id.checksumAlgorithm);
    PersistentDataSession state;
    ASSERT_TRUE(state.begin(begin.value().header, decoded.value().relativePath,
                            decoded.value().checksumAlgorithm).isOk());
    ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender, id, 0, bytes, sizeof(bytes)).isOk());
    auto data = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(data.isOk()) << data.status().message();
    ASSERT_TRUE(state.data(data.value().header, data.value().payload.size(), data.value().payload.data()).isOk());
    ASSERT_TRUE(PersistentDataSession::writeEnd(&pair.sender, id, expected).isOk());
    auto end = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(end.isOk()) << end.status().message();
    auto completed = state.end(end.value().header, end.value().payload.data(), end.value().payload.size());
    ASSERT_TRUE(completed.isOk()) << completed.status().message();
    EXPECT_EQ(completed.value().checksum.algorithm, expected.algorithm);
    EXPECT_EQ(completed.value().checksum.value, expected.value);
}

TEST(PersistentSessionTest, OversizedFrameRejectedBeforePayloadRead) {
    SocketPair pair;
    auto h = beginHeader(1);
    h.payloadSize = PersistentDataSession::kMaxPayload + 1;
    auto bytes = encodeFrameHeader(h);
    ASSERT_TRUE(pair.sender.writeAll(bytes.data(), bytes.size()).isOk());
    EXPECT_FALSE(PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload).isOk());
}

TEST(PersistentSessionTest, IdleReadTimesOutAndDisconnectedReadFails) {
    SocketPair pair;
    ASSERT_TRUE(PersistentDataSession::setTimeout(&pair.receiver, 1).isOk());
    auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload).isOk());
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
    pair.sender = FramedDataSocket{};
    EXPECT_FALSE(PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload).isOk());
}

TEST(PersistentSessionTest, DisconnectRetainsOnlyCompletedCheckpointRanges) {
    SocketPair pair;
    TempRoot root;
    PersistentFileIdentity id{1, 1, 131072, "partial", "interrupt", 65536};
    PersistentTreeStats stats;
    cpnetflux::common::Status result;
    std::jthread receiver([&] {
        result = receivePersistentTree(&pair.receiver, root.path.string(), false, "", &stats);
    });
    ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, id).isOk());
    std::vector<std::uint8_t> data(65536, 7);
    ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender, id, 0, data.data(), data.size()).isOk());
    ::shutdown(pair.sender.fd(), SHUT_RDWR);
    receiver.join();
    EXPECT_FALSE(result.isOk());
    EXPECT_FALSE(std::filesystem::exists(root.path / "partial"));
    auto manifest = cpnetflux::checkpoint::ManifestStore::load(
        cpnetflux::checkpoint::manifestPathForOutput((root.path / "partial").string()));
    ASSERT_TRUE(manifest.isOk()) << manifest.status().message();
    EXPECT_EQ(manifest.value().state, cpnetflux::checkpoint::ManifestState::Failed);
    ASSERT_EQ(manifest.value().verifiedChunks.size(), 1U);
    EXPECT_EQ(manifest.value().verifiedChunks.front().length, 65536U);
    EXPECT_TRUE(std::filesystem::exists(manifest.value().tempPath));
    auto resumed = cpnetflux::core::session::TransferSession::resume(
        (root.path / "partial").string(), id.transferId, id.totalSize, id.chunkSize,
        cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(resumed.isOk()) << resumed.status().message();
    EXPECT_EQ(resumed.value().bytesCompleted(), 65536U);
}

TEST(PersistentSessionTest, InterruptedDownloadCheckpointIsAcceptedByLegacyResume) {
    SocketPair pair;
    TempRoot root;
    PersistentFileIdentity id{1, 1, 131072, "partial", "download-interrupt", 65536};
    PersistentTreeStats stats;
    cpnetflux::common::Status result;
    std::jthread receiver([&] {
        result = receivePersistentTree(&pair.receiver, root.path.string(), true, "remote/./tree", &stats);
    });
    ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, id).isOk());
    std::vector<std::uint8_t> data(65536, 7);
    ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender, id, 0, data.data(), data.size()).isOk());
    ::shutdown(pair.sender.fd(), SHUT_RDWR);
    receiver.join();
    EXPECT_FALSE(result.isOk());
    auto resumed = cpnetflux::core::session::DownloadSession::resume(
        (root.path / "partial").string(), "remote/tree/partial", id.transferId, id.totalSize,
        id.chunkSize, cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(resumed.isOk()) << resumed.status().message();
    EXPECT_EQ(resumed.value().bytesCompleted(), 65536U);
    ASSERT_EQ(resumed.value().missingRanges().size(), 1U);
    EXPECT_EQ(resumed.value().missingRanges()[0].begin, 65536U);
}

TEST(PersistentSessionTest, ScannerKeepsUserFilesAndExcludesOwnedCheckpointAndPartial) {
    TempRoot root;
    std::ofstream(root.path / "user.cpnetflux.manifest") << "ordinary user content";
    auto created = cpnetflux::core::session::TransferSession::createNew(
        (root.path / "data").string(), "scan-owned", 4, 65536,
        cpnetflux::checksum::ChecksumAlgorithm::None);
    ASSERT_TRUE(created.isOk());
    ASSERT_TRUE(created.value().save().isOk());
    std::ofstream(created.value().manifest().tempPath) << "part";
    auto scanned = scanPersistentTree(root.path.string());
    ASSERT_TRUE(scanned.isOk());
    ASSERT_EQ(scanned.value().size(), 1U);
    EXPECT_EQ(scanned.value()[0].relativePath, "user.cpnetflux.manifest");
}

TEST(PersistentSessionTest, RejectsTraversingPathAndChangedFileIdentity) {
    PersistentDataSession state;
    EXPECT_FALSE(state.begin(beginHeader(1), "../outside").isOk());
    ASSERT_TRUE(state.begin(beginHeader(1), "safe").isOk());
    auto data = beginHeader(1);
    data.type = FrameType::Data;
    ++data.chunkId;
    EXPECT_FALSE(state.data(data, 1).isOk());
    EXPECT_EQ(state.receivedBytes(), 0U);
}

TEST(PersistentSessionTest, ReceiverDrainsFailedMiddleFileAndCommitsNext) {
    SocketPair pair;
    TempRoot root;
    std::ofstream(root.path / "b") << "keep";
    PersistentTreeStats stats;
    cpnetflux::common::Status result;
    std::jthread receiver([&] {
        result = receivePersistentTree(&pair.receiver, root.path.string(), false, "", &stats);
    });
    for (std::uint32_t i = 1; i <= 3; ++i) {
        PersistentFileIdentity id{i, i, 1, std::string(1, char('a' + i - 1)), "file" + std::to_string(i)};
        ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, id).isOk());
        const std::uint8_t byte = static_cast<std::uint8_t>('0' + i);
        ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender, id, 0, &byte, 1).isOk());
        ASSERT_TRUE(PersistentDataSession::writeEnd(&pair.sender, id).isOk());
        auto status = PersistentDataSession::readResult(&pair.sender, id);
        ASSERT_TRUE(status.isOk());
        EXPECT_EQ(status.value(), i == 2 ? FrameStatusCode::WriteFailed : FrameStatusCode::Ok);
    }
    ASSERT_TRUE(PersistentDataSession::writeDirectoryEnd(&pair.sender, 3).isOk());
    receiver.join();
    EXPECT_FALSE(result.isOk());
    EXPECT_EQ(stats.files, 3U);
    EXPECT_EQ(stats.bytes, 2U);
    std::string content;
    std::ifstream(root.path / "b") >> content;
    EXPECT_EQ(content, "keep");
    std::ifstream(root.path / "c") >> content;
    EXPECT_EQ(content, "3");
}

TEST(PersistentSessionTest, StableFileIdsMapToExactlyOneBoundedChannel) {
    for (const std::uint32_t channelCount : {1U, 2U, 4U, 8U}) {
        for (std::uint32_t fileId = 1; fileId <= 128; ++fileId) {
            std::uint32_t matches = 0;
            for (std::uint32_t channel = 0; channel < channelCount; ++channel)
                matches += persistentFileAssignedToChannel(fileId, channel, channelCount) ? 1U : 0U;
            EXPECT_EQ(matches, 1U) << "fileId=" << fileId << " channels=" << channelCount;
        }
    }
    EXPECT_FALSE(persistentFileAssignedToChannel(0, 0, 4));
    EXPECT_FALSE(persistentFileAssignedToChannel(1, 4, 4));
    EXPECT_FALSE(persistentFileAssignedToChannel(1, 0, 0));
}

TEST(PersistentSessionTest, ReceiverRejectsFileAssignedToAnotherChannel) {
    SocketPair pair;
    TempRoot root;
    PersistentTreeStats stats;
    cpnetflux::common::Status result;
    std::jthread receiver([&] {
        result = receivePersistentTree(&pair.receiver, root.path.string(), false, "", &stats,
                                       {}, 1, 1, 2);
    });
    const PersistentFileIdentity wrongChannel{1, 1, 0, "wrong-channel", "generation"};
    ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, wrongChannel).isOk());
    receiver.join();
    EXPECT_FALSE(result.isOk());
    EXPECT_FALSE(std::filesystem::exists(root.path / "wrong-channel"));
}

TEST(PersistentSessionTest, DynamicAcceptsDescendingIdsButRejectsReplayAndStaleGeneration) {
    PersistentDataSession state(true);
    auto h=beginHeader(0);h.streamId=9;h.chunkId=9;
    ASSERT_TRUE(state.begin(h,"big").isOk());
    h.type=FrameType::FileEnd;h.payloadSize=0;
    ASSERT_TRUE(state.end(h).isOk());
    h=beginHeader(0);h.streamId=2;h.chunkId=2;
    ASSERT_TRUE(state.begin(h,"small").isOk());
    h.type=FrameType::FileEnd;h.payloadSize=0;
    ASSERT_TRUE(state.end(h).isOk());
    h=beginHeader(0);h.streamId=9;h.chunkId=10;
    EXPECT_FALSE(state.begin(h,"replay").isOk());
    h.streamId=10;h.chunkId=2;
    EXPECT_FALSE(state.begin(h,"stale").isOk());
}

TEST(PersistentSessionTest, CarriesRangeIdentityAndAcceptsOffsetRelativeData) {
    SocketPair pair;
    PersistentFileIdentity id{4, 9, 16, "range.bin", "range-transfer", 1024, 0,
                              cpnetflux::checksum::ChecksumAlgorithm::Crc32c, {}, 2, 8, 4, 4};
    const std::uint8_t bytes[] = {1, 2, 3, 4};
    cpnetflux::checksum::ChecksumComputer computer(id.checksumAlgorithm);
    computer.update(bytes, sizeof(bytes));
    const auto expected = computer.finalize();
    ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, id).isOk());
    auto begin = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(begin.isOk()) << begin.status().message();
    auto decoded = PersistentDataSession::decodeBegin(begin.value());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value().rangeId, id.rangeId);
    EXPECT_EQ(decoded.value().rangeOffset, id.rangeOffset);
    EXPECT_EQ(decoded.value().attempt, id.attempt);
    PersistentDataSession state;
    ASSERT_TRUE(state.begin(begin.value().header, decoded.value()).isOk());
    ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender, id, 8, bytes, sizeof(bytes)).isOk());
    auto data = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(data.isOk());
    ASSERT_TRUE(state.data(data.value().header, data.value().payload.size(), data.value().payload.data()).isOk());
    ASSERT_TRUE(PersistentDataSession::writeEnd(&pair.sender, id, expected).isOk());
    auto end = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(end.isOk());
    auto completed = state.end(end.value().header, end.value().payload.data(), end.value().payload.size());
    ASSERT_TRUE(completed.isOk()) << completed.status().message();
    EXPECT_EQ(completed.value().rangeId, id.rangeId);
    EXPECT_EQ(completed.value().checksum.value, expected.value);
}

TEST(PersistentSessionTest, RangeAttemptMakesRetryIdentityDistinct) {
    PersistentDataSession state;
    PersistentFileIdentity id{6, 2, 8, "retry.bin", "retry-transfer", 1024, 0,
                              cpnetflux::checksum::ChecksumAlgorithm::None, {}, 1, 0, 4, 2, 0};
    auto header = beginHeader(0);
    header.streamId = id.fileId;
    header.chunkId = id.generation;
    header.totalSize = id.totalSize;
    header.rangeId = id.rangeId;
    header.attempt = id.attempt;
    ASSERT_TRUE(state.begin(header, id).isOk());
    header.type = FrameType::FileEnd;
    header.flags = kRangeSkipped;
    header.payloadSize = 0;
    header.offset = id.rangeOffset + id.rangeLength;
    ASSERT_TRUE(state.end(header).isOk());
    id.attempt = 1;
    header.type = FrameType::FileBegin;
    header.flags = 0;
    header.payloadSize = 1;
    header.offset = 0;
    header.attempt = id.attempt;
    const auto retry = state.begin(header, id);
    ASSERT_TRUE(retry.isOk()) << retry.message();
}

TEST(PersistentSessionTest, SkipsAlreadyCompletedRangeWithoutPayload) {
    SocketPair pair;
    PersistentFileIdentity id{5, 1, 12, "skip.bin", "skip-transfer", 1024, 0,
                              cpnetflux::checksum::ChecksumAlgorithm::None, {}, 1, 0, 4, 3};
    ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, id).isOk());
    auto begin = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(begin.isOk());
    auto decoded = PersistentDataSession::decodeBegin(begin.value());
    ASSERT_TRUE(decoded.isOk());
    PersistentDataSession state;
    ASSERT_TRUE(state.begin(begin.value().header, decoded.value()).isOk());
    ASSERT_TRUE(PersistentDataSession::writeEnd(&pair.sender, id, {}, true).isOk());
    auto end = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(end.isOk());
    ASSERT_TRUE(state.end(end.value().header, nullptr, 0).isOk());
}


TEST(PersistentTreeRangeTest, LoopbackTransfersTwoRangesAndPublishesAfterFinalRange) {
    SocketPair pair;
    TempRoot source;
    TempRoot destination;
    const std::string content = "ABCDEFGH";
    {
        std::ofstream output(source.path / "large.bin", std::ios::binary);
        output << content;
    }
    struct stat sourceStat{};
    ASSERT_EQ(::stat((source.path / "large.bin").c_str(), &sourceStat), 0);
    std::vector<PersistentFileIdentity> ranges;
    for (std::uint64_t index = 0; index < 2; ++index) {
        ranges.push_back(PersistentFileIdentity{1, 7, content.size(), "large.bin", "range-loopback",
            4, sourceStat.st_mtim.tv_sec, cpnetflux::checksum::ChecksumAlgorithm::None, {},
            index + 1, index * 4, 4, 2, 1});
    }
    PersistentTreeStats receivedStats;
    cpnetflux::common::Status receiveStatus;
    std::jthread receiver([&] {
        receiveStatus = receivePersistentTree(&pair.receiver, destination.path.string(), false, "",
            &receivedStats, {}, 2, 0, 1, 1, false, false);
    });
    PersistentTreeStats sentStats;
    const auto sendStatus = sendPersistentTree(&pair.sender, source.path.string(), ranges, &sentStats,
        {}, 2, 0, 1, nullptr);
    receiver.join();
    ASSERT_TRUE(sendStatus.isOk()) << "send=" << sendStatus.message() << " receive=" << receiveStatus.message();
    ASSERT_TRUE(receiveStatus.isOk()) << receiveStatus.message();
    EXPECT_EQ(sentStats.files, 1U);
    EXPECT_EQ(receivedStats.files, 1U);
    std::ifstream input(destination.path / "large.bin", std::ios::binary);
    std::string actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(actual, content);
}
