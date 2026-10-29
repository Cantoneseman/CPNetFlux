#include "cpnetflux/core/io/persistent_data_session.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <array>
#include <future>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/core/io/payload_pipeline.h"
#include "cpnetflux/core/io/persistent_directory_batch.h"
#include "cpnetflux/core/io/work_executor.h"
#include <tuple>
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
    auto logicalHeader=data.value().header;
    auto prefix=(logicalHeader.flags&kDataChecksum)?4U:0U;
    logicalHeader.flags=0;logicalHeader.payloadSize-=prefix;
    ASSERT_EQ(data.value().header.flags & kDataChecksum, kDataChecksum);
    ASSERT_EQ(data.value().payload.size(), sizeof(bytes) + 4U);
    const auto& wirePayload = data.value().payload;
    const std::uint32_t wireCrc =
        (static_cast<std::uint32_t>(wirePayload[0]) << 24U) |
        (static_cast<std::uint32_t>(wirePayload[1]) << 16U) |
        (static_cast<std::uint32_t>(wirePayload[2]) << 8U) |
        static_cast<std::uint32_t>(wirePayload[3]);
    EXPECT_EQ(wireCrc, expected.value);
    EXPECT_TRUE(std::equal(wirePayload.begin() + 4, wirePayload.end(), bytes));
    ASSERT_TRUE(state.data(logicalHeader, data.value().payload.size()-prefix,
        data.value().payload.data()+prefix).isOk());
    ASSERT_TRUE(PersistentDataSession::writeEnd(&pair.sender, id, expected).isOk());
    auto end = PersistentDataSession::readNext(&pair.receiver, PersistentDataSession::kMaxPayload);
    ASSERT_TRUE(end.isOk()) << end.status().message();
    auto completed = state.end(end.value().header, end.value().payload.data(), end.value().payload.size());
    ASSERT_TRUE(completed.isOk()) << completed.status().message();
    EXPECT_EQ(completed.value().checksum.algorithm, expected.algorithm);
    EXPECT_EQ(completed.value().checksum.value, expected.value);
}

TEST(PersistentSessionTest, BuildsFileEndChecksumFromVerifiedFrameChecksums) {
    PersistentFileIdentity id{1, 1, 7, "combined.bin", "combined-transfer"};
    id.checksumAlgorithm = cpnetflux::checksum::ChecksumAlgorithm::Crc32c;
    PersistentDataSession state;
    ASSERT_TRUE(state.begin(beginHeader(7), id).isOk());

    const std::uint8_t bytes[]{'a', 'b', 'c', 'd', 'e', 'f', 'g'};
    const std::array<std::size_t, 2> lengths{2, 5};
    std::size_t offset = 0;
    for (const auto length : lengths) {
        auto header = beginHeader(7);
        header.type = FrameType::Data;
        header.offset = offset;
        header.payloadSize = length;
        cpnetflux::checksum::ChecksumComputer frame(id.checksumAlgorithm);
        frame.update(bytes + offset, length);
        ASSERT_TRUE(state.dataWithChecksum(header, length, frame.finalize()).isOk());
        offset += length;
    }

    cpnetflux::checksum::ChecksumComputer expected(id.checksumAlgorithm);
    expected.update(bytes, sizeof(bytes));
    const auto digest = expected.finalize();
    const std::uint8_t digestBytes[]{static_cast<std::uint8_t>(digest.value >> 24U),
        static_cast<std::uint8_t>(digest.value >> 16U),
        static_cast<std::uint8_t>(digest.value >> 8U), static_cast<std::uint8_t>(digest.value)};
    auto end = beginHeader(7);
    end.type = FrameType::FileEnd;
    end.offset = 7;
    end.payloadSize = sizeof(digestBytes);
    auto completed = state.end(end, digestBytes, sizeof(digestBytes));
    ASSERT_TRUE(completed.isOk()) << completed.status().message();
    EXPECT_EQ(completed.value().checksum.value, digest.value);
}

TEST(PersistentSessionTest, ChecksumAlgorithmMismatchDoesNotAdvanceSession) {
    PersistentFileIdentity id{1, 1, 3, "mismatch.bin", "mismatch-transfer"};
    id.checksumAlgorithm = cpnetflux::checksum::ChecksumAlgorithm::Crc32c;
    PersistentDataSession state;
    ASSERT_TRUE(state.begin(beginHeader(3), id).isOk());

    const std::uint8_t bytes[]{'x', 'y', 'z'};
    auto header = beginHeader(3);
    header.type = FrameType::Data;
    header.payloadSize = sizeof(bytes);
    EXPECT_FALSE(state.dataWithChecksum(header, sizeof(bytes),
        cpnetflux::checksum::ChecksumValue{cpnetflux::checksum::ChecksumAlgorithm::None, 0}).isOk());
    EXPECT_EQ(state.receivedBytes(), 0U);

    cpnetflux::checksum::ChecksumComputer frame(id.checksumAlgorithm);
    frame.update(bytes, sizeof(bytes));
    ASSERT_TRUE(state.dataWithChecksum(header, sizeof(bytes), frame.finalize()).isOk());
    EXPECT_EQ(state.receivedBytes(), sizeof(bytes));
}

TEST(PersistentSessionTest, MissingChecksumPayloadDoesNotAdvanceSession) {
    PersistentDataSession state;
    ASSERT_TRUE(state.begin(beginHeader(3), "missing.bin",
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c).isOk());
    auto header = beginHeader(3);
    header.type = FrameType::Data;
    header.payloadSize = 3;
    EXPECT_FALSE(state.data(header, 3).isOk());
    EXPECT_EQ(state.receivedBytes(), 0U);
    const std::uint8_t bytes[]{'x', 'y', 'z'};
    ASSERT_TRUE(state.data(header, sizeof(bytes), bytes).isOk());
    EXPECT_EQ(state.receivedBytes(), sizeof(bytes));
}

