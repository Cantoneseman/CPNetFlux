#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::io {

struct ParallelCompressionLimits {
    std::size_t workers = 4;
    std::size_t maxInFlightJobs = 16;
    std::size_t maxInFlightBytes = 256U * 1024U * 1024U;
};

struct ParallelCompressionSnapshot {
    std::size_t workerCount = 0;
    std::size_t maxInFlightJobs = 0;
    std::size_t inFlightJobs = 0;
    std::size_t inFlightBytes = 0;
    std::size_t peakInFlightBytes = 0;
    std::size_t peakInFlightJobs = 0;
    std::size_t maxActiveWorkers = 0;
    std::size_t byteLimit = 0;
    std::uint64_t totalCompleted = 0;
};

class ParallelCompressionExecutor {
    struct State;
    struct Job;

   public:
    class Task;
    using Encoder = std::function<common::Result<std::vector<std::uint8_t>>(
        const std::vector<std::uint8_t>&, std::size_t)>;

    class CompletedPayload {
       public:
        CompletedPayload() = default;
        CompletedPayload(const CompletedPayload&) = delete;
        CompletedPayload& operator=(const CompletedPayload&) = delete;
        CompletedPayload(CompletedPayload&& other) noexcept;
        CompletedPayload& operator=(CompletedPayload&& other) noexcept;
        ~CompletedPayload();

        [[nodiscard]] const std::vector<std::uint8_t>& rawBytes() const noexcept { return rawBytes_; }
        [[nodiscard]] const std::vector<std::uint8_t>& compressedBytes() const noexcept { return bytes_; }
        [[nodiscard]] std::vector<std::uint8_t>& compressedBytes() noexcept { return bytes_; }
        [[nodiscard]] const common::Status& compressionStatus() const noexcept { return status_; }

       private:
        friend class ParallelCompressionExecutor::Task;
        CompletedPayload(std::shared_ptr<State> state, std::size_t reservedBytes,
                         std::vector<std::uint8_t> rawBytes,
                         std::vector<std::uint8_t> bytes, common::Status status);
        void release() noexcept;

        std::shared_ptr<State> state_;
        std::size_t reservedBytes_ = 0;
        std::vector<std::uint8_t> rawBytes_;
        std::vector<std::uint8_t> bytes_;
        common::Status status_ = common::Status::ok();
    };

    class Task {
       public:
        Task() = default;
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        Task(Task&& other) noexcept;
        Task& operator=(Task&& other) noexcept;
        ~Task();

        [[nodiscard]] common::Result<CompletedPayload> take();

       private:
        friend class ParallelCompressionExecutor;
        Task(std::shared_ptr<State> state, std::shared_ptr<Job> job);
        void discard() noexcept;

        std::shared_ptr<State> state_;
        std::shared_ptr<Job> job_;
    };

    explicit ParallelCompressionExecutor(ParallelCompressionLimits limits = {}, Encoder encoder = {});
    ParallelCompressionExecutor(const ParallelCompressionExecutor&) = delete;
    ParallelCompressionExecutor& operator=(const ParallelCompressionExecutor&) = delete;
    ~ParallelCompressionExecutor();

    // Returns an empty optional when the process-wide budget is full. On successful
    // admission, ownership of *input moves to the executor.
    [[nodiscard]] common::Result<std::optional<Task>> trySubmit(
        std::vector<std::uint8_t>* input, std::size_t maxPayloadBytes);
    [[nodiscard]] common::Result<Task> submit(std::vector<std::uint8_t>* input,
        std::size_t maxPayloadBytes, const std::function<bool()>& shouldCancel = {});
    [[nodiscard]] ParallelCompressionSnapshot snapshot() const;

   private:
    static void releaseBudget(const std::shared_ptr<State>& state, std::size_t reservedBytes) noexcept;
    std::shared_ptr<State> state_;
};

[[nodiscard]] ParallelCompressionExecutor& sharedParallelCompressionExecutor();

}  // namespace cpnetflux::core::io
