#include "cpnetflux/storage/file_io.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

struct FakeCompletionState {
    std::vector<std::size_t> completions;
    cpnetflux::common::Status status = cpnetflux::common::Status::ok();
    std::vector<cpnetflux::common::Status> statuses;
    std::size_t index = 0;
    std::vector<std::uint64_t> offsets;
    std::vector<std::size_t> lengths;
};

cpnetflux::common::Status fakeCompletion(std::uint64_t offset, std::size_t length,
                                         std::size_t* completed, void* userData) {
    auto* state = static_cast<FakeCompletionState*>(userData);
    state->offsets.push_back(offset);
    state->lengths.push_back(length);
    if (state->index < state->statuses.size()) {
        cpnetflux::common::Status status = state->statuses[state->index];
        if (!status.isOk()) {
            ++state->index;
            return status;
        }
    }
    if (!state->status.isOk()) {
        return state->status;
    }
    if (state->index >= state->completions.size()) {
        *completed = length;
        return cpnetflux::common::Status::ok();
    }
    *completed = state->completions[state->index++];
    return cpnetflux::common::Status::ok();
}

}  // namespace

TEST(FileIoTest, ParsesBackendAndAdvice) {
    auto backend = cpnetflux::storage::parseFileIoBackendKind("posix");
    ASSERT_TRUE(backend.isOk()) << backend.status().message();
    EXPECT_EQ(backend.value(), cpnetflux::storage::FileIoBackendKind::Posix);
    auto ioUringBackend = cpnetflux::storage::parseFileIoBackendKind("io_uring");
    ASSERT_TRUE(ioUringBackend.isOk()) << ioUringBackend.status().message();
    EXPECT_EQ(ioUringBackend.value(), cpnetflux::storage::FileIoBackendKind::IoUring);
    EXPECT_FALSE(cpnetflux::storage::parseFileIoBackendKind("uring").isOk());

    auto advice = cpnetflux::storage::parseFileIoAdvice("sequential_dontneed");
    ASSERT_TRUE(advice.isOk()) << advice.status().message();
    EXPECT_EQ(advice.value(), cpnetflux::storage::FileIoAdvice::SequentialDontNeed);
    EXPECT_FALSE(cpnetflux::storage::parseFileIoAdvice("random").isOk());

    auto strategy = cpnetflux::storage::parsePosixWriteStrategy("coalesced");
    ASSERT_TRUE(strategy.isOk()) << strategy.status().message();
    EXPECT_EQ(strategy.value(), cpnetflux::storage::PosixWriteStrategy::Coalesced);
    EXPECT_FALSE(cpnetflux::storage::parsePosixWriteStrategy("buffered").isOk());
}

TEST(FileIoTest, DefaultsQueueDepthAndBatchSizeToOne) {
    cpnetflux::storage::FileIoConfig config;
    EXPECT_EQ(config.queueDepth, 1U);
    EXPECT_EQ(config.batchSize, 1U);
    EXPECT_EQ(config.posixWriteStrategy, cpnetflux::storage::PosixWriteStrategy::Auto);
    EXPECT_EQ(cpnetflux::storage::effectivePosixWriteStrategy(config),
              cpnetflux::storage::PosixWriteStrategy::Direct);
    config.bufferSize = 1024;
    EXPECT_EQ(cpnetflux::storage::effectivePosixWriteStrategy(config),
              cpnetflux::storage::PosixWriteStrategy::Coalesced);
}

TEST(FileIoTest, RejectsCoalescedStrategyWithoutBuffer) {
    cpnetflux::storage::FileIoConfig config;
    config.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Coalesced;
    const auto status = cpnetflux::storage::validateFileIoConfig(config);
    EXPECT_FALSE(status.isOk());
    EXPECT_NE(status.message().find("coalesced"), std::string::npos);
}

TEST(FileIoTest, ContextReportsIoUringAvailability) {
    cpnetflux::storage::FileIoConfig config;
    config.backend = cpnetflux::storage::FileIoBackendKind::IoUring;
    config.queueDepth = 4;
    config.batchSize = 4;
    cpnetflux::storage::FileIoContext context(config);
    const cpnetflux::common::Status status = context.validateAvailable();
#if CPNETFLUX_HAS_IO_URING
    EXPECT_TRUE(status.isOk()) << status.message();
#else
    EXPECT_FALSE(status.isOk());
    EXPECT_NE(status.message().find("io_uring"), std::string::npos);
#endif
}