TEST(PersistentSessionTest, CombinesOnlyMissingRangeFramesInTransferOrder) {
    using cpnetflux::checksum::ChecksumAlgorithm;
    using cpnetflux::checksum::ChecksumComputer;
    PersistentFileIdentity id{1, 1, 16, "sparse.bin", "sparse-crc", 4, 0,
        ChecksumAlgorithm::Crc32c, {}, 2, 4, 8, 2};
    auto header = beginHeader(id.totalSize);
    header.rangeId = id.rangeId;
    header.flags = kFileResume;
    PersistentDataSession state;
    ASSERT_TRUE(state.begin(header, id).isOk());
    ASSERT_TRUE(state.setMissingRanges({{4, 6}, {9, 12}}).isOk());
    const std::uint8_t missing[]{'e', 'f', 'j', 'k', 'l'};
    ChecksumComputer first(ChecksumAlgorithm::Crc32c);
    first.update(missing, 2);
    header.type = FrameType::Data;
    header.flags = 0;
    header.payloadSize = 2;
    header.offset = 9;
    EXPECT_FALSE(state.dataWithChecksum(header, 2, first.finalize()).isOk());
    EXPECT_EQ(state.receivedBytes(), 4U);
    header.offset = 4;
    ASSERT_TRUE(state.dataWithChecksum(header, 2, first.finalize()).isOk());
    EXPECT_EQ(state.receivedBytes(), 9U);
    EXPECT_FALSE(state.dataWithChecksum(header, 2, first.finalize()).isOk());
    EXPECT_EQ(state.receivedBytes(), 9U);
    ChecksumComputer second(ChecksumAlgorithm::Crc32c);
    second.update(missing + 2, 3);
    header.offset = 9;
    header.payloadSize = 3;
    ASSERT_TRUE(state.dataWithChecksum(header, 3, second.finalize()).isOk());
    EXPECT_EQ(state.receivedBytes(), 12U);

    ChecksumComputer expected(ChecksumAlgorithm::Crc32c);
    expected.update(missing, sizeof(missing));
    const auto value = expected.finalize().value;
    std::uint8_t digest[]{static_cast<std::uint8_t>(value >> 24U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 8U), static_cast<std::uint8_t>(value)};
    header.type = FrameType::FileEnd;
    header.offset = 12;
    header.payloadSize = sizeof(digest);
    digest[0] ^= 0xffU;
    EXPECT_FALSE(state.end(header, digest, sizeof(digest)).isOk());
    EXPECT_TRUE(state.active());
    digest[0] ^= 0xffU;
    auto completed = state.end(header, digest, sizeof(digest));
    ASSERT_TRUE(completed.isOk()) << completed.status().message();
    EXPECT_EQ(completed.value().checksum.value, value);
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

TEST(PersistentSessionTest, InterruptedDownloadCheckpointIsAcceptedByV3Resume) {
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
        EXPECT_EQ(status.value(), i == 2 ? FrameStatusCode::OutputExists : FrameStatusCode::Ok);
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
    for (const std::uint32_t channelCount : {1U, 2U, 4U, 8U, 10U}) {
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
    auto logicalHeader=data.value().header;
    auto prefix=(logicalHeader.flags&kDataChecksum)?4U:0U;
    logicalHeader.flags=0;logicalHeader.payloadSize-=prefix;
    ASSERT_TRUE(state.data(logicalHeader, data.value().payload.size()-prefix,
        data.value().payload.data()+prefix).isOk());
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


TEST(PersistentTreeRecoveryTest, DirectoryEndCountsFilesOnCurrentConnectionAfterReconnect) {
    SocketPair pair;
    TempRoot source;
    TempRoot destination;
    const std::string content = "retry payload";
    {
        std::ofstream output(source.path / "retry.bin", std::ios::binary);
        output << content;
    }
    struct stat sourceStat{};
    ASSERT_EQ(::stat((source.path / "retry.bin").c_str(), &sourceStat), 0);
    PersistentFileIdentity file{1, 2, content.size(), "retry.bin", "retry-count",
        65536, sourceStat.st_mtim.tv_sec, cpnetflux::checksum::ChecksumAlgorithm::None};
    PersistentTreeStats receivedStats;
    cpnetflux::common::Status receiveStatus;
    std::jthread receiver([&] {
        receiveStatus = receivePersistentTree(&pair.receiver, destination.path.string(), false, "",
            &receivedStats);
    });
    PersistentTreeStats sentStats;
    sentStats.files = 1;  // The previous data connection completed one file.
    const auto sendStatus = sendPersistentTree(&pair.sender, source.path.string(), {file}, &sentStats);
    receiver.join();
    EXPECT_TRUE(sendStatus.isOk()) << sendStatus.message();
    EXPECT_TRUE(receiveStatus.isOk()) << receiveStatus.message();
    EXPECT_EQ(sentStats.files, 2U);  // Cumulative reporting remains intact.
    EXPECT_EQ(receivedStats.files, 1U);  // This connection carried one file.
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
    EXPECT_GT(sentStats.payloadPipelineBudgetBytes, 0U);
    EXPECT_GT(sentStats.payloadPipelineHighWatermarkBytes, 0U);
    EXPECT_LE(sentStats.payloadPipelineHighWatermarkBytes, sentStats.payloadPipelineBudgetBytes);
    std::ifstream input(destination.path / "large.bin", std::ios::binary);
    std::string actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(actual, content);
}

TEST(PersistentTreeRangeTest, IncompleteCoverageCannotPublishDespiteMatchingRangeCount) {
    SocketPair pair; TempRoot source; TempRoot destination;
    std::ofstream(source.path / "gap.bin") << "ABCDEFGH";
    struct stat st{}; ASSERT_EQ(::stat((source.path / "gap.bin").c_str(), &st),0);
    PersistentFileIdentity id{1,7,8,"gap.bin","gap-transfer",4,st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::None,{},1,0,4,1};
    PersistentTreeStats received, sent; cpnetflux::common::Status receiveStatus;
    std::jthread receiver([&] {
        receiveStatus = receivePersistentTree(&pair.receiver,destination.path.string(),false,"",
            &received,{},1,0,1,1,true,false);
    });
    auto status = sendPersistentTree(&pair.sender,source.path.string(),{id},&sent);
    ::shutdown(pair.sender.fd(),SHUT_RDWR);
    receiver.join();
    EXPECT_FALSE(status.isOk());
    EXPECT_FALSE(std::filesystem::exists(destination.path / "gap.bin"));
}

TEST(PersistentTreeRangeTest, DisconnectReleasesTransactionAndResumeRechecksCorruptCheckpoint) {
    TempRoot source, destination;
    std::ofstream(source.path/"resume.bin")<<"ABCDEFGH";
    struct stat st{}; ASSERT_EQ(::stat((source.path/"resume.bin").c_str(),&st),0);
    PersistentFileIdentity a{1,7,8,"resume.bin","resume-range",4,st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c,{},1,0,4,2};
    auto b=a; b.rangeId=2; b.rangeOffset=4;
    {
        SocketPair pair; PersistentTreeStats stats; cpnetflux::common::Status result;
        std::jthread receiver([&] {result=receivePersistentTree(&pair.receiver,destination.path.string(),
            false,"",&stats,{},1,0,1,1,true,false);});
        ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender,a).isOk());
        auto response=PersistentDataSession::readResumeResponse(&pair.sender,a);
        ASSERT_TRUE(response.isOk());
        const std::uint8_t bytes[]={'A','B','C','D'};
        cpnetflux::checksum::ChecksumComputer computer(a.checksumAlgorithm);
        computer.update(bytes,4);
        ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender,a,0,bytes,4).isOk());
        ASSERT_TRUE(PersistentDataSession::writeEnd(&pair.sender,a,computer.finalize()).isOk());
        auto done=PersistentDataSession::readResult(&pair.sender,a); ASSERT_TRUE(done.isOk());
        ASSERT_EQ(done.value(),FrameStatusCode::Ok);
        ::shutdown(pair.sender.fd(),SHUT_RDWR); receiver.join();
        EXPECT_FALSE(result.isOk());
    }
    auto saved=cpnetflux::checkpoint::ManifestStore::load(
        cpnetflux::checkpoint::manifestPathForOutput((destination.path/"resume.bin").string()));
    ASSERT_TRUE(saved.isOk()); ASSERT_EQ(saved.value().verifiedChunks.size(),1U);
    EXPECT_EQ(saved.value().state,cpnetflux::checkpoint::ManifestState::Failed);
    { std::fstream partial(saved.value().tempPath,std::ios::in|std::ios::out|std::ios::binary);
      partial.put('X'); }
    SocketPair pair; PersistentTreeStats received,sent; cpnetflux::common::Status result;
    std::jthread receiver([&] {result=receivePersistentTree(&pair.receiver,destination.path.string(),
        false,"",&received,{},1,0,1,1,true,true);});
    auto status=sendPersistentTree(&pair.sender,source.path.string(),{a,b},&sent);
    ::shutdown(pair.sender.fd(),SHUT_RDWR); receiver.join();
    ASSERT_TRUE(status.isOk())<<status.message(); ASSERT_TRUE(result.isOk())<<result.message();
    std::ifstream output(destination.path/"resume.bin");
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(output)),{}),"ABCDEFGH");
}

