#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/checkpoint/download_manifest.h"
#include <gtest/gtest.h>
#include <atomic>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <thread>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace cpnetflux::core::io;
using namespace cpnetflux::core::protocol;
using namespace std::chrono_literals;
namespace {
// Only this test binary wraps pwrite. Other tests and all other fds call libc.
struct WriteGate {
    std::string directory;
    std::mutex mutex;
    std::condition_variable changed;
    bool first = false, second = false, released = false;
    bool partialFailure = false, shortWritten = false, errorReturned = false;
    bool holdSecond = false, third = false;
    std::thread::id firstWriter;
    explicit WriteGate(std::string root) : directory(std::move(root)) {}
    bool wait(bool WriteGate::*member) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 2s, [&] { return this->*member; });
    }
    void release() { std::lock_guard lock(mutex); released = true; changed.notify_all(); }
};
std::atomic<WriteGate*> activeGate{nullptr};
struct InstallGate {
    WriteGate& gate;
    explicit InstallGate(WriteGate& value) : gate(value) { activeGate.store(&gate); }
    ~InstallGate() { gate.release(); activeGate.store(nullptr); }
};
struct TempRoot {
    std::filesystem::path path;
    TempRoot() {
        char name[] = "/tmp/cpnetflux-range-write-XXXXXX";
        const char* root = ::mkdtemp(name);
        if (!root) throw std::runtime_error("mkdtemp");
        path = root;
    }
    ~TempRoot() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
struct Lane {
    FramedDataSocket sender, receiver;
    PersistentTreeStats sent, received;
    cpnetflux::common::Status receiveStatus;
    std::thread::id receiverThread;
    std::jthread rx;
    Lane(const TempRoot& destination, bool download,
         std::shared_ptr<PersistentTransferContext> context, unsigned index, unsigned channels = 2) {
        int fds[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) throw std::runtime_error("socketpair");
        sender = FramedDataSocket(UniqueFd(fds[0]));
        receiver = FramedDataSocket(UniqueFd(fds[1]));
        received.phaseTiming = true;
        rx = std::jthread([&, download, context, index, channels] {
            receiverThread = std::this_thread::get_id();
            receiveStatus = receivePersistentTree(&receiver, destination.path.string(), download,
                "remote/tree", &received, {}, 1, index, channels, kUnknownPersistentFileCount, true, true, context);
            (void)::shutdown(receiver.fd(), SHUT_RDWR);
        });
    }
    auto send(const TempRoot& source, PersistentFileIdentity id, unsigned index) {
        return std::async(std::launch::async, [&, id, index] {
            cpnetflux::config::FileTransferOptions options; options.resume = true;
            auto status = sendPersistentTree(&sender, source.path.string(), {id}, &sent,
                {}, 1, 0, 1, nullptr, &options);
            (void)::shutdown(sender.fd(), SHUT_RDWR);
            return status;
        });
    }
};
PersistentFileIdentity identity(const TempRoot& source) {
    std::ofstream(source.path / "payload.bin", std::ios::binary) << "ABCDEFGH";
    struct stat st{};
    if (::stat((source.path / "payload.bin").c_str(), &st)) throw std::runtime_error("stat");
    return {1, 7, 8, "payload.bin", "range-write-gate", 4, st.st_mtim.tv_sec,
        cpnetflux::checksum::ChecksumAlgorithm::Crc32c, {}, 1, 0, 4, 2, 1};
}
void checkOutput(const TempRoot& destination) {
    std::ifstream input(destination.path / "payload.bin", std::ios::binary);
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(input)), {}), "ABCDEFGH");
}
std::vector<cpnetflux::checkpoint::ChunkChecksumRecord> records(const TempRoot& destination, bool download) {
    const auto output = (destination.path / "payload.bin").string();
    if (download) {
        auto manifest = cpnetflux::checkpoint::loadDownloadManifest(
            cpnetflux::checkpoint::downloadManifestPathForOutput(output));
        EXPECT_TRUE(manifest.isOk()) << manifest.status().message();
        return manifest.isOk() ? manifest.value().verifiedChunks : std::vector<cpnetflux::checkpoint::ChunkChecksumRecord>{};
    }
    auto manifest = cpnetflux::checkpoint::ManifestStore::load(
        cpnetflux::checkpoint::manifestPathForOutput(output));
    EXPECT_TRUE(manifest.isOk()) << manifest.status().message();
    return manifest.isOk() ? manifest.value().verifiedChunks : std::vector<cpnetflux::checkpoint::ChunkChecksumRecord>{};
}
}
extern "C" ssize_t __real_pwrite(int, const void*, size_t, off_t);
namespace { std::atomic<WriteGate*> activeMapGate{nullptr}; }
extern "C" void* __real_mmap(void*, size_t, int, int, int, off_t);
extern "C" void* __wrap_mmap(void* address, size_t length, int protection, int flags, int fd, off_t offset) {
    auto* gate = activeMapGate.load();
    if (gate && fd >= 0 && (protection & PROT_WRITE)) {
        std::array<char, 1024> path{};
        const auto link = "/proc/self/fd/" + std::to_string(fd);
        const auto count = ::readlink(link.c_str(), path.data(), path.size());
        if (count > 0 && std::string_view(path.data(), count).starts_with(gate->directory + "/")) {
            std::unique_lock lock(gate->mutex);
            if (!gate->first) {
                gate->first = true; gate->changed.notify_all();
                gate->changed.wait(lock, [&] { return gate->released; });
                if (gate->partialFailure) { errno = ENOMEM; return MAP_FAILED; }
            } else {
                gate->second = true; gate->changed.notify_all();
            }
        }
    }
    return __real_mmap(address, length, protection, flags, fd, offset);
}
extern "C" ssize_t __wrap_pwrite(int fd, const void* data, size_t length, off_t offset) {
    auto* gate = activeGate.load();
    if (!gate) return __real_pwrite(fd, data, length, offset);
    std::array<char, 1024> path{};
    const auto link = "/proc/self/fd/" + std::to_string(fd);
    const auto count = ::readlink(link.c_str(), path.data(), path.size());
    if (count <= 0 || !std::string_view(path.data(), count).starts_with(gate->directory + "/"))
        return __real_pwrite(fd, data, length, offset);
    std::unique_lock lock(gate->mutex);
    if (offset == 4) {
        gate->second = true; gate->changed.notify_all();
        if (gate->holdSecond) gate->changed.wait(lock, [&] { return gate->released; });
    }
    if (offset == 8) { gate->third = true; gate->changed.notify_all(); }
    if (offset == 0 && !gate->first) {
        gate->firstWriter = std::this_thread::get_id();
        gate->first = true; gate->changed.notify_all();
        gate->changed.wait(lock, [&] { return gate->released; });
        if (gate->partialFailure) {
            const auto result = __real_pwrite(fd, data, 1, offset);
            gate->shortWritten = result == 1;
            return result;
        }
    } else if (offset == 1 && gate->partialFailure && gate->shortWritten && !gate->errorReturned) {
        gate->errorReturned = true; errno = EIO; return -1;
    }
    lock.unlock();
    return __real_pwrite(fd, data, length, offset);
}