TEST(FileIoTest, TracksReadAndWriteCalls) {
    const std::filesystem::path path = tempPath("cpnetflux-file-io-stats-test.bin");
    std::filesystem::remove(path);

    auto fileResult = cpnetflux::storage::PosixFile::openWriteTruncate(path.string());
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());

    cpnetflux::storage::FileIoStats stats;
    const std::vector<std::uint8_t> data{'a', 'b', 'c', 'd'};
    ASSERT_TRUE(cpnetflux::storage::writeAtAll(file, 0, data.data(), data.size(), &stats).isOk());
    EXPECT_EQ(stats.writeCalls(), 1U);
    EXPECT_EQ(stats.writeBytes(), data.size());
    EXPECT_EQ(stats.posixWriteSyscallCount(), 1U);
    EXPECT_EQ(stats.posixWriteSyscallBytes(), data.size());
    EXPECT_GT(stats.averageWriteBytesPerCall(), 0.0);
    EXPECT_GT(stats.posixAverageBytesPerWriteSyscall(), 0.0);

    file = cpnetflux::storage::PosixFile();
    auto inputResult = cpnetflux::storage::PosixFile::openReadOnly(path.string());
    ASSERT_TRUE(inputResult.isOk()) << inputResult.status().message();
    std::vector<std::uint8_t> output(data.size());
    ASSERT_TRUE(
        cpnetflux::storage::readAtAll(inputResult.value(), 0, output.data(), output.size(), &stats)
            .isOk());
    EXPECT_EQ(stats.readCalls(), 1U);
    EXPECT_EQ(stats.readBytes(), data.size());
    EXPECT_EQ(output, data);

    std::filesystem::remove(path);
}

TEST(FileIoTest, PosixContextReadsAndWrites) {
    const std::filesystem::path path = tempPath("cpnetflux-file-io-context-posix-test.bin");
    std::filesystem::remove(path);

    auto fileResult = cpnetflux::storage::PosixFile::openWriteTruncate(path.string());
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());

    cpnetflux::storage::FileIoConfig config;
    config.backend = cpnetflux::storage::FileIoBackendKind::Posix;
    cpnetflux::storage::FileIoContext context(config);
    cpnetflux::storage::FileIoStats stats;
    const std::vector<std::uint8_t> data{'x', 'y', 'z'};
    ASSERT_TRUE(
        cpnetflux::storage::writeAtAll(file, 0, data.data(), data.size(), context, &stats).isOk());
    EXPECT_EQ(stats.writeCalls(), 1U);

    file = cpnetflux::storage::PosixFile();
    auto inputResult = cpnetflux::storage::PosixFile::openReadOnly(path.string());
    ASSERT_TRUE(inputResult.isOk()) << inputResult.status().message();
    std::vector<std::uint8_t> output(data.size());
    ASSERT_TRUE(cpnetflux::storage::readAtAll(inputResult.value(), 0, output.data(),
                                             output.size(), context, &stats)
                    .isOk());
    EXPECT_EQ(output, data);

    std::filesystem::remove(path);
}

TEST(FileIoTest, IoUringContextReadWriteSmokeWhenAvailable) {
    const std::filesystem::path path = tempPath("cpnetflux-file-io-context-iouring-test.bin");
    std::filesystem::remove(path);

    cpnetflux::storage::FileIoConfig config;
    config.backend = cpnetflux::storage::FileIoBackendKind::IoUring;
    cpnetflux::storage::FileIoContext context(config);
    if (!context.validateAvailable().isOk()) {
        GTEST_SKIP() << "io_uring backend unavailable";
    }

    auto fileResult = cpnetflux::storage::PosixFile::openWriteTruncate(path.string());
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());

    cpnetflux::storage::FileIoStats stats;
    const std::vector<std::uint8_t> data(4096, 42);
    ASSERT_TRUE(
        cpnetflux::storage::writeAtAll(file, 0, data.data(), data.size(), context, &stats).isOk());

    file = cpnetflux::storage::PosixFile();
    auto inputResult = cpnetflux::storage::PosixFile::openReadOnly(path.string());
    ASSERT_TRUE(inputResult.isOk()) << inputResult.status().message();
    std::vector<std::uint8_t> output(data.size());
    ASSERT_TRUE(cpnetflux::storage::readAtAll(inputResult.value(), 0, output.data(),
                                             output.size(), context, &stats)
                    .isOk());
    EXPECT_EQ(output, data);

    std::filesystem::remove(path);
}