TEST(PersistentTreeRangeTest, SeparateChannelsShareOneTransactionAndCommitFullCoverage) {
    TempRoot source,destination; std::ofstream(source.path/"parallel.bin")<<"ABCDEFGH";
    struct stat st{}; ASSERT_EQ(::stat((source.path/"parallel.bin").c_str(),&st),0);
    auto context=makePersistentTransferContext();
    SocketPair left,right; PersistentTreeStats ra,rb,sa,sb;
    PersistentFileIdentity a{1,7,8,"parallel.bin","parallel-range",4,st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c,{},1,0,4,2};
    auto b=a; b.rangeId=2; b.rangeOffset=4;
    cpnetflux::common::Status l,r,sl,sr;
    std::jthread rxLeft([&] {l=receivePersistentTree(&left.receiver,destination.path.string(),
        false,"",&ra,{},1,0,2,kUnknownPersistentFileCount,true,false,context);});
    std::jthread rxRight([&] {r=receivePersistentTree(&right.receiver,destination.path.string(),
        false,"",&rb,{},1,1,2,kUnknownPersistentFileCount,true,false,context);});
    DynamicFileQueue queue({a,b},2,1,2);
    std::jthread txLeft([&] {sl=sendPersistentTree(&left.sender,source.path.string(),{},&sa,{},1,0,2,&queue);});
    std::jthread txRight([&] {sr=sendPersistentTree(&right.sender,source.path.string(),{},&sb,{},1,1,2,&queue);});
    txLeft.join();txRight.join();rxLeft.join();rxRight.join();
    EXPECT_TRUE(sl.isOk())<<sl.message();EXPECT_TRUE(sr.isOk())<<sr.message();
    EXPECT_TRUE(l.isOk())<<l.message();EXPECT_TRUE(r.isOk())<<r.message();
    std::ifstream output(destination.path/"parallel.bin");
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(output)),{}),"ABCDEFGH");
}

TEST(PersistentSessionTest, SparseResumeRejectsWritingVerifiedBytesAndRequiresAllMissingBytes) {
    PersistentDataSession state;
    auto header=beginHeader(12);header.flags=kFileResume;
    PersistentFileIdentity id{1,1,12,"sparse","sparse",4,0};
    ASSERT_TRUE(state.begin(header,id).isOk());
    ASSERT_TRUE(state.setMissingRanges({{4,8}}).isOk());
    header.type=FrameType::Data;header.flags=0;header.payloadSize=4;header.offset=0;
    std::uint8_t bytes[4]{};
    EXPECT_FALSE(state.data(header,4,bytes).isOk());
    header.offset=4;ASSERT_TRUE(state.data(header,4,bytes).isOk());
    header.offset=8;EXPECT_FALSE(state.data(header,4,bytes).isOk());
    header.type=FrameType::FileEnd;header.payloadSize=0;header.offset=12;
    ASSERT_TRUE(state.end(header).isOk());
}

TEST(PersistentTreeRangeTest, UnifiedPolicySupportsCompressionBufferedIoPreallocationAndAtomicOverwrite) {
    SocketPair pair; TempRoot source,destination;
    const std::string content(131072,'A');
    std::ofstream(source.path/"policy.bin")<<content;
    std::ofstream(destination.path/"policy.bin")<<"existing";
    struct stat st{};ASSERT_EQ(::stat((source.path/"policy.bin").c_str(),&st),0);
    PersistentFileIdentity id{1,7,content.size(),"policy.bin","policy",65536,st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c};
    cpnetflux::config::FileTransferOptions options;
    options.path=(source.path/"policy.bin").string();
    options.hotPathCompression.enabled=options.hotPathCompression.candidate=true;
    options.fileIo.bufferSize=131072;
    options.fileIo.posixWriteStrategy=cpnetflux::storage::PosixWriteStrategy::Coalesced;
    PersistentTransferPolicy policy;
    policy.options=options;policy.options.overwrite=true;
    policy.options.preallocateMode=cpnetflux::storage::PreallocateMode::Full;
    policy.options.commitSyncPolicy=cpnetflux::core::session::CommitSyncPolicy::FsyncFileAndDir;
    auto context=makePersistentTransferContext(policy);
    PersistentTreeStats received,sent;cpnetflux::common::Status result;
    std::jthread receiver([&] {result=receivePersistentTree(&pair.receiver,destination.path.string(),
        false,"",&received,{},1,0,1,1,false,false,context);});
    auto status=sendPersistentTree(&pair.sender,source.path.string(),{id},&sent,{},1,0,1,nullptr,&options);
    receiver.join();
    ASSERT_TRUE(status.isOk())<<status.message();ASSERT_TRUE(result.isOk())<<result.message();
    EXPECT_GT(sent.runtime.compressedFrames,0U);
    EXPECT_LT(sent.wireBytes,content.size());
    EXPECT_GT(received.fileIoStats.posixWriteSyscallCount(),0U);
    std::ifstream output(destination.path/"policy.bin");
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(output)),{}),content);
}

TEST(PersistentTreeRangeTest, RangeResumeConsumesCurrentChunkManifestAndSendsOnlyMissingBytes) {
    TempRoot source,destination;std::ofstream(source.path/"old.bin")<<"ABCDEFGH";
    struct stat st{};ASSERT_EQ(::stat((source.path/"old.bin").c_str(),&st),0);
    auto session=cpnetflux::core::session::TransferSession::createNew(
        (destination.path/"old.bin").string(),"old-resume",8,4,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    ASSERT_TRUE(session.isOk());ASSERT_TRUE(session.value().save().isOk());
    const std::uint8_t first[]={'A','B','C','D'};
    cpnetflux::checksum::ChecksumComputer checksum(cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    checksum.update(first,4);
    { std::ofstream temp(session.value().manifest().tempPath);temp<<"ABCDxxxx"; }
    ASSERT_TRUE(session.value().recordVerifiedChunk(0,0,4,checksum.finalize()).isOk());
    ASSERT_TRUE(session.value().markFailed().isOk());
    PersistentFileIdentity a{1,7,8,"old.bin","old-resume",4,st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c,{},1,0,4,2};
    auto b=a;b.rangeId=2;b.rangeOffset=4;
    SocketPair pair;PersistentTreeStats received,sent;cpnetflux::common::Status result;
    std::jthread receiver([&] {result=receivePersistentTree(&pair.receiver,destination.path.string(),
        false,"",&received,{},1,0,1,1,true,true);});
    auto status=sendPersistentTree(&pair.sender,source.path.string(),{a,b},&sent);
    receiver.join();ASSERT_TRUE(status.isOk())<<status.message();ASSERT_TRUE(result.isOk())<<result.message();
    EXPECT_EQ(sent.payloadBytes,4U);EXPECT_EQ(sent.skippedBytes,4U);
    std::ifstream output(destination.path/"old.bin");
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(output)),{}),"ABCDEFGH");
}

TEST(PersistentTreeRangeTest, FramesCrossingChunkBoundariesKeepPerChunkChecksums) {
    using cpnetflux::checksum::ChecksumAlgorithm;
    using cpnetflux::checksum::ChecksumComputer;
    SocketPair pair;
    TempRoot destination;
    PersistentFileIdentity id{1, 1, 10, "cross-chunk.bin", "cross-chunk", 4, 0,
        ChecksumAlgorithm::Crc32c};
    const std::uint8_t bytes[]{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'};
    ASSERT_TRUE(PersistentDataSession::setTimeout(&pair.receiver, 2).isOk());
    ASSERT_TRUE(PersistentDataSession::setTimeout(&pair.sender, 2).isOk());
    PersistentTreeStats stats;
    cpnetflux::common::Status result;
    std::jthread receiver([&] {
        result = receivePersistentTree(&pair.receiver, destination.path.string(), false, "", &stats);
    });
    ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, id).isOk());
    ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender, id, 0, bytes, 6).isOk());
    ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender, id, 6, bytes + 6, 4).isOk());
    ChecksumComputer whole(id.checksumAlgorithm);
    whole.update(bytes, sizeof(bytes));
    ASSERT_TRUE(PersistentDataSession::writeEnd(&pair.sender, id, whole.finalize()).isOk());
    auto completed = PersistentDataSession::readResult(&pair.sender, id);
    ASSERT_TRUE(completed.isOk()) << completed.status().message();
    EXPECT_EQ(completed.value(), FrameStatusCode::Ok);
    ASSERT_TRUE(PersistentDataSession::writeDirectoryEnd(&pair.sender, 1).isOk());
    receiver.join();
    ASSERT_TRUE(result.isOk()) << result.message();
    auto manifest = cpnetflux::checkpoint::ManifestStore::load(
        cpnetflux::checkpoint::manifestPathForOutput((destination.path / id.relativePath).string()));
    ASSERT_TRUE(manifest.isOk()) << manifest.status().message();
    ASSERT_EQ(manifest.value().verifiedChunks.size(), 3U);
    for (const auto& record : manifest.value().verifiedChunks) {
        ChecksumComputer chunk(id.checksumAlgorithm);
        chunk.update(bytes + record.offset, record.length);
        EXPECT_EQ(record.checksum.value, chunk.finalize().value);
    }
    std::ifstream output(destination.path / id.relativePath, std::ios::binary);
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(output)), {}), "abcdefghij");
}

