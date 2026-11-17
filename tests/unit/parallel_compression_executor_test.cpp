#include "cpnetflux/core/io/parallel_compression_executor.h"
#include "cpnetflux/core/protocol/compressed_data.h"

#include <gtest/gtest.h>

#include <barrier>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

using cpnetflux::core::io::ParallelCompressionExecutor;
using cpnetflux::core::io::ParallelCompressionLimits;

TEST(ParallelCompressionExecutorTest, RunsJobsInParallelAndReturnsIndependentResults) {
    std::barrier gate(3);
    auto encoder = [&gate](const std::vector<std::uint8_t>&, std::size_t) {
        gate.arrive_and_wait();
        return cpnetflux::common::Result<std::vector<std::uint8_t>>(
            std::vector<std::uint8_t>{0x5a});
    };
    ParallelCompressionExecutor executor({3, 8, 1024 * 1024}, encoder);
    std::vector<ParallelCompressionExecutor::Task> tasks;
    for (int index = 0; index < 3; ++index) {
        std::vector<std::uint8_t> raw(4096, static_cast<std::uint8_t>(index));
        auto submitted = executor.trySubmit(&raw, 16384);
        ASSERT_TRUE(submitted.isOk()) << submitted.status().message();
        ASSERT_TRUE(submitted.value().has_value());
        EXPECT_TRUE(raw.empty());
        tasks.push_back(std::move(*submitted.value()));
    }
    for (auto& task : tasks) {
        auto encoded = task.take();
        ASSERT_TRUE(encoded.isOk()) << encoded.status().message();
        ASSERT_TRUE(encoded.value().compressionStatus().isOk());
        ASSERT_EQ(encoded.value().compressedBytes().size(), 1U);
        EXPECT_EQ(encoded.value().compressedBytes()[0], 0x5a);
        ASSERT_EQ(encoded.value().rawBytes().size(), 4096U);
    }
    EXPECT_EQ(executor.snapshot().maxActiveWorkers, 3U);
}

TEST(ParallelCompressionExecutorTest, AppliesGlobalJobAndByteBoundsUntilResultsAreTaken) {
    ParallelCompressionExecutor executor({1, 1, 1024 * 1024});
    std::vector<std::uint8_t> firstRaw(16 * 1024, 'A');
    auto first = executor.trySubmit(&firstRaw, 32 * 1024);
    ASSERT_TRUE(first.isOk()) << first.status().message();
    ASSERT_TRUE(first.value().has_value());
    auto firstTask = std::move(*first.value());

    std::vector<std::uint8_t> secondRaw(16 * 1024, 'B');
    auto blocked = executor.trySubmit(&secondRaw, 32 * 1024);
    ASSERT_TRUE(blocked.isOk()) << blocked.status().message();
    EXPECT_FALSE(blocked.value().has_value());
    EXPECT_EQ(secondRaw.size(), 16 * 1024);
    EXPECT_EQ(executor.snapshot().inFlightJobs, 1U);

    {
        auto firstEncoded = firstTask.take();
        ASSERT_TRUE(firstEncoded.isOk()) << firstEncoded.status().message();
        EXPECT_TRUE(firstEncoded.value().compressionStatus().isOk());
    }
    auto second = executor.trySubmit(&secondRaw, 32 * 1024);
    ASSERT_TRUE(second.isOk()) << second.status().message();
    ASSERT_TRUE(second.value().has_value());
    auto secondTask = std::move(*second.value());
    {
        auto secondEncoded = secondTask.take();
        ASSERT_TRUE(secondEncoded.isOk());
        EXPECT_TRUE(secondEncoded.value().compressionStatus().isOk());
        EXPECT_EQ(executor.snapshot().inFlightJobs, 1U);
        EXPECT_LE(executor.snapshot().peakInFlightBytes, executor.snapshot().byteLimit);
    }
    EXPECT_EQ(executor.snapshot().inFlightJobs, 0U);
    EXPECT_EQ(executor.snapshot().inFlightBytes, 0U);
}

TEST(ParallelCompressionExecutorTest, RejectsPayloadWhoseWorstCaseMemoryExceedsGlobalLimit) {
    ParallelCompressionExecutor executor({1, 2, 4096});
    std::vector<std::uint8_t> raw(8192, 'A');
    auto result = executor.trySubmit(&raw, 16384);
    EXPECT_FALSE(result.isOk());
    EXPECT_EQ(result.status().code(), cpnetflux::common::StatusCode::InvalidArgument);
    EXPECT_EQ(raw.size(), 8192U);
}

TEST(ParallelCompressionExecutorTest, DefaultEncoderProducesValidZlibPayload) {
    ParallelCompressionExecutor executor({2, 4, 1024 * 1024});
    std::vector<std::uint8_t> raw(64 * 1024, 'C');
    const auto expected = raw;
    auto result = executor.trySubmit(&raw, 128 * 1024);
    ASSERT_TRUE(result.isOk()) << result.status().message();
    ASSERT_TRUE(result.value().has_value());
    auto encoded = result.value()->take();
    ASSERT_TRUE(encoded.isOk()) << encoded.status().message();
    ASSERT_TRUE(encoded.value().compressionStatus().isOk());
    const auto& compressed = encoded.value().compressedBytes();
    auto decoded = cpnetflux::core::protocol::decodeCompressedDataPayload(
        compressed.data(), compressed.size(), expected.size(), expected.size());
    ASSERT_TRUE(decoded.isOk()) << decoded.status().message();
    EXPECT_EQ(decoded.value(), expected);
}