TEST(FileIoTest, IoUringCompletionLoopHandlesPartialCompletions) {
    FakeCompletionState state;
    state.completions = {2, 3, 5};
    const auto status = cpnetflux::storage::ioUringRunCompletionLoopForTest(
        cpnetflux::storage::IoUringOperation::Read, 100, 10, fakeCompletion, &state);
    EXPECT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(state.offsets, (std::vector<std::uint64_t>{100, 102, 105}));
    EXPECT_EQ(state.lengths, (std::vector<std::size_t>{10, 8, 5}));
}

TEST(FileIoTest, IoUringBatchedCompletionLoopTracksStatsAndOutOfOrderSqes) {
    FakeCompletionState state;
    state.completions = {4, 4, 4, 4};
    cpnetflux::storage::FileIoStats stats;
    const auto status = cpnetflux::storage::ioUringRunBatchedCompletionLoopForTest(
        cpnetflux::storage::IoUringOperation::Read, 64, 16, 4, 4, 4, fakeCompletion, &state,
        &stats);
    EXPECT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(state.offsets, (std::vector<std::uint64_t>{76, 72, 68, 64}));
    EXPECT_EQ(stats.ioUringSubmitCount(), 1U);
    EXPECT_EQ(stats.ioUringSqeCount(), 4U);
    EXPECT_EQ(stats.ioUringWaitCount(), 4U);
    EXPECT_EQ(stats.ioUringCompletionCount(), 4U);
    EXPECT_EQ(stats.ioUringAverageBytesPerSqe(), 4.0);
}

TEST(FileIoTest, IoUringBatchedCompletionLoopHandlesPartialAndRetry) {
    FakeCompletionState state;
    state.statuses = {cpnetflux::common::Status::systemError("try again", EAGAIN),
                      cpnetflux::common::Status::ok(), cpnetflux::common::Status::ok()};
    state.completions = {3, 5};
    cpnetflux::storage::FileIoStats stats;
    const auto status = cpnetflux::storage::ioUringRunBatchedCompletionLoopForTest(
        cpnetflux::storage::IoUringOperation::Write, 0, 8, 2, 2, 8, fakeCompletion, &state,
        &stats);
    EXPECT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(stats.ioUringRetryCount(), 1U);
    EXPECT_EQ(stats.ioUringPartialCompletionCount(), 1U);
    EXPECT_EQ(stats.ioUringCompletionCount(), 2U);
}

TEST(FileIoTest, IoUringCompletionLoopPropagatesRetryAndSystemErrors) {
    FakeCompletionState retryState;
    retryState.completions = {0, 4};
    const auto eof = cpnetflux::storage::ioUringRunCompletionLoopForTest(
        cpnetflux::storage::IoUringOperation::Read, 0, 4, fakeCompletion, &retryState);
    EXPECT_FALSE(eof.isOk());
    EXPECT_NE(eof.message().find("EOF"), std::string::npos);

    FakeCompletionState errorState;
    errorState.status = cpnetflux::common::Status::systemError("fake error", EIO);
    const auto error = cpnetflux::storage::ioUringRunCompletionLoopForTest(
        cpnetflux::storage::IoUringOperation::Write, 0, 4, fakeCompletion, &errorState);
    EXPECT_FALSE(error.isOk());
    EXPECT_EQ(error.errorNumber(), EIO);
}