TEST(PersistentTreeRangeTest, BadDataChecksumNeverBecomesAResumeCheckpoint) {
    SocketPair pair;TempRoot source,destination;
    std::ofstream(source.path/"bad.bin")<<std::string(65536,'A');
    struct stat st{};ASSERT_EQ(::stat((source.path/"bad.bin").c_str(),&st),0);
    PersistentFileIdentity id{1,7,65536,"bad.bin","bad-checksum",65536,st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c};
    cpnetflux::config::FileTransferOptions options;
    options.path=(source.path/"bad.bin").string();options.hasCorruptChunk=true;options.corruptChunk=0;
    PersistentTreeStats received,sent;cpnetflux::common::Status result;
    std::jthread receiver([&] {
        result=receivePersistentTree(&pair.receiver,destination.path.string(),false,"",&received);
        ::shutdown(pair.receiver.fd(),SHUT_RDWR);
    });
    auto status=sendPersistentTree(&pair.sender,source.path.string(),{id},&sent,{},1,0,1,nullptr,&options);
    receiver.join();EXPECT_FALSE(status.isOk());EXPECT_FALSE(result.isOk());
    auto checkpoint=cpnetflux::checkpoint::ManifestStore::load(
        cpnetflux::checkpoint::manifestPathForOutput((destination.path/"bad.bin").string()));
    ASSERT_TRUE(checkpoint.isOk());EXPECT_TRUE(checkpoint.value().verifiedChunks.empty());
    EXPECT_FALSE(std::filesystem::exists(destination.path/"bad.bin"));
}


TEST(PersistentTreeLeaseTest, ReopenedWorkerUsesCurrentEpochThroughActualSender) {
    TempRoot source, destination;
    std::ofstream(source.path / "retry.bin") << "ABCDEFGH";
    struct stat st{};
    ASSERT_EQ(::stat((source.path / "retry.bin").c_str(), &st), 0);
    PersistentFileIdentity id{1, 7, 8, "retry.bin", "lease-retry", 4, st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c};
    DynamicFileQueue queue(std::vector<PersistentFileIdentity>{id}, 1, 1, 1);
    auto owner = queue.worker(0).value();
    auto first = queue.claim(owner); ASSERT_TRUE(first.isOk()); ASSERT_TRUE(first.value());
    ASSERT_TRUE(queue.begin(*first.value()).isOk());
    ASSERT_TRUE(queue.disconnect(owner).isOk());
    ASSERT_TRUE(queue.requeue(*first.value()).isOk());
    auto reopened = queue.reopen(owner); ASSERT_TRUE(reopened.isOk());
    ASSERT_EQ(reopened.value().epoch.value, 2U);
    SocketPair pair; PersistentTreeStats sent, received;
    cpnetflux::common::Status receiverStatus;
    std::jthread receiver([&] {
        receiverStatus = receivePersistentTree(&pair.receiver, destination.path.string(), false,
            "", &received, {}, 1, 0, 1, 1, true);
    });
    std::uint64_t completedAttempt = 0;
    auto senderStatus = sendPersistentTree(&pair.sender, source.path.string(), {}, &sent,
        [&](const PersistentFileIdentity& file, const cpnetflux::common::Status& status, bool complete) {
            if (complete && status.isOk()) completedAttempt = file.attempt;
            return cpnetflux::common::Status::ok();
        }, 1, 0, 1, &queue);
    ::shutdown(pair.sender.fd(), SHUT_RDWR);
    receiver.join();
    ASSERT_TRUE(senderStatus.isOk()) << senderStatus.message();
    ASSERT_TRUE(receiverStatus.isOk()) << receiverStatus.message();
    EXPECT_EQ(completedAttempt, 1U);
    EXPECT_EQ(queue.outstanding(), 0U);
    EXPECT_FALSE(queue.commit(*first.value()).isOk());
    std::ifstream output(destination.path / "retry.bin");
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(output)), {}), "ABCDEFGH");
}

namespace {
class PersistentRecoveryTest : public ::testing::TestWithParam<std::tuple<bool, bool>> {};
}

TEST_P(PersistentRecoveryTest, ReopenedTransportSendsOnlyManifestMissingPayload) {
    const auto [download, ranged] = GetParam();
    TempRoot source, destination;
    const auto sourcePath = source.path / "recover.bin";
    std::ofstream(sourcePath, std::ios::binary) << "ABCDEFGH";
    struct stat st{}; ASSERT_EQ(::stat(sourcePath.c_str(), &st), 0);
    PersistentFileIdentity id{1, 7, 8, "recover.bin", "recover-transfer", 4, st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c};
    // A single full-file range still uses the range coordinator and sparse handshake.
    if (ranged) { id.rangeId = 1; id.rangeLength = 8; id.rangeCount = 1; }
    DynamicFileQueue queue(std::vector<PersistentFileIdentity>{id}, 1, 1, 1);
    WorkExecutor executor(queue, queue.worker(0).value());
    auto next = executor.claim(); ASSERT_TRUE(next.isOk()); ASSERT_TRUE(next.value());
    ASSERT_TRUE(executor.begin(*next.value()).isOk());
    {
        SocketPair broken;
        PersistentTreeStats partial;
        cpnetflux::common::Status interrupted;
        std::jthread receiver([&] {
            interrupted = receivePersistentTree(&broken.receiver, destination.path.string(),
                download, "remote/tree", &partial, {}, 1, 0, 1, 1, true);
        });
        ASSERT_TRUE(PersistentDataSession::writeBegin(&broken.sender, id).isOk());
        if (ranged) {
            auto missing = PersistentDataSession::readResumeResponse(&broken.sender, id);
            ASSERT_TRUE(missing.isOk());
        }
        const std::uint8_t bytes[]{'A', 'B', 'C', 'D'};
        ASSERT_TRUE(PersistentDataSession::writeData(&broken.sender, id, 0, bytes, sizeof(bytes)).isOk());
        ::shutdown(broken.sender.fd(), SHUT_RDWR);
        receiver.join();
        EXPECT_FALSE(interrupted.isOk());
        EXPECT_EQ(partial.payloadBytes, 4U);
    } // Old sockets closed and receiver joined before disconnect/recover.
    const auto output = (destination.path / "recover.bin").string();
    if (download) {
        auto manifest = cpnetflux::checkpoint::loadDownloadManifest(
            cpnetflux::checkpoint::downloadManifestPathForOutput(output));
        ASSERT_TRUE(manifest.isOk()); ASSERT_EQ(manifest.value().verifiedChunks.size(), 1U);
        EXPECT_EQ(manifest.value().verifiedChunks.front().length, 4U);
    } else {
        auto manifest = cpnetflux::checkpoint::ManifestStore::load(
            cpnetflux::checkpoint::manifestPathForOutput(output));
        ASSERT_TRUE(manifest.isOk()); ASSERT_EQ(manifest.value().verifiedChunks.size(), 1U);
        EXPECT_EQ(manifest.value().verifiedChunks.front().length, 4U);
    }
    ASSERT_TRUE(queue.disconnect(executor.owner()).isOk());
    auto recovered = executor.recover({{*next.value(), WorkRecoveryDisposition::Missing}}, 1);
    ASSERT_TRUE(recovered.isOk()) << recovered.status().message();
    EXPECT_EQ(recovered.value().epoch.value, 2U);
    EXPECT_FALSE(executor.commit(*next.value()).isOk());

    SocketPair resumed;
    PersistentTreeStats sent, received;
    DynamicFileQueue incoming(std::vector<WorkItem>{}, 1, 1, 1);
    ASSERT_TRUE(incoming.setConnectionEpoch({2}).isOk());
    cpnetflux::common::Status receiverStatus;
    std::jthread receiver([&] {
        receiverStatus = receivePersistentTree(&resumed.receiver, destination.path.string(),
            download, "remote/tree", &received, {}, 1, 0, 1, 1, true, true, {}, &incoming);
    });
    cpnetflux::config::FileTransferOptions options; options.resume = true;
    std::uint64_t completedAttempt = 0;
    auto senderStatus = sendPersistentTree(&resumed.sender, source.path.string(), {}, &sent,
        [&](const PersistentFileIdentity& file, const cpnetflux::common::Status& status, bool complete) {
            if (complete && status.isOk()) completedAttempt = file.attempt;
            return cpnetflux::common::Status::ok();
        }, 1, 0, 1, &queue, &options);
    ::shutdown(resumed.sender.fd(), SHUT_RDWR);
    receiver.join();
    ASSERT_TRUE(senderStatus.isOk()) << senderStatus.message();
    ASSERT_TRUE(receiverStatus.isOk()) << receiverStatus.message();
    EXPECT_EQ(sent.payloadBytes, 4U);
    EXPECT_EQ(sent.skippedBytes, 4U);
    EXPECT_EQ(received.payloadBytes, 4U);
    EXPECT_EQ(completedAttempt, 1U);
    EXPECT_EQ(queue.outstanding(), 0U);
    EXPECT_EQ(incoming.outstanding(), 0U);
    std::ifstream file(output, std::ios::binary);
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(file)), {}), "ABCDEFGH");
}