TEST(ParallelCompressionExecutorTest, ByteBudgetBindsEvenWhenJobSlotsAreAvailable) {
    ParallelCompressionExecutor executor({2, 8, 20000});
    std::vector<std::uint8_t> raw(8192, 'A');
    auto first = executor.trySubmit(&raw, 16384);
    ASSERT_TRUE(first.isOk());
    ASSERT_TRUE(first.value());
    std::vector<std::uint8_t> second(8192, 'B');
    auto blocked = executor.trySubmit(&second, 16384);
    ASSERT_TRUE(blocked.isOk());
    EXPECT_FALSE(blocked.value());
    EXPECT_EQ(second.size(), 8192U);
    auto result = first.value()->take();
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(executor.snapshot().inFlightJobs, 1U);
    EXPECT_LE(executor.snapshot().inFlightBytes, 20000U);
}

TEST(ParallelCompressionExecutorTest, CancellationInterruptsAdmissionWithoutReleasingOtherJobs) {
    ParallelCompressionExecutor executor({1, 1, 1024 * 1024});
    std::vector<std::uint8_t> firstRaw(4096, 'A');
    auto first = executor.submit(&firstRaw, 8192);
    ASSERT_TRUE(first.isOk());
    std::atomic<bool> cancelled{false};
    auto waiting = std::async(std::launch::async, [&] {
        std::vector<std::uint8_t> raw(4096, 'B');
        return executor.submit(&raw, 8192, [&] { return cancelled.load(); });
    });
    cancelled.store(true);
    ASSERT_EQ(waiting.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto rejected = waiting.get();
    EXPECT_FALSE(rejected.isOk());
    EXPECT_EQ(executor.snapshot().inFlightJobs, 1U);
}

TEST(ParallelCompressionExecutorTest, EncoderExceptionRetainsRawDataAndDoesNotKillWorker) {
    ParallelCompressionExecutor executor({1, 2, 1024 * 1024},
        [](const std::vector<std::uint8_t>& raw, std::size_t limit)
            -> cpnetflux::common::Result<std::vector<std::uint8_t>> {
            if (raw.front() == 'X') throw std::runtime_error("injected encoder failure");
            return cpnetflux::core::protocol::encodeCompressedDataPayload(raw.data(), raw.size(), limit);
        });
    std::vector<std::uint8_t> raw(4096, 'X');
    auto failed = executor.submit(&raw, 8192);
    ASSERT_TRUE(failed.isOk());
    {
        auto result = failed.value().take();
        ASSERT_TRUE(result.isOk());
        EXPECT_FALSE(result.value().compressionStatus().isOk());
        EXPECT_EQ(result.value().rawBytes(), std::vector<std::uint8_t>(4096, 'X'));
        EXPECT_TRUE(result.value().compressedBytes().empty());
    }
    std::vector<std::uint8_t> good(4096, 'Y');
    auto task = executor.submit(&good, 8192);
    ASSERT_TRUE(task.isOk());
    auto result = task.value().take();
    ASSERT_TRUE(result.isOk());
    EXPECT_TRUE(result.value().compressionStatus().isOk());
}

TEST(ParallelCompressionExecutorTest, SimultaneousTransfersShareTheSameJobAndByteLimits) {
    ParallelCompressionExecutor executor({4, 6, 70000});
    std::vector<std::future<bool>> senders;
    for (int index = 0; index < 12; ++index) {
        senders.push_back(std::async(std::launch::async, [&, index] {
            for (int chunk = 0; chunk < 16; ++chunk) {
                const auto value = static_cast<std::uint8_t>(index + chunk);
                std::vector<std::uint8_t> raw(8192, value);
                auto task = executor.submit(&raw, 16384);
                if (!task.isOk()) return false;
                auto result = task.value().take();
                if (!result.isOk() || !result.value().compressionStatus().isOk()) return false;
                const auto& encoded = result.value().compressedBytes();
                auto decoded = cpnetflux::core::protocol::decodeCompressedDataPayload(
                    encoded.data(), encoded.size(), 8192, 8192);
                if (!decoded.isOk() || decoded.value() != std::vector<std::uint8_t>(8192, value))
                    return false;
            }
            return true;
        }));
    }
    for (auto& sender : senders) ASSERT_TRUE(sender.get());
    const auto observed = executor.snapshot();
    EXPECT_EQ(observed.inFlightJobs, 0U);
    EXPECT_EQ(observed.inFlightBytes, 0U);
    EXPECT_LE(observed.peakInFlightJobs, 6U);
    EXPECT_LE(observed.peakInFlightBytes, 70000U);
    EXPECT_LE(observed.maxActiveWorkers, 4U);
    EXPECT_EQ(observed.totalCompleted, 12U * 16U);
}

TEST(ParallelCompressionExecutorTest, TaskCanOutliveItsExecutorWithoutDanglingWorkers) {
    ParallelCompressionExecutor::Task retained;
    {
        ParallelCompressionExecutor executor({1, 2, 1024 * 1024});
        std::vector<std::uint8_t> raw(4096, 'Z');
        auto submitted = executor.submit(&raw, 8192);
        ASSERT_TRUE(submitted.isOk());
        retained = std::move(submitted.value());
    }
    auto result = retained.take();
    ASSERT_TRUE(result.isOk());
    EXPECT_TRUE(result.value().compressionStatus().isOk());
    EXPECT_EQ(result.value().rawBytes(), std::vector<std::uint8_t>(4096, 'Z'));
}