class PersistentRangeWriteTest : public ::testing::TestWithParam<bool> {
    std::string previous_;
    bool hadPrevious_ = false;
    std::string previousStageWrite_; bool hadPreviousStageWrite_ = false;
 protected:
    void SetUp() override {
        if (const auto* value = std::getenv("CPNETFLUX_RANGE_DIRECT_WRITE")) { previous_ = value; hadPrevious_ = true; }
        ASSERT_EQ(::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", "parallel", 1), 0);
        if (const auto* value=std::getenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY")) {previousStageWrite_=value;hadPreviousStageWrite_=true;}
        ASSERT_EQ(::setenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY", "0", 1), 0);
    }
    void TearDown() override {
        if (hadPreviousStageWrite_) (void)::setenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY",previousStageWrite_.c_str(),1);
        else (void)::unsetenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY");
        if (hadPrevious_) (void)::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", previous_.c_str(), 1);
        else (void)::unsetenv("CPNETFLUX_RANGE_DIRECT_WRITE");
    }
};
namespace {
struct MappedSettings {
    std::vector<std::pair<std::string, std::optional<std::string>>> previous;
    MappedSettings() {
        for (const auto& [key, value] : std::vector<std::pair<const char*, const char*>>{
                 {"CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION", "inline"},
                 {"CPNETFLUX_STAGE_READ_CONCURRENCY", "0"},
                 {"CPNETFLUX_STAGE_WRITE_CONCURRENCY", "0"},
                 {"CPNETFLUX_RANGE_DIRECT_WRITE", "parallel"},
                 {"CPNETFLUX_RECEIVER_WRITE_BATCH", "1"}}) {
            const auto* old = std::getenv(key);
            previous.emplace_back(key, old ? std::optional<std::string>(old) : std::nullopt);
            (void)::setenv(key, value, 1);
        }
    }
    ~MappedSettings() {
        for (const auto& [key, value] : previous) {
            if (value) (void)::setenv(key.c_str(), value->c_str(), 1);
            else (void)::unsetenv(key.c_str());
        }
    }
};
}
TEST(PersistentMappedRangeTest, FourRangesRetireBeforePublication) {
    MappedSettings settings;
    TempRoot source, destination;
    auto id = identity(source); id.chunkSize = 2; id.rangeLength = 2; id.rangeCount = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.experimentalMappedDownload = true;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    std::vector<std::unique_ptr<Lane>> lanes;
    std::vector<std::future<cpnetflux::common::Status>> sends;
    for (unsigned i = 0; i < 4; ++i) {
        lanes.push_back(std::make_unique<Lane>(destination, true, context, i, 4));
        id.rangeId = i + 1; id.rangeOffset = i * 2;
        sends.push_back(lanes.back()->send(source, id, i));
    }
    for (unsigned i = 0; i < 4; ++i) {
        const auto status = sends[i].get(); lanes[i]->rx.join();
        EXPECT_TRUE(status.isOk()) << status.message();
        EXPECT_TRUE(lanes[i]->receiveStatus.isOk()) << lanes[i]->receiveStatus.message();
        ASSERT_EQ(lanes[i]->received.fileTimings.size(), 1U);
        const auto& timing = lanes[i]->received.fileTimings.front();
        EXPECT_TRUE(timing.receiverMappedPayload);
        EXPECT_EQ(timing.mappedWrite.mapCalls, 1U);
        EXPECT_EQ(timing.mappedWrite.unmapCalls, 1U);
        EXPECT_EQ(timing.mappedWrite.writtenBytes, 2U);
        EXPECT_EQ(timing.mappedWrite.activeMappedBytes, 0U);
        EXPECT_EQ(timing.rangeObservation.fileWrite.writeAtAllCalls, 0U);
    }
    checkOutput(destination);
    EXPECT_TRUE(verifyCommittedPersistentOutputs(context).isOk());
}
TEST(PersistentMappedRangeTest, ReclaimWaitsForFailedMapWhileOtherRangesProgress) {
    MappedSettings settings;
    TempRoot source, destination;
    auto id = identity(source); id.chunkSize = 2; id.rangeLength = 2; id.rangeCount = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.experimentalMappedDownload = true;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); gate.partialFailure = true;
    struct MapInstallation {
        WriteGate& gate;
        explicit MapInstallation(WriteGate& g) : gate(g) { activeMapGate.store(&g); }
        ~MapInstallation() { gate.release(); activeMapGate.store(nullptr); }
    } installed(gate);
    Lane first(destination, true, context, 0, 4), retry(destination, true, context, 0, 4);
    auto old = first.send(source, id, 0);
    EXPECT_TRUE(gate.wait(&WriteGate::first));
    auto replacement = id; ++replacement.attempt;
    auto recovered = retry.send(source, replacement, 0);
    pollfd descriptor{retry.sender.fd(), POLLIN, 0};
    EXPECT_EQ(::poll(&descriptor, 1, 100), 0);
    std::vector<std::unique_ptr<Lane>> others;
    std::vector<std::future<cpnetflux::common::Status>> sends;
    for (unsigned i = 1; i < 4; ++i) {
        others.push_back(std::make_unique<Lane>(destination, true, context, i, 4));
        id.rangeId = i + 1; id.rangeOffset = i * 2;
        sends.push_back(others.back()->send(source, id, i));
    }
    for (unsigned i = 0; i < 3; ++i) {
        EXPECT_EQ(sends[i].wait_for(2s), std::future_status::ready);
        EXPECT_TRUE(sends[i].get().isOk()); others[i]->rx.join();
        EXPECT_TRUE(others[i]->receiveStatus.isOk());
    }
    EXPECT_EQ(records(destination, true).size(), 3U);
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    gate.release();
    EXPECT_FALSE(old.get().isOk());
    const auto fixed = recovered.get(); first.rx.join(); retry.rx.join();
    EXPECT_FALSE(first.receiveStatus.isOk());
    EXPECT_TRUE(fixed.isOk()) << fixed.message();
    EXPECT_TRUE(retry.receiveStatus.isOk()) << retry.receiveStatus.message();
    ASSERT_EQ(retry.received.fileTimings.size(), 1U);
    const auto& timing = retry.received.fileTimings.front();
    EXPECT_EQ(timing.rangeObservation.reclaimWaitCalls, 1U);
    EXPECT_GT(timing.rangeObservation.reclaimWaitSeconds, 0.05);
    EXPECT_EQ(timing.mappedWrite.mapCalls, 1U);
    EXPECT_EQ(timing.mappedWrite.unmapCalls, 1U);
    EXPECT_EQ(timing.mappedWrite.writtenBytes, 2U);
    EXPECT_EQ(timing.mappedWrite.activeMappedBytes, 0U);
    checkOutput(destination);
}
TEST(PersistentMappedRangeTest, CorruptFrameCannotWriteOrCertifyOrPublish) {
    MappedSettings settings;
    TempRoot source, destination;
    auto id = identity(source); id.chunkSize = 2; id.rangeLength = 2; id.rangeCount = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.experimentalMappedDownload = true;
    auto context = makePersistentTransferContext(policy);
    Lane lane(destination, true, context, 0, 4);
    ASSERT_TRUE(PersistentDataSession::writeBegin(&lane.sender, id, true).isOk());
    ASSERT_TRUE(PersistentDataSession::readResumeResponse(&lane.sender, id).isOk());
    FrameHeader header;
    header.type = FrameType::Data; header.streamId = id.fileId; header.chunkId = id.generation;
    header.rangeId = id.rangeId; header.attempt = id.attempt; header.totalSize = id.totalSize;
    header.flags = kDataChecksum; header.payloadSize = 6;
    const std::array<std::uint8_t, 6> corrupt{0, 0, 0, 0, 'A', 'B'};
    const auto encoded = encodeFrameHeader(header);
    ASSERT_TRUE(lane.sender.writeFrame(encoded.data(), encoded.size(), corrupt.data(), corrupt.size()).isOk());
    (void)::shutdown(lane.sender.fd(), SHUT_WR); lane.rx.join();
    EXPECT_FALSE(lane.receiveStatus.isOk());
    EXPECT_NE(lane.receiveStatus.message().find("checksum mismatch before write"), std::string::npos);
    EXPECT_TRUE(records(destination, true).empty());
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
}
TEST(PersistentMappedRangeTest, RejectsUploadAndWrongLaneCountBeforeInput) {
    MappedSettings settings;
    for (bool download : {false, true}) {
        TempRoot destination;
        PersistentTransferPolicy policy;
        policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
        policy.experimentalMappedDownload = true;
        Lane lane(destination, download, makePersistentTransferContext(policy), 0, download ? 2 : 4);
        lane.rx.join();
        EXPECT_EQ(lane.receiveStatus.code(), cpnetflux::common::StatusCode::InvalidArgument);
        EXPECT_NE(lane.receiveStatus.message().find("experimental download write"), std::string::npos);
        EXPECT_TRUE(std::filesystem::is_empty(destination.path));
    }
}
TEST_P(PersistentRangeWriteTest, IndependentDirectRangeProgressesWhileOtherWriteIsBlocked) {
    const bool download = GetParam();
    TempRoot source, destination;
    auto first = identity(source); auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); InstallGate installed(gate);
    Lane a(destination, download, context, 0), b(destination, download, context, 1);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto right = b.send(source, second, 1);
    const bool independent = gate.wait(&WriteGate::second);
    const bool secondFinished = right.wait_for(2s) == std::future_status::ready;
    EXPECT_TRUE(blocked);
    EXPECT_TRUE(independent) << "second disjoint range must enter pwrite before first is released";
    EXPECT_TRUE(secondFinished);
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    if (secondFinished) {
        const auto verified = records(destination, download);
        EXPECT_EQ(verified.size(), 1U);
        if (!verified.empty()) EXPECT_EQ(verified.front().offset, 4U);
    }
    gate.release();
    auto l = left.get(); auto r = right.get(); a.rx.join(); b.rx.join();
    EXPECT_TRUE(l.isOk()) << l.message(); EXPECT_TRUE(r.isOk()) << r.message();
    EXPECT_TRUE(a.receiveStatus.isOk()) << a.receiveStatus.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    checkOutput(destination);
    ASSERT_EQ(a.received.fileTimings.size(), 1U);
    ASSERT_EQ(b.received.fileTimings.size(), 1U);
    EXPECT_EQ(a.received.fileTimings.front().rangeObservation.parallelDirectWriteCalls, 1U);
    EXPECT_EQ(b.received.fileTimings.front().rangeObservation.parallelDirectWriteCalls, 1U);
}
TEST_P(PersistentRangeWriteTest, InlinePayloadWritesOnReceiverThreadAndCertifiesOnlyAfterIo) {
    struct ExecutionSetting {
        std::string previous;
        bool hadPrevious = false;
        ExecutionSetting() {
            if (const auto* value = std::getenv("CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION")) {
                previous = value; hadPrevious = true;
            }
            if (::setenv("CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION", "inline", 1))
                throw std::runtime_error("setenv");
        }
        ~ExecutionSetting() {
            if (hadPrevious) (void)::setenv("CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION", previous.c_str(), 1);
            else (void)::unsetenv("CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION");
        }
    } execution;
    TempRoot source, destination;
    auto id = identity(source); id.rangeLength = 8; id.rangeCount = 1;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); InstallGate installed(gate);
    Lane lane(destination, GetParam(), context, 0);
    auto transfer = lane.send(source, id, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    EXPECT_TRUE(blocked);
    if (blocked) EXPECT_EQ(gate.firstWriter, lane.receiverThread);
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    EXPECT_EQ(transfer.wait_for(30ms), std::future_status::timeout);
    gate.release();
    auto sent = transfer.get(); lane.rx.join();
    ASSERT_TRUE(sent.isOk()) << sent.message();
    ASSERT_TRUE(lane.receiveStatus.isOk()) << lane.receiveStatus.message();
    checkOutput(destination);
    EXPECT_EQ(records(destination, GetParam()).size(), 2U);
    EXPECT_EQ(lane.received.payloadBytes, 8U);
    ASSERT_EQ(lane.received.fileTimings.size(), 1U);
    EXPECT_EQ(lane.received.fileTimings.front().pipelineHighWatermarkBytes, 0U);
    EXPECT_EQ(lane.received.fileTimings.front().receiverWriterThreadCpuSeconds, 0);
}
TEST_P(PersistentRangeWriteTest, BufferedRangesRetainSerialization) {
    TempRoot source, destination;
    auto first = identity(source); auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Coalesced;
    policy.options.fileIo.bufferSize = 16;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); InstallGate installed(gate);
    Lane a(destination, GetParam(), context, 0), b(destination, GetParam(), context, 1);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto right = b.send(source, second, 1);
    const bool independent = gate.wait(&WriteGate::second);
    EXPECT_TRUE(blocked); EXPECT_FALSE(independent);
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    gate.release();
    auto l = left.get(); auto r = right.get(); a.rx.join(); b.rx.join();
    EXPECT_TRUE(l.isOk()) << l.message(); EXPECT_TRUE(r.isOk()) << r.message();
    EXPECT_TRUE(a.receiveStatus.isOk()) << a.receiveStatus.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    checkOutput(destination);
}
TEST_P(PersistentRangeWriteTest, SerialDiagnosticRetainsDirectWriteSerialization) {
    ASSERT_EQ(::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", "serial", 1), 0);
    TempRoot source, destination;
    auto first = identity(source); auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); InstallGate installed(gate);
    Lane a(destination, GetParam(), context, 0), b(destination, GetParam(), context, 1);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto right = b.send(source, second, 1);
    const bool independent = gate.wait(&WriteGate::second);
    EXPECT_TRUE(blocked); EXPECT_FALSE(independent);
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    gate.release();
    auto l = left.get(); auto r = right.get(); a.rx.join(); b.rx.join();
    EXPECT_TRUE(l.isOk()) << l.message(); EXPECT_TRUE(r.isOk()) << r.message();
    EXPECT_TRUE(a.receiveStatus.isOk()) << a.receiveStatus.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    checkOutput(destination);
}
TEST_P(PersistentRangeWriteTest, SharedWriterAdmissionBoundsBothRanges) {
    ASSERT_EQ(::setenv("CPNETFLUX_STAGE_WRITE_CONCURRENCY", "1", 1), 0);
    TempRoot source, destination;
    auto first = identity(source); auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); InstallGate installed(gate);
    Lane a(destination, GetParam(), context, 0), b(destination, GetParam(), context, 1);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto right = b.send(source, second, 1);
    const bool independent = gate.wait(&WriteGate::second);
    EXPECT_TRUE(blocked); EXPECT_FALSE(independent);
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    gate.release();
    auto l = left.get(); auto r = right.get(); a.rx.join(); b.rx.join();
    EXPECT_TRUE(l.isOk()) << l.message(); EXPECT_TRUE(r.isOk()) << r.message();
    EXPECT_TRUE(a.receiveStatus.isOk()) << a.receiveStatus.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    checkOutput(destination);
    ASSERT_EQ(a.received.fileTimings.size(),1U); ASSERT_EQ(b.received.fileTimings.size(),1U);
    EXPECT_EQ(a.received.fileTimings[0].stageWriteConcurrency,1U);
    EXPECT_EQ(b.received.fileTimings[0].stageWriteConcurrency,1U);
    EXPECT_EQ(a.received.fileTimings[0].stageWriteCalls,1U);
    EXPECT_GT(b.received.fileTimings[0].stageWriteWaitSeconds,0.01);
    EXPECT_EQ(b.received.fileTimings[0].rangeObservation.parallelDirectWriteCalls,1U);
}
TEST_P(PersistentRangeWriteTest, HigherAttemptDrainsPartialFailedWriteBeforeReusingSlot) {
    const bool download = GetParam();
    TempRoot source, destination;
    auto first = identity(source); auto retry = first; ++retry.attempt;
    auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); gate.partialFailure = true; InstallGate installed(gate);
    Lane a(destination, download, context, 0), b(destination, download, context, 1), c(destination, download, context, 0);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto recovered = c.send(source, retry, 0);
    // The higher attempt may not receive its resume mask while an old syscall can still write.
    pollfd descriptor{c.sender.fd(), POLLIN, 0};
    EXPECT_EQ(::poll(&descriptor, 1, 100), 0);
    auto right = b.send(source, second, 1);
    const bool independent = gate.wait(&WriteGate::second);
    const bool secondFinished = right.wait_for(2s) == std::future_status::ready;
    EXPECT_TRUE(blocked); EXPECT_TRUE(independent); EXPECT_TRUE(secondFinished);
    if (secondFinished) {
        const auto verified = records(destination, download);
        EXPECT_EQ(verified.size(), 1U);
        if (!verified.empty()) EXPECT_EQ(verified.front().offset, 4U);
    }
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    gate.release();
    auto l = left.get(); auto r = right.get(); auto fixed = recovered.get();
    a.rx.join(); b.rx.join(); c.rx.join();
    EXPECT_FALSE(l.isOk()); EXPECT_FALSE(a.receiveStatus.isOk());
    EXPECT_TRUE(r.isOk()) << r.message(); EXPECT_TRUE(fixed.isOk()) << fixed.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    EXPECT_TRUE(c.receiveStatus.isOk()) << c.receiveStatus.message();
    EXPECT_TRUE(gate.shortWritten); EXPECT_TRUE(gate.errorReturned);
    EXPECT_EQ(c.sent.payloadBytes, 4U); EXPECT_EQ(c.sent.skippedBytes, 0U);
    ASSERT_EQ(c.received.fileTimings.size(), 1U);
    EXPECT_EQ(c.received.fileTimings.front().rangeObservation.reclaimWaitCalls, 1U);
    EXPECT_GT(c.received.fileTimings.front().rangeObservation.reclaimWaitSeconds, 0.05);
    const auto verified = records(destination, download);
    EXPECT_EQ(verified.size(), 2U);
    for (const auto& record : verified) EXPECT_EQ(record.length, 4U);
    checkOutput(destination);
}
TEST_P(PersistentRangeWriteTest, HigherAttemptCannotChangeItsPartition) {
    TempRoot source, destination;
    auto first = identity(source); auto moved = first; ++moved.attempt; moved.rangeOffset = 4;
    auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    {
        Lane original(destination, GetParam(), context, 0);
        auto sent = original.send(source, first, 0); auto status = sent.get(); original.rx.join();
        ASSERT_TRUE(status.isOk()) << status.message();
        ASSERT_TRUE(original.receiveStatus.isOk()) << original.receiveStatus.message();
    }
    {
        Lane retry(destination, GetParam(), context, 0);
        auto sent = retry.send(source, moved, 0); auto status = sent.get(); retry.rx.join();
        EXPECT_FALSE(status.isOk()) << "a retry must preserve the original range partition";
        EXPECT_FALSE(retry.receiveStatus.isOk());
    }
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    const auto before = records(destination, GetParam());
    EXPECT_EQ(before.size(), 1U);
    {
        Lane remaining(destination, GetParam(), context, 1);
        auto sent = remaining.send(source, second, 1); auto status = sent.get(); remaining.rx.join();
        EXPECT_TRUE(status.isOk()) << status.message();
        EXPECT_TRUE(remaining.receiveStatus.isOk()) << remaining.receiveStatus.message();
    }
    checkOutput(destination);
}
TEST_P(PersistentRangeWriteTest, FileSerialQueuesPhysicalWrites) {
    ASSERT_EQ(::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", "file_serial", 1), 0);
    TempRoot source, destination;
    auto first = identity(source); auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); InstallGate installed(gate);
    Lane a(destination, GetParam(), context, 0), b(destination, GetParam(), context, 1);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto right = b.send(source, second, 1);
    const bool independent = gate.wait(&WriteGate::second);
    EXPECT_TRUE(blocked); EXPECT_FALSE(independent);
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    gate.release();
    auto l = left.get(); auto r = right.get(); a.rx.join(); b.rx.join();
    EXPECT_TRUE(l.isOk()) << l.message(); EXPECT_TRUE(r.isOk()) << r.message();
    EXPECT_TRUE(a.receiveStatus.isOk()) << a.receiveStatus.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    checkOutput(destination);
    ASSERT_EQ(a.received.fileTimings.size(), 1U);
    ASSERT_EQ(b.received.fileTimings.size(), 1U);
    EXPECT_EQ(a.received.fileTimings[0].rangeObservation.serializedDirectWriteCalls, 1U);
    EXPECT_EQ(b.received.fileTimings[0].rangeObservation.serializedDirectWriteCalls, 1U);
    EXPECT_GT(b.received.fileTimings[0].rangeObservation.directWriteWaitSeconds, 0.01);
}
TEST_P(PersistentRangeWriteTest, FileSerialAllowsMetadataDuringBlockedIo) {
    ASSERT_EQ(::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", "file_serial", 1), 0);
    TempRoot source, destination;
    auto first = identity(source); auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); InstallGate installed(gate);
    Lane a(destination, GetParam(), context, 0), b(destination, GetParam(), context, 1);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto begin = PersistentDataSession::writeBegin(&b.sender, second, true);
    pollfd descriptor{b.sender.fd(), POLLIN, 0};
    const auto ready = ::poll(&descriptor, 1, 1000);
    EXPECT_TRUE(blocked); EXPECT_TRUE(begin.isOk());
    EXPECT_GT(ready, 0) << "resume metadata must respond while physical IO is blocked";
    gate.release();
    auto resume = PersistentDataSession::readResumeResponse(&b.sender, second);
    EXPECT_TRUE(resume.isOk()) << resume.status().message();
    auto status = left.get(); a.rx.join();
    EXPECT_TRUE(status.isOk()) << status.message();
    (void)::shutdown(b.sender.fd(), SHUT_RDWR); b.rx.join();
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
}
TEST_P(PersistentRangeWriteTest, FileSerialDoesNotBlockAnotherFile) {
    ASSERT_EQ(::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", "file_serial", 1), 0);
    TempRoot source, destination;
    auto first = identity(source); auto other = first;
    std::filesystem::copy_file(source.path / "payload.bin", source.path / "other.bin");
    other.fileId = 2; other.generation = 8; other.relativePath = "other.bin";
    other.rangeCount = 1; other.rangeLength = 8;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); InstallGate installed(gate);
    Lane a(destination, GetParam(), context, 0), b(destination, GetParam(), context, 1);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto right = b.send(source, other, 1);
    const bool independent = right.wait_for(2s) == std::future_status::ready;
    EXPECT_TRUE(blocked); EXPECT_TRUE(independent);
    gate.release();
    auto l = left.get(); auto r = right.get(); a.rx.join(); b.rx.join();
    EXPECT_TRUE(l.isOk()) << l.message(); EXPECT_TRUE(r.isOk()) << r.message();
    EXPECT_TRUE(a.receiveStatus.isOk()) << a.receiveStatus.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    std::ifstream input(destination.path / "other.bin", std::ios::binary);
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(input)), {}), "ABCDEFGH");
}
TEST_P(PersistentRangeWriteTest, FileSerialDrainsFailedWriteBeforeRetry) {
    ASSERT_EQ(::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", "file_serial", 1), 0);
    const bool download = GetParam();
    TempRoot source, destination;
    auto first = identity(source); auto retry = first; ++retry.attempt;
    auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); gate.partialFailure = true; InstallGate installed(gate);
    Lane a(destination, download, context, 0), b(destination, download, context, 1), c(destination, download, context, 0);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto recovered = c.send(source, retry, 0);
    // The higher attempt may not receive its resume mask while an old syscall can still write.
    pollfd descriptor{c.sender.fd(), POLLIN, 0};
    EXPECT_EQ(::poll(&descriptor, 1, 100), 0);
    auto right = b.send(source, second, 1);
    const bool independent = gate.wait(&WriteGate::second);
    const bool secondFinished = right.wait_for(2s) == std::future_status::ready;
    EXPECT_TRUE(blocked); EXPECT_FALSE(independent); EXPECT_FALSE(secondFinished);
    if (secondFinished) {
        const auto verified = records(destination, download);
        EXPECT_EQ(verified.size(), 1U);
        if (!verified.empty()) EXPECT_EQ(verified.front().offset, 4U);
    }
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    gate.release();
    auto l = left.get(); auto r = right.get(); auto fixed = recovered.get();
    a.rx.join(); b.rx.join(); c.rx.join();
    EXPECT_FALSE(l.isOk()); EXPECT_FALSE(a.receiveStatus.isOk());
    EXPECT_TRUE(r.isOk()) << r.message(); EXPECT_TRUE(fixed.isOk()) << fixed.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    EXPECT_TRUE(c.receiveStatus.isOk()) << c.receiveStatus.message();
    EXPECT_TRUE(gate.shortWritten); EXPECT_TRUE(gate.errorReturned);
    EXPECT_EQ(c.sent.payloadBytes, 4U); EXPECT_EQ(c.sent.skippedBytes, 0U);
    ASSERT_EQ(c.received.fileTimings.size(), 1U);
    EXPECT_EQ(c.received.fileTimings.front().rangeObservation.reclaimWaitCalls, 1U);
    EXPECT_GT(c.received.fileTimings.front().rangeObservation.reclaimWaitSeconds, 0.05);
    const auto verified = records(destination, download);
    EXPECT_EQ(verified.size(), 2U);
    for (const auto& record : verified) EXPECT_EQ(record.length, 4U);
    checkOutput(destination);
}
TEST_P(PersistentRangeWriteTest, FileDualLimitsPhysicalWritesPerFileToTwo) {
    ASSERT_EQ(::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", "file_dual", 1), 0);
    TempRoot source, destination, independentDestination;
    auto first = identity(source);
    std::ofstream(source.path / "payload.bin", std::ios::binary | std::ios::app) << "IJKL";
    struct stat st{}; ASSERT_EQ(::stat((source.path / "payload.bin").c_str(), &st), 0);
    first.totalSize = 12; first.rangeCount = 3; first.mtimeUnixSeconds = st.st_mtim.tv_sec;
    auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    auto third = first; third.rangeId = 3; third.rangeOffset = 8;
    auto other = first; other.fileId = 2; other.generation = 8;
    other.rangeCount = 1; other.rangeLength = 12;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); gate.holdSecond = true; InstallGate installed(gate);
    Lane a(destination, GetParam(), context, 0), b(destination, GetParam(), context, 1),
         c(destination, GetParam(), context, 0), d(independentDestination, GetParam(), context, 1);
    auto left = a.send(source, first, 0); const bool firstBlocked = gate.wait(&WriteGate::first);
    auto right = b.send(source, second, 1); const bool secondBlocked = gate.wait(&WriteGate::second);
    auto pending = c.send(source, third, 0); const bool exceeded = gate.wait(&WriteGate::third);
    auto independent = d.send(source, other, 1);
    const bool independentFinished = independent.wait_for(2s) == std::future_status::ready;
    EXPECT_TRUE(firstBlocked); EXPECT_TRUE(secondBlocked); EXPECT_FALSE(exceeded);
    EXPECT_TRUE(independentFinished) << "two occupied permits must not block a different file";
    gate.release();
    auto l=left.get(), r=right.get(), t=pending.get(), o=independent.get();
    a.rx.join(); b.rx.join(); c.rx.join(); d.rx.join();
    EXPECT_TRUE(l.isOk()) << l.message(); EXPECT_TRUE(r.isOk()) << r.message();
    EXPECT_TRUE(t.isOk()) << t.message(); EXPECT_TRUE(o.isOk()) << o.message();
    EXPECT_TRUE(a.receiveStatus.isOk()); EXPECT_TRUE(b.receiveStatus.isOk());
    EXPECT_TRUE(c.receiveStatus.isOk()); EXPECT_TRUE(d.receiveStatus.isOk());
    for (const auto* lane : {&a, &b, &c}) {
        ASSERT_EQ(lane->received.fileTimings.size(), 1U);
        const auto& observed = lane->received.fileTimings[0].rangeObservation;
        EXPECT_EQ(observed.directWriteConcurrency, 2U);
        EXPECT_EQ(observed.boundedDirectWriteCalls, 1U);
    }
    for (const auto* root : {&destination, &independentDestination}) {
        std::ifstream input(root->path / "payload.bin", std::ios::binary);
        EXPECT_EQ(std::string((std::istreambuf_iterator<char>(input)), {}), "ABCDEFGHIJKL");
    }
}
TEST_P(PersistentRangeWriteTest, FileDualDrainsPartialFailedWriteBeforeRetry) {
    ASSERT_EQ(::setenv("CPNETFLUX_RANGE_DIRECT_WRITE", "file_dual", 1), 0);
    const bool download = GetParam();
    TempRoot source, destination;
    auto first = identity(source); auto retry = first; ++retry.attempt;
    auto second = first; second.rangeId = 2; second.rangeOffset = 4;
    PersistentTransferPolicy policy;
    policy.options.fileIo.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    policy.rangeCheckpointBatchChunks = 1;
    auto context = makePersistentTransferContext(policy);
    WriteGate gate(destination.path.string()); gate.partialFailure = true; InstallGate installed(gate);
    Lane a(destination, download, context, 0), b(destination, download, context, 1), c(destination, download, context, 0);
    auto left = a.send(source, first, 0);
    const bool blocked = gate.wait(&WriteGate::first);
    auto recovered = c.send(source, retry, 0);
    // The higher attempt may not receive its resume mask while an old syscall can still write.
    pollfd descriptor{c.sender.fd(), POLLIN, 0};
    EXPECT_EQ(::poll(&descriptor, 1, 100), 0);
    auto right = b.send(source, second, 1);
    const bool independent = gate.wait(&WriteGate::second);
    const bool secondFinished = right.wait_for(2s) == std::future_status::ready;
    EXPECT_TRUE(blocked); EXPECT_TRUE(independent); EXPECT_TRUE(secondFinished);
    if (secondFinished) {
        const auto verified = records(destination, download);
        EXPECT_EQ(verified.size(), 1U);
        if (!verified.empty()) EXPECT_EQ(verified.front().offset, 4U);
    }
    EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    gate.release();
    auto l = left.get(); auto r = right.get(); auto fixed = recovered.get();
    a.rx.join(); b.rx.join(); c.rx.join();
    EXPECT_FALSE(l.isOk()); EXPECT_FALSE(a.receiveStatus.isOk());
    EXPECT_TRUE(r.isOk()) << r.message(); EXPECT_TRUE(fixed.isOk()) << fixed.message();
    EXPECT_TRUE(b.receiveStatus.isOk()) << b.receiveStatus.message();
    EXPECT_TRUE(c.receiveStatus.isOk()) << c.receiveStatus.message();
    EXPECT_TRUE(gate.shortWritten); EXPECT_TRUE(gate.errorReturned);
    EXPECT_EQ(c.sent.payloadBytes, 4U); EXPECT_EQ(c.sent.skippedBytes, 0U);
    ASSERT_EQ(c.received.fileTimings.size(), 1U);
    EXPECT_EQ(c.received.fileTimings.front().rangeObservation.reclaimWaitCalls, 1U);
    EXPECT_GT(c.received.fileTimings.front().rangeObservation.reclaimWaitSeconds, 0.05);
    const auto verified = records(destination, download);
    EXPECT_EQ(verified.size(), 2U);
    for (const auto& record : verified) EXPECT_EQ(record.length, 4U);
    checkOutput(destination);
}
INSTANTIATE_TEST_SUITE_P(UploadDownload, PersistentRangeWriteTest, ::testing::Bool());