INSTANTIATE_TEST_SUITE_P(UploadDownloadWholeAndRange, PersistentRecoveryTest,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));

TEST(PersistentTreeLeaseTest, UnreadResultReconcilesCommittedManifestWithoutPayloadRetry) {
    for (const bool download : {false, true}) {
        TempRoot destination;
        PersistentFileIdentity id{1, 7, 8, "committed.bin", "lost-result-transfer", 4, 12,
            cpnetflux::checksum::ChecksumAlgorithm::Crc32c};
        DynamicFileQueue queue(std::vector<PersistentFileIdentity>{id}, 1, 1, 1);
        WorkExecutor executor(queue, queue.worker(0).value());
        auto lease = executor.claim(); ASSERT_TRUE(lease.isOk()); ASSERT_TRUE(lease.value());
        ASSERT_TRUE(executor.begin(*lease.value()).isOk());
        {
            SocketPair pair;
            PersistentTreeStats received;
            cpnetflux::common::Status receiverStatus;
            std::jthread receiver([&] {
                receiverStatus = receivePersistentTree(&pair.receiver, destination.path.string(),
                    download, "remote/tree", &received, {}, 1, 0, 1, 1, true);
            });
            const std::uint8_t payload[]{'A','B','C','D','E','F','G','H'};
            cpnetflux::checksum::ChecksumComputer digest(id.checksumAlgorithm);
            digest.update(payload, sizeof(payload));
            ASSERT_TRUE(PersistentDataSession::writeBegin(&pair.sender, id).isOk());
            ASSERT_TRUE(PersistentDataSession::writeData(&pair.sender, id, 0, payload, sizeof(payload)).isOk());
            ASSERT_TRUE(PersistentDataSession::writeEnd(&pair.sender, id, digest.finalize()).isOk());
            ASSERT_TRUE(executor.awaitResult(*lease.value()).isOk());
            ASSERT_TRUE(PersistentDataSession::writeDirectoryEnd(&pair.sender, 1).isOk());
            receiver.join();
            ASSERT_TRUE(receiverStatus.isOk()) << receiverStatus.message();
            // Leave FILE_RESULT unread: sender has no terminal acknowledgement.
        }
        const auto output = (destination.path / id.relativePath).string();
        if (download) {
            auto manifest = cpnetflux::checkpoint::loadDownloadManifest(
                cpnetflux::checkpoint::downloadManifestPathForOutput(output));
            ASSERT_TRUE(manifest.isOk());
            EXPECT_EQ(manifest.value().state, cpnetflux::checkpoint::ManifestState::Committed);
        } else {
            auto manifest = cpnetflux::checkpoint::ManifestStore::load(
                cpnetflux::checkpoint::manifestPathForOutput(output));
            ASSERT_TRUE(manifest.isOk());
            EXPECT_EQ(manifest.value().state, cpnetflux::checkpoint::ManifestState::Committed);
        }
        std::ifstream file(output, std::ios::binary);
        ASSERT_EQ(std::string((std::istreambuf_iterator<char>(file)), {}), "ABCDEFGH");
        ASSERT_TRUE(queue.disconnect(executor.owner()).isOk());
        ASSERT_TRUE(executor.recover({{*lease.value(), WorkRecoveryDisposition::Committed}}, 0).isOk());
        auto empty = executor.claim(); ASSERT_TRUE(empty.isOk()); EXPECT_FALSE(empty.value());
        EXPECT_EQ(queue.outstanding(), 0U);
        EXPECT_FALSE(executor.commit(*lease.value()).isOk());
    }
}

namespace {
class PersistentMultiRangeRecoveryTest : public ::testing::TestWithParam<bool> {};
}

TEST_P(PersistentMultiRangeRecoveryTest, PublishesAfterMissingRangeWhenPriorRangeIsAlreadyVerified) {
    const bool download = GetParam();
    TempRoot source, destination;
    std::ofstream(source.path / "ranges.bin", std::ios::binary) << "ABCDEFGHIJKLMNOP";
    struct stat st{}; ASSERT_EQ(::stat((source.path / "ranges.bin").c_str(), &st), 0);
    PersistentFileIdentity first{1, 7, 16, "ranges.bin", "multirange-recovery", 4,
        st.st_mtim.tv_sec, cpnetflux::checksum::ChecksumAlgorithm::Crc32c, {}, 1, 0, 8, 2};
    auto second = first; second.rangeId = 2; second.rangeOffset = 8;
    DynamicFileQueue queue(std::vector<PersistentFileIdentity>{first, second}, 1, 1, 2, true);
    WorkExecutor executor(queue, queue.worker(0).value());
    std::optional<WorkLease> interruptedLease;
    {
        SocketPair broken;
        PersistentTreeStats partial;
        cpnetflux::common::Status interrupted;
        std::jthread receiver([&] {
            interrupted = receivePersistentTree(&broken.receiver, destination.path.string(),
                download, "remote/tree", &partial, {}, 1, 0, 1, 1, true);
        });
        auto a = executor.claim(); ASSERT_TRUE(a.isOk()); ASSERT_TRUE(a.value());
        ASSERT_EQ(a.value()->file.rangeId, 1U);
        ASSERT_TRUE(executor.begin(*a.value()).isOk());
        ASSERT_TRUE(PersistentDataSession::writeBegin(&broken.sender, a.value()->file).isOk());
        ASSERT_TRUE(PersistentDataSession::readResumeResponse(&broken.sender, a.value()->file).isOk());
        const std::uint8_t bytes[]{'A','B','C','D','E','F','G','H','I','J','K','L'};
        cpnetflux::checksum::ChecksumComputer digest(first.checksumAlgorithm);
        digest.update(bytes, 8);
        for (std::uint64_t offset : {0U, 4U}) {
            ASSERT_TRUE(PersistentDataSession::writeData(&broken.sender,
                a.value()->file, offset, bytes + offset, 4).isOk());
        }
        ASSERT_TRUE(PersistentDataSession::writeEnd(&broken.sender, a.value()->file, digest.finalize()).isOk());
        ASSERT_TRUE(executor.awaitResult(*a.value()).isOk());
        auto result = PersistentDataSession::readResult(&broken.sender, a.value()->file);
        ASSERT_TRUE(result.isOk()); ASSERT_EQ(result.value(), FrameStatusCode::Ok);
        ASSERT_TRUE(executor.finalize(*a.value(), cpnetflux::common::Status::ok()).isOk());
        auto b = executor.claim(); ASSERT_TRUE(b.isOk()); ASSERT_TRUE(b.value());
        interruptedLease = *b.value();
        ASSERT_EQ(b.value()->file.rangeId, 2U);
        ASSERT_TRUE(executor.begin(*b.value()).isOk());
        ASSERT_TRUE(PersistentDataSession::writeBegin(&broken.sender, b.value()->file).isOk());
        ASSERT_TRUE(PersistentDataSession::readResumeResponse(&broken.sender, b.value()->file).isOk());
        ASSERT_TRUE(PersistentDataSession::writeData(&broken.sender,
            b.value()->file, 8, bytes + 8, 4).isOk());
        ::shutdown(broken.sender.fd(), SHUT_RDWR);
        receiver.join();
        EXPECT_FALSE(interrupted.isOk());
        EXPECT_EQ(partial.payloadBytes, 12U);
    }
    ASSERT_TRUE(interruptedLease.has_value());
    ASSERT_TRUE(queue.disconnect(executor.owner()).isOk());
    ASSERT_TRUE(executor.recover({{*interruptedLease, WorkRecoveryDisposition::Missing}}, 1).isOk());

    SocketPair resumed;
    PersistentTreeStats sent, received;
    cpnetflux::common::Status receiverStatus;
    std::jthread receiver([&] {
        receiverStatus = receivePersistentTree(&resumed.receiver, destination.path.string(),
            download, "remote/tree", &received, {}, 1, 0, 1, 1, true, true);
    });
    cpnetflux::config::FileTransferOptions options; options.resume = true;
    auto sentStatus = sendPersistentTree(&resumed.sender, source.path.string(), {}, &sent,
        {}, 1, 0, 1, &queue, &options);
    ::shutdown(resumed.sender.fd(), SHUT_RDWR);
    receiver.join();
    ASSERT_TRUE(sentStatus.isOk()) << sentStatus.message();
    ASSERT_TRUE(receiverStatus.isOk()) << receiverStatus.message();
    EXPECT_EQ(sent.payloadBytes, 4U);
    EXPECT_EQ(sent.skippedBytes, 4U);
    const auto output = destination.path / "ranges.bin";
    ASSERT_TRUE(std::filesystem::exists(output)) << "complete verified coverage must publish the file";
    std::ifstream file(output, std::ios::binary);
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(file)), {}), "ABCDEFGHIJKLMNOP");

}