TEST(FileIoTest, BufferedWriterCoalescesContiguousWritesAndFlushesGaps) {
    const std::filesystem::path path = tempPath("cpnetflux-file-io-buffered-test.bin");
    std::filesystem::remove(path);

    auto fileResult = cpnetflux::storage::PosixFile::openWriteTruncate(path.string());
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());

    cpnetflux::storage::FileIoConfig config;
    config.bufferSize = 8;
    cpnetflux::storage::FileIoStats stats;
    cpnetflux::storage::BufferedFileWriter writer(file, config, &stats);

    const std::vector<std::uint8_t> first{'a', 'b'};
    const std::vector<std::uint8_t> second{'c', 'd'};
    const std::vector<std::uint8_t> third{'z'};
    ASSERT_TRUE(writer.write(0, first.data(), first.size()).isOk());
    ASSERT_TRUE(writer.write(2, second.data(), second.size()).isOk());
    EXPECT_EQ(stats.writeCalls(), 0U);
    ASSERT_TRUE(writer.write(6, third.data(), third.size()).isOk());
    EXPECT_EQ(stats.writeCalls(), 1U);
    ASSERT_TRUE(writer.flush().isOk());
    EXPECT_EQ(stats.writeCalls(), 2U);

    file = cpnetflux::storage::PosixFile();
    auto inputResult = cpnetflux::storage::PosixFile::openReadOnly(path.string());
    ASSERT_TRUE(inputResult.isOk()) << inputResult.status().message();
    std::vector<std::uint8_t> output(7);
    ASSERT_TRUE(inputResult.value().readAtAll(0, output.data(), output.size()).isOk());
    EXPECT_EQ(output[0], 'a');
    EXPECT_EQ(output[1], 'b');
    EXPECT_EQ(output[2], 'c');
    EXPECT_EQ(output[3], 'd');
    EXPECT_EQ(output[6], 'z');

    std::filesystem::remove(path);
}

TEST(FileIoTest, DirectStrategyBypassesBufferedWriter) {
    const std::filesystem::path path = tempPath("cpnetflux-file-io-direct-strategy-test.bin");
    std::filesystem::remove(path);

    auto fileResult = cpnetflux::storage::PosixFile::openWriteTruncate(path.string());
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    cpnetflux::storage::PosixFile file = std::move(fileResult.value());

    cpnetflux::storage::FileIoConfig config;
    config.bufferSize = 8;
    config.posixWriteStrategy = cpnetflux::storage::PosixWriteStrategy::Direct;
    cpnetflux::storage::FileIoStats stats;
    cpnetflux::storage::BufferedFileWriter writer(file, config, &stats);

    const std::vector<std::uint8_t> first{'a', 'b'};
    const std::vector<std::uint8_t> second{'c', 'd'};
    ASSERT_TRUE(writer.write(0, first.data(), first.size()).isOk());
    ASSERT_TRUE(writer.write(2, second.data(), second.size()).isOk());
    EXPECT_EQ(stats.writeCalls(), 2U);
    EXPECT_EQ(stats.posixWriteSyscallCount(), 2U);
    ASSERT_TRUE(writer.flush().isOk());
    EXPECT_EQ(stats.writeCalls(), 2U);

    std::filesystem::remove(path);
}

TEST(FileIoTest, AppliesOffAndSequentialAdvice) {
    const std::filesystem::path path = tempPath("cpnetflux-file-io-advice-test.bin");
    std::filesystem::remove(path);

    auto fileResult = cpnetflux::storage::PosixFile::openWriteTruncate(path.string());
    ASSERT_TRUE(fileResult.isOk()) << fileResult.status().message();
    const std::vector<std::uint8_t> data(4096, 7);
    ASSERT_TRUE(fileResult.value().writeAtAll(0, data.data(), data.size()).isOk());

    EXPECT_TRUE(cpnetflux::storage::applyFileIoAdvice(
                    fileResult.value(), cpnetflux::storage::FileIoAdvice::Off, 0, data.size())
                    .isOk());
    EXPECT_TRUE(cpnetflux::storage::applyFileIoAdvice(
                    fileResult.value(), cpnetflux::storage::FileIoAdvice::Sequential, 0,
                    data.size())
                    .isOk());

    std::filesystem::remove(path);
}