TEST(PersistentRangeStageTest, BudgetRejectsBeforeFramesOrOutputCreation) {
 TempRoot destination;int fds[2];ASSERT_EQ(::socketpair(AF_UNIX,SOCK_STREAM,0,fds),0);
 FramedDataSocket first{UniqueFd(fds[0])},second{UniqueFd(fds[1])};PersistentTreeStats sent,received;
 const char* old=std::getenv("CPNETFLUX_STAGE_BUFFER_BYTES");std::string previous=old?old:"";
 setenv("CPNETFLUX_STAGE_BUFFER_BYTES","65536",1);
 auto send=sendPersistentTree(&first,destination.path.string(),{},&sent);
 auto receive=receivePersistentTree(&second,destination.path.string(),true,"remote",&received);
 if(old)setenv("CPNETFLUX_STAGE_BUFFER_BYTES",previous.c_str(),1);else unsetenv("CPNETFLUX_STAGE_BUFFER_BYTES");
 EXPECT_FALSE(send.isOk());EXPECT_FALSE(receive.isOk());
 EXPECT_NE(send.message().find("buffer budget"),std::string::npos);
 EXPECT_NE(receive.message().find("buffer budget"),std::string::npos);
 pollfd a{first.fd(),POLLIN,0},b{second.fd(),POLLIN,0};EXPECT_EQ(::poll(&a,1,0),0);EXPECT_EQ(::poll(&b,1,0),0);
 EXPECT_TRUE(std::filesystem::is_empty(destination.path));
}