TEST_P(PersistentMultiRangeRecoveryTest, ReplaysVerifiedPartitionAfterFirstSkipPublishes) {
    const bool download = GetParam();
    TempRoot source, destination;
    std::ofstream(source.path / "ranges.bin", std::ios::binary) << "ABCDEFGHIJKLMNOP";
    struct stat st{}; ASSERT_EQ(::stat((source.path / "ranges.bin").c_str(), &st), 0);
    PersistentFileIdentity first{1, 7, 16, "ranges.bin", "verified-partition", 4,
        st.st_mtim.tv_sec, cpnetflux::checksum::ChecksumAlgorithm::Crc32c, {}, 1, 0, 8, 2};
    auto second = first; second.rangeId = 2; second.rangeOffset = 8;
    {
        SocketPair broken;
        PersistentTreeStats partial;
        cpnetflux::common::Status interrupted;
        std::jthread receiver([&] {
            interrupted = receivePersistentTree(&broken.receiver, destination.path.string(),
                download, "remote/tree", &partial, {}, 1, 0, 1, 1, true);
        });
        const std::uint8_t bytes[]{'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P'};
        for (const auto& id : {first, second}) {
            ASSERT_TRUE(PersistentDataSession::writeBegin(&broken.sender, id).isOk());
            ASSERT_TRUE(PersistentDataSession::readResumeResponse(&broken.sender, id).isOk());
            for (auto offset = id.rangeOffset; offset < id.rangeOffset + id.rangeLength; offset += 4)
                ASSERT_TRUE(PersistentDataSession::writeData(&broken.sender, id, offset, bytes + offset, 4).isOk());
            if (id.rangeId == first.rangeId) {
                cpnetflux::checksum::ChecksumComputer digest(id.checksumAlgorithm);
                digest.update(bytes, 8);
                ASSERT_TRUE(PersistentDataSession::writeEnd(&broken.sender, id, digest.finalize()).isOk());
                auto result = PersistentDataSession::readResult(&broken.sender, id);
                ASSERT_TRUE(result.isOk()); ASSERT_EQ(result.value(), FrameStatusCode::Ok);
            }
        }
        // All chunks were checkpointed, but the final range END was lost.
        ::shutdown(broken.sender.fd(), SHUT_RDWR);
        receiver.join();
        ASSERT_FALSE(interrupted.isOk());
        ASSERT_EQ(partial.payloadBytes, 16U);
        ASSERT_FALSE(std::filesystem::exists(destination.path / "ranges.bin"));
    }
    // Replay the original partition. The first skip can publish full coverage;
    // the later slot must still receive an empty missing-set and finish safely.
    cpnetflux::config::FileTransferOptions options; options.resume = true;
    SocketPair committed;
    PersistentTreeStats skipped, validated;
    DynamicFileQueue replay(std::vector<PersistentFileIdentity>{first, second}, 1, 1, 2, true);
    cpnetflux::common::Status replayReceiverStatus;
    std::jthread replayReceiver([&] {
        replayReceiverStatus = receivePersistentTree(&committed.receiver, destination.path.string(),
            download, "remote/tree", &validated, {}, 1, 0, 1, 1, true, true);
    });
    auto replayStatus = sendPersistentTree(&committed.sender, source.path.string(), {}, &skipped,
        {}, 1, 0, 1, &replay, &options);
    ::shutdown(committed.sender.fd(), SHUT_RDWR);
    replayReceiver.join();
    ASSERT_TRUE(replayStatus.isOk()) << replayStatus.message();
    ASSERT_TRUE(replayReceiverStatus.isOk()) << replayReceiverStatus.message();
    EXPECT_EQ(skipped.payloadBytes, 0U); EXPECT_EQ(skipped.skippedBytes, 16U);
    EXPECT_EQ(validated.payloadBytes, 0U);
}


TEST(PersistentDirectoryBatchTest, DetectsDataResetDuringPeerReadyWaitAndAllowsReattach) {
    PersistentDirectoryBatch batch(1,2);
    ASSERT_TRUE(batch.attach(0).isOk());
    ASSERT_TRUE(batch.attach(1).isOk());
    SocketPair failedLane;
    batch.bind(0,-1,failedLane.sender.fd(),true);

    const auto started=std::chrono::steady_clock::now();
    failedLane.receiver=FramedDataSocket{};
    const auto failed=batch.waitReady(0,std::chrono::seconds(1));
    const auto elapsed=std::chrono::steady_clock::now()-started;
    ASSERT_EQ(failed.code(),cpnetflux::common::StatusCode::TransportError) << failed.message();
    EXPECT_LT(elapsed,std::chrono::milliseconds(500));
    EXPECT_TRUE(batch.status().isOk());

    SocketPair reattachedLane;
    ASSERT_TRUE(batch.attach(0).isOk());
    batch.bind(0,-1,reattachedLane.sender.fd(),true);
    batch.bind(1,-1,-1,true);
    EXPECT_TRUE(batch.waitReady(0,std::chrono::milliseconds(50)).isOk());
}

TEST(PersistentDirectoryBatchTest, ReattachesOnlyFailedChannelAndKeepsCommittedRanges) {
    PersistentDirectoryBatch batch(2, 2);
    ASSERT_TRUE(batch.attach(0).isOk());
    ASSERT_TRUE(batch.attach(1).isOk());
    batch.bind(0, -1, -1, true);
    batch.bind(1, -1, -1, true);
    ASSERT_TRUE(batch.waitReady(0,std::chrono::milliseconds(50)).isOk());

    PersistentFileIdentity failed{1, 1, 1, "failed.bin", "failed-transfer"};
    PersistentFileIdentity healthy{2, 1, 1, "healthy.bin", "healthy-transfer"};
    ASSERT_TRUE(batch.fileEvent(0, failed, cpnetflux::common::Status::ok(), false).isOk());
    ASSERT_TRUE(batch.fileEvent(1, healthy, cpnetflux::common::Status::ok(), false).isOk());
    ASSERT_TRUE(batch.fileEvent(1, healthy, cpnetflux::common::Status::ok(), true).isOk());

    ASSERT_TRUE(batch.failChannel(0, cpnetflux::common::Status::transportError("data reset")).isOk());
    batch.unbind(0);
    ASSERT_TRUE(batch.attach(0).isOk());
    batch.bind(0, -1, -1, true);
    ASSERT_TRUE(batch.waitReady(0,std::chrono::milliseconds(50)).isOk());

    failed.attempt = 1;
    ASSERT_TRUE(batch.fileEvent(0, failed, cpnetflux::common::Status::ok(), false).isOk());
    ASSERT_TRUE(batch.fileEvent(0, failed, cpnetflux::common::Status::ok(), true).isOk());

    cpnetflux::common::Status healthyFinish, failedFinish;
    std::jthread healthyThread([&] {
        healthyFinish = batch.finish(1, std::chrono::seconds(1));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    failedFinish = batch.finish(0, std::chrono::seconds(1));
    healthyThread.join();
    EXPECT_TRUE(failedFinish.isOk()) << failedFinish.message();
    EXPECT_TRUE(healthyFinish.isOk()) << healthyFinish.message();
    EXPECT_TRUE(batch.status().isOk());
}

TEST(PersistentDirectoryBatchTest, HealthyLaneFinishesBeforeFailedLaneReattach) {
    int oldControlFds[2], retryControlFds[2], healthyControlFds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, oldControlFds), 0);
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, retryControlFds), 0);
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, healthyControlFds), 0);
    UniqueFd oldControl(oldControlFds[0]), oldControlPeer(oldControlFds[1]);
    UniqueFd retryControl(retryControlFds[0]), retryControlPeer(retryControlFds[1]);
    UniqueFd healthyControl(healthyControlFds[0]), healthyControlPeer(healthyControlFds[1]);

    PersistentDirectoryBatch batch(1, 2);
    ASSERT_TRUE(batch.attach(0).isOk());
    ASSERT_TRUE(batch.attach(1).isOk());
    batch.bind(0, oldControl.get(), -1, true);
    batch.bind(1, healthyControl.get(), -1, true);

    PersistentFileIdentity first{1, 1, 8, "ranges.bin", "ranges-transfer", 4};
    first.rangeId = 1;
    first.rangeOffset = 0;
    first.rangeLength = 4;
    first.rangeCount = 2;
    auto second = first;
    second.rangeId = 2;
    second.rangeOffset = 4;

    ASSERT_TRUE(batch.fileEvent(0, first, cpnetflux::common::Status::ok(), false).isOk());
    ASSERT_TRUE(batch.fileEvent(1, second, cpnetflux::common::Status::ok(), false).isOk());
    ASSERT_TRUE(batch.fileEvent(1, second, cpnetflux::common::Status::ok(), true).isOk());
    ASSERT_TRUE(batch.failChannel(
        0, cpnetflux::common::Status::transportError("data reset")).isOk());
    // A retry may attach before the failed control thread finishes cleanup.
    ASSERT_TRUE(batch.attach(0).isOk());
    batch.bind(0, retryControl.get(), -1, true);
    // Cleanup from the old control handler must not erase the newly attached lane.
    batch.unbind(0, oldControl.get());

    // The healthy lane must not wait for the failed lane's 30-second batch timeout.
    ASSERT_TRUE(batch.finish(1, std::chrono::seconds(1)).isOk());
    batch.unbind(1, healthyControl.get());

    ASSERT_TRUE(batch.waitReady(0,std::chrono::milliseconds(50)).isOk());
    first.attempt = 1;
    ASSERT_TRUE(batch.fileEvent(0, first, cpnetflux::common::Status::ok(), false).isOk());
    ASSERT_TRUE(batch.fileEvent(0, first, cpnetflux::common::Status::ok(), true).isOk());
    EXPECT_TRUE(batch.finish(0, std::chrono::seconds(1)).isOk());
    EXPECT_TRUE(batch.status().isOk());
}

INSTANTIATE_TEST_SUITE_P(UploadDownload, PersistentMultiRangeRecoveryTest, ::testing::Bool());

TEST(PersistentSessionTest, LogicalLinkMatrixRangesPartitionSourceExactlyOnce) {
    for (const std::uint32_t lanes : {1U, 2U, 4U, 8U, 10U}) {
        PersistentFileIdentity file{1, 1, (lanes * 16ULL + 1) * 1024 * 1024 + 19,
                                    "large.bin", "partition"};
        file.chunkSize = 65536;
        auto ranges = expandPersistentFileRanges({file}, lanes);
        ASSERT_TRUE(ranges.isOk()) << ranges.status().message();
        if (lanes == 1) {
            ASSERT_EQ(ranges.value().size(), 1U);
            EXPECT_EQ(ranges.value()[0].rangeId, 0U);
            continue;
        }
        std::uint64_t offset = 0;
        std::vector<unsigned> work(lanes);
        for (const auto& range : ranges.value()) {
            EXPECT_EQ(range.rangeOffset, offset);
            EXPECT_GT(range.rangeLength, 0U);
            EXPECT_EQ(range.rangeCount, ranges.value().size());
            EXPECT_EQ(range.rangeOffset % file.chunkSize, 0U);
            ++work[(range.rangeId - 1) % lanes];
            offset += range.rangeLength;
        }
        EXPECT_EQ(offset, file.totalSize);
        for (auto count : work) EXPECT_GT(count, 0U);
    }
}


TEST(BoundedPayloadPipelineTest, BlocksAtByteBudgetAndWakesWhenConsumerPops) {
    using cpnetflux::core::io::BoundedPayloadPipeline;
    using cpnetflux::core::io::PreparedPayload;
    BoundedPayloadPipeline pipeline(8, 2);
    ASSERT_TRUE(pipeline.push(PreparedPayload{0, std::vector<std::uint8_t>(8, 1)}).isOk());
    auto waiting = std::async(std::launch::async, [&] {
        return pipeline.push(PreparedPayload{8, std::vector<std::uint8_t>(1, 2)});
    });
    EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    auto first = pipeline.pop();
    ASSERT_TRUE(first.isOk());
    ASSERT_TRUE(first.value());
    EXPECT_EQ(first.value()->bytes.size(), 8U);
    ASSERT_TRUE(waiting.get().isOk());
    EXPECT_EQ(pipeline.highWatermarkBytes(), 8U);
    EXPECT_GT(pipeline.producerWaitCount(), 0U);
}

TEST(BoundedPayloadPipelineTest, CancellationWakesProducerAndConsumerWithoutLeakingBytes) {
    using cpnetflux::core::io::BoundedPayloadPipeline;
    using cpnetflux::core::io::PreparedPayload;
    BoundedPayloadPipeline pipeline(4, 1);
    ASSERT_TRUE(pipeline.push(PreparedPayload{0, std::vector<std::uint8_t>(4, 1)}).isOk());
    auto waiting = std::async(std::launch::async, [&] {
        return pipeline.push(PreparedPayload{4, std::vector<std::uint8_t>(1, 2)});
    });
    EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    pipeline.cancel(cpnetflux::common::Status::transportError("pipeline cancelled"));
    EXPECT_FALSE(waiting.get().isOk());
    EXPECT_FALSE(pipeline.pop().isOk());
    EXPECT_EQ(pipeline.queuedBytes(), 0U);
    EXPECT_EQ(pipeline.status().message(), "pipeline cancelled");
}

TEST(BoundedPayloadPipelineTest, CloseDrainsQueuedChunksAndThenReturnsEndOfStream) {
    using cpnetflux::core::io::BoundedPayloadPipeline;
    using cpnetflux::core::io::PreparedPayload;
    BoundedPayloadPipeline pipeline(16, 2);
    ASSERT_TRUE(pipeline.push(PreparedPayload{0, {1, 2, 3}}).isOk());
    ASSERT_TRUE(pipeline.push(PreparedPayload{3, {4}}).isOk());
    pipeline.close();
    auto first = pipeline.pop();
    auto second = pipeline.pop();
    ASSERT_TRUE(first.isOk());
    ASSERT_TRUE(second.isOk());
    ASSERT_TRUE(first.value());
    ASSERT_TRUE(second.value());
    auto end = pipeline.pop();
    ASSERT_TRUE(end.isOk());
    EXPECT_FALSE(end.value());
}
TEST(PersistentTreeResourceTest, ReleasesCommittedFileDescriptorsUnderDefaultLimit) {
    constexpr std::size_t fileCount = 2048;
    constexpr std::size_t fileSize = 64 * 1024;
    TempRoot source;
    TempRoot destination;
    const std::vector<std::uint8_t> contents(fileSize, 0x5a);
    std::vector<PersistentFileIdentity> files;
    files.reserve(fileCount);
    for (std::size_t index = 0; index < fileCount; ++index) {
        const auto relative = "file-" + std::to_string(index) + ".bin";
        const auto path = source.path / relative;
        {
            std::ofstream output(path, std::ios::binary);
            ASSERT_TRUE(output.is_open()) << path;
            output.write(reinterpret_cast<const char*>(contents.data()),
                         static_cast<std::streamsize>(contents.size()));
            ASSERT_TRUE(output.good()) << path;
        }
        struct stat sourceStat {};
        ASSERT_EQ(::stat(path.c_str(), &sourceStat), 0) << path;
        files.emplace_back(index + 1, index + 1, fileSize, relative, "fd-release-batch",
            fileSize, sourceStat.st_mtim.tv_sec, cpnetflux::checksum::ChecksumAlgorithm::Crc32c);
    }

    struct rlimit previousLimit {};
    ASSERT_EQ(::getrlimit(RLIMIT_NOFILE, &previousLimit), 0);
    if (previousLimit.rlim_cur < 1024) GTEST_SKIP() << "soft RLIMIT_NOFILE is below 1024";
    struct rlimit testLimit = previousLimit;
    testLimit.rlim_cur = 1024;
    ASSERT_EQ(::setrlimit(RLIMIT_NOFILE, &testLimit), 0);
    struct RestoreLimit {
        struct rlimit previous;
        ~RestoreLimit() { (void)::setrlimit(RLIMIT_NOFILE, &previous); }
    } restoreLimit{previousLimit};

    auto countDestinationFds = [&](const std::filesystem::path& root) {
        std::size_t count = 0;
        std::error_code error;
        for (std::filesystem::directory_iterator it("/proc/self/fd", error), end;
             !error && it != end; it.increment(error)) {
            std::error_code linkError;
            auto target = std::filesystem::read_symlink(it->path(), linkError);
            if (linkError) continue;
            auto targetText = target.string();
            constexpr char deletedSuffix[] = " (deleted)";
            if (targetText.size() >= sizeof(deletedSuffix) - 1 &&
                targetText.compare(targetText.size() - (sizeof(deletedSuffix) - 1),
                    sizeof(deletedSuffix) - 1, deletedSuffix) == 0) {
                targetText.resize(targetText.size() - (sizeof(deletedSuffix) - 1));
            }
            const auto normalized = std::filesystem::path(targetText).lexically_normal();
            const auto prefix = root.lexically_normal().string() + "/";
            if (normalized.string().compare(0, prefix.size(), prefix) == 0) ++count;
        }
        return count;
    };

    SocketPair pair;
    auto context = makePersistentTransferContext();
    PersistentTreeStats receivedStats;
    cpnetflux::common::Status receiveStatus;
    std::jthread receiver([&] {
        receiveStatus = receivePersistentTree(&pair.receiver, destination.path.string(), false, "",
            &receivedStats, {}, 1, 0, 1, static_cast<std::uint32_t>(fileCount), false, false,
            context);
        if (!receiveStatus.isOk()) ::shutdown(pair.receiver.fd(), SHUT_RDWR);
    });
    PersistentTreeStats sentStats;
    const auto sendStatus = sendPersistentTree(&pair.sender, source.path.string(), files, &sentStats);
    receiver.join();

    ASSERT_TRUE(sendStatus.isOk()) << "send: " << sendStatus.message()
                                   << "; receive: " << receiveStatus.message();
    ASSERT_TRUE(receiveStatus.isOk()) << receiveStatus.message();
    EXPECT_EQ(receivedStats.files, fileCount);
    EXPECT_EQ(countDestinationFds(destination.path), 0U)
        << "completed transactions must not retain destination file descriptors";

    const auto auditStatus = verifyCommittedPersistentOutputs(context);
    ASSERT_TRUE(auditStatus.isOk()) << auditStatus.message();
    EXPECT_EQ(countDestinationFds(destination.path), 0U)
        << "batch audit must close each output after checking it";

    for (const auto index : {std::size_t{0}, fileCount - 1}) {
        const auto output = destination.path / ("file-" + std::to_string(index) + ".bin");
        EXPECT_EQ(std::filesystem::file_size(output), fileSize);
    }
}

TEST(BoundedPayloadPipelineTest, MeasuresRealProducerAndConsumerWaitIncludingCancellation) {
    BoundedPayloadPipeline full(4, 1, true);
    ASSERT_TRUE(full.push(PreparedPayload{0,{1,2,3,4}}).isOk());
    auto producer = std::async(std::launch::async,[&] { return full.push(PreparedPayload{4,{5}}); });
    EXPECT_EQ(producer.wait_for(std::chrono::milliseconds(50)),std::future_status::timeout);
    ASSERT_TRUE(full.pop().isOk());
    ASSERT_TRUE(producer.get().isOk());
    EXPECT_GE(full.producerWaitSeconds(),0.03);
    EXPECT_EQ(full.consumerWaitSeconds(),0);
    BoundedPayloadPipeline empty(4, 1, true);
    auto consumer = std::async(std::launch::async,[&] { return empty.pop(); });
    EXPECT_EQ(consumer.wait_for(std::chrono::milliseconds(50)),std::future_status::timeout);
    empty.cancel(cpnetflux::common::Status::runtimeError("observed cancellation"));
    EXPECT_FALSE(consumer.get().isOk());
    EXPECT_GE(empty.consumerWaitSeconds(),0.03);
    EXPECT_EQ(empty.producerWaitSeconds(),0);
}

TEST(BoundedPayloadPipelineTest, DisabledObservationKeepsWaitCountersWithoutTiming) {
    BoundedPayloadPipeline empty(4,1);
    auto consumer = std::async(std::launch::async,[&] { return empty.pop(); });
    EXPECT_EQ(consumer.wait_for(std::chrono::milliseconds(30)),std::future_status::timeout);
    empty.close();
    ASSERT_TRUE(consumer.get().isOk());
    EXPECT_EQ(empty.consumerWaitCount(),1U);
    EXPECT_EQ(empty.consumerWaitSeconds(),0);
}

TEST(PersistentTreeRangeTest, RejectsUnboundedDiagnosticDepthBeforeWritingAnyFrame) {
    SocketPair pair;
    PersistentTreeStats stats;
    const auto* previous = std::getenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH");
    const std::optional<std::string> saved = previous ? std::optional<std::string>(previous) : std::nullopt;
    ::setenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH","999999999",1);
    const auto status=sendPersistentTree(&pair.sender,"",{},&stats);
    if (saved) ::setenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH",saved->c_str(),1);
    else ::unsetenv("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH");
    EXPECT_FALSE(status.isOk());
    EXPECT_NE(status.message().find("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH"),std::string::npos);
    EXPECT_EQ(stats.payloadBytes,0U);
}
