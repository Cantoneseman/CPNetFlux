#include "cpnetflux/core/io/parallel_compression_executor.h"

#include <algorithm>
#include <condition_variable>
#include <chrono>
#include <exception>
#include <string>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include <zlib.h>

#include "cpnetflux/core/protocol/compressed_data.h"

namespace cpnetflux::core::io {

struct ParallelCompressionExecutor::Job {
    std::vector<std::uint8_t> raw;
    std::size_t maxPayloadBytes = 0;
    std::size_t reservedBytes = 0;
    std::vector<std::uint8_t> output;
    common::Status status = common::Status::ok();
    bool done = false;
    std::condition_variable completed;
};

struct ParallelCompressionExecutor::State {
    ParallelCompressionLimits limits;
    Encoder encoder;
    mutable std::mutex mutex;
    std::condition_variable workAvailable;
    std::condition_variable capacityAvailable;
    std::deque<std::shared_ptr<Job>> queue;
    std::vector<std::thread> workers;
    bool stopping = false;
    std::size_t inFlightJobs = 0;
    std::size_t inFlightBytes = 0;
    std::size_t peakInFlightBytes = 0;
    std::size_t peakInFlightJobs = 0;
    std::size_t activeWorkers = 0;
    std::size_t maxActiveWorkers = 0;
    std::uint64_t totalCompleted = 0;

    State(ParallelCompressionLimits configured, Encoder configuredEncoder)
        : limits(configured), encoder(std::move(configuredEncoder)) {}
};

namespace {

common::Result<std::size_t> outputAllocationBound(std::size_t rawBytes) {
    // Persistent V3 frames are at most 64 KiB. Reservations cover payload
    // buffers; zlib workspace is separately bounded by the fixed worker count.
    if (rawBytes == 0 || rawBytes > 65536)
        return common::Status::invalidArgument("parallel compression input size is invalid");
    const auto bound = static_cast<std::uint64_t>(compressBound(static_cast<uLong>(rawBytes)));
    constexpr std::uint64_t prefix = core::protocol::kCompressedDataPrefixSize;
    if (bound > std::numeric_limits<std::size_t>::max() - prefix)
        return common::Status::invalidArgument("parallel compression output bound overflows");
    return static_cast<std::size_t>(bound + prefix);
}

}  // namespace

void ParallelCompressionExecutor::releaseBudget(const std::shared_ptr<State>& state,
                                                std::size_t reservedBytes) noexcept {
    {
        std::lock_guard lock(state->mutex);
        --state->inFlightJobs;
        state->inFlightBytes -= reservedBytes;
    }
    state->capacityAvailable.notify_all();
}

ParallelCompressionExecutor::CompletedPayload::CompletedPayload(
    std::shared_ptr<State> state, std::size_t reservedBytes,
    std::vector<std::uint8_t> rawBytes, std::vector<std::uint8_t> bytes,
    common::Status status)
    : state_(std::move(state)), reservedBytes_(reservedBytes),
      rawBytes_(std::move(rawBytes)), bytes_(std::move(bytes)), status_(std::move(status)) {}

ParallelCompressionExecutor::CompletedPayload::CompletedPayload(CompletedPayload&& other) noexcept
    : state_(std::move(other.state_)), reservedBytes_(std::exchange(other.reservedBytes_, 0)),
      rawBytes_(std::move(other.rawBytes_)), bytes_(std::move(other.bytes_)),
      status_(std::move(other.status_)) {}

ParallelCompressionExecutor::CompletedPayload&
ParallelCompressionExecutor::CompletedPayload::operator=(CompletedPayload&& other) noexcept {
    if (this == &other) return *this;
    release();
    state_ = std::move(other.state_);
    reservedBytes_ = std::exchange(other.reservedBytes_, 0);
    rawBytes_ = std::move(other.rawBytes_);
    bytes_ = std::move(other.bytes_);
    status_ = std::move(other.status_);
    return *this;
}

ParallelCompressionExecutor::CompletedPayload::~CompletedPayload() { release(); }

void ParallelCompressionExecutor::CompletedPayload::release() noexcept {
    if (!state_) return;
    rawBytes_.clear();
    std::vector<std::uint8_t>().swap(rawBytes_);
    bytes_.clear();
    std::vector<std::uint8_t>().swap(bytes_);
    ParallelCompressionExecutor::releaseBudget(state_, reservedBytes_);
    state_.reset();
    reservedBytes_ = 0;
}

ParallelCompressionExecutor::Task::Task(std::shared_ptr<State> state, std::shared_ptr<Job> job)
    : state_(std::move(state)), job_(std::move(job)) {}

ParallelCompressionExecutor::Task::Task(Task&& other) noexcept
    : state_(std::move(other.state_)), job_(std::move(other.job_)) {}

ParallelCompressionExecutor::Task& ParallelCompressionExecutor::Task::operator=(Task&& other) noexcept {
    if (this == &other) return *this;
    discard();
    state_ = std::move(other.state_);
    job_ = std::move(other.job_);
    return *this;
}

ParallelCompressionExecutor::Task::~Task() { discard(); }

common::Result<ParallelCompressionExecutor::CompletedPayload>
ParallelCompressionExecutor::Task::take() {
    if (!state_ || !job_)
        return common::Status::invalidArgument("parallel compression task is empty");
    std::unique_lock lock(state_->mutex);
    job_->completed.wait(lock, [&] { return job_->done; });
    auto rawBytes = std::move(job_->raw);
    auto output = std::move(job_->output);
    const auto status = job_->status;
    const auto reserved = job_->reservedBytes;
    auto state = state_;
    job_.reset();
    state_.reset();
    lock.unlock();
    return CompletedPayload(std::move(state), reserved, std::move(rawBytes),
                            std::move(output), status);
}

void ParallelCompressionExecutor::Task::discard() noexcept {
    if (!state_ || !job_) return;
    std::unique_lock lock(state_->mutex);
    job_->completed.wait(lock, [&] { return job_->done; });
    const auto reserved = job_->reservedBytes;
    std::vector<std::uint8_t>().swap(job_->raw);
    std::vector<std::uint8_t>().swap(job_->output);
    auto state = state_;
    job_.reset();
    state_.reset();
    lock.unlock();
    ParallelCompressionExecutor::releaseBudget(state, reserved);
}

ParallelCompressionExecutor::ParallelCompressionExecutor(ParallelCompressionLimits limits,
                                                         Encoder encoder) {
    limits.workers = std::clamp<std::size_t>(limits.workers, 1, 64);
    if (limits.maxInFlightJobs == 0) limits.maxInFlightJobs = limits.workers;
    if (limits.maxInFlightBytes == 0) limits.maxInFlightBytes = 1;
    if (!encoder) {
        encoder = [](const std::vector<std::uint8_t>& raw, std::size_t maxPayloadBytes) {
            return core::protocol::encodeCompressedDataPayload(
                raw.data(), raw.size(), maxPayloadBytes);
        };
    }
    state_ = std::make_shared<State>(limits, std::move(encoder));
    state_->workers.reserve(limits.workers);
    try {
    for (std::size_t index = 0; index < limits.workers; ++index) {
        state_->workers.emplace_back([state = state_]() {
            for (;;) {
                std::shared_ptr<Job> job;
                {
                    std::unique_lock lock(state->mutex);
                    state->workAvailable.wait(lock, [&] {
                        return state->stopping || !state->queue.empty();
                    });
                    if (state->stopping && state->queue.empty()) return;
                    job = std::move(state->queue.front());
                    state->queue.pop_front();
                    ++state->activeWorkers;
                    state->maxActiveWorkers = std::max(state->maxActiveWorkers,
                                                       state->activeWorkers);
                }
                common::Result<std::vector<std::uint8_t>> encoded(
                    common::Status::runtimeError("parallel compression encoder did not run"));
                try {
                    encoded = state->encoder(job->raw, job->maxPayloadBytes);
                } catch (const std::exception& error) {
                    encoded = common::Status::runtimeError(
                        std::string("parallel compression exception: ") + error.what());
                } catch (...) {
                    encoded = common::Status::runtimeError("parallel compression unknown exception");
                }
                {
                    std::lock_guard lock(state->mutex);
                    --state->activeWorkers;
                    if (encoded.isOk()) job->output = std::move(encoded.value());
                    else job->status = encoded.status();
                    ++state->totalCompleted;
                    state->peakInFlightJobs = std::max(state->peakInFlightJobs, state->inFlightJobs);
                    job->done = true;
                }
                job->completed.notify_all();
            }
        });
    }
    } catch (...) {
        {
            std::lock_guard lock(state_->mutex);
            state_->stopping = true;
        }
        state_->workAvailable.notify_all();
        for (auto& worker : state_->workers)
            if (worker.joinable()) worker.join();
        throw;
    }
}

ParallelCompressionExecutor::~ParallelCompressionExecutor() {
    if (!state_) return;
    {
        std::lock_guard lock(state_->mutex);
        state_->stopping = true;
    }
    state_->workAvailable.notify_all();
    for (auto& worker : state_->workers)
        if (worker.joinable()) worker.join();
}

common::Result<std::optional<ParallelCompressionExecutor::Task>>
ParallelCompressionExecutor::trySubmit(std::vector<std::uint8_t>* input,
                                       std::size_t maxPayloadBytes) {
    if (input == nullptr)
        return common::Status::invalidArgument("parallel compression input is null");
    auto outputBound = outputAllocationBound(input->size());
    if (!outputBound.isOk()) return outputBound.status();
    if (outputBound.value() > std::numeric_limits<std::size_t>::max() - input->capacity())
        return common::Status::invalidArgument("parallel compression reservation overflows");
    const auto reservation = input->capacity() + outputBound.value();
    if (reservation > state_->limits.maxInFlightBytes)
        return common::Status::invalidArgument(
            "payload exceeds the global parallel compression byte budget");
    auto job = std::make_shared<Job>();
    job->maxPayloadBytes = maxPayloadBytes;
    job->reservedBytes = reservation;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->stopping)
            return common::Status::runtimeError("parallel compression executor is stopping");
        if (state_->inFlightJobs >= state_->limits.maxInFlightJobs ||
            reservation > state_->limits.maxInFlightBytes - state_->inFlightBytes)
            return std::optional<Task>{};
        state_->queue.push_back(job);
        job->raw = std::move(*input);
        ++state_->inFlightJobs;
        state_->peakInFlightJobs = std::max(state_->peakInFlightJobs, state_->inFlightJobs);
        state_->inFlightBytes += reservation;
        state_->peakInFlightBytes = std::max(state_->peakInFlightBytes,
                                             state_->inFlightBytes);
    }
    state_->workAvailable.notify_one();
    Task task(state_, std::move(job));
    return std::optional<Task>(std::move(task));
}

common::Result<ParallelCompressionExecutor::Task> ParallelCompressionExecutor::submit(
    std::vector<std::uint8_t>* input, std::size_t maxPayloadBytes,
    const std::function<bool()>& shouldCancel) {
    for (;;) {
        if (shouldCancel && shouldCancel())
            return common::Status::runtimeError("parallel compression admission cancelled");
        auto result = trySubmit(input, maxPayloadBytes);
        if (!result.isOk()) return result.status();
        if (result.value()) return std::move(*result.value());
        if (shouldCancel && shouldCancel())
            return common::Status::runtimeError("parallel compression admission cancelled");
        auto bound = outputAllocationBound(input->size());
        if (!bound.isOk()) return bound.status();
        const auto reservation = input->capacity() + bound.value();
        std::unique_lock lock(state_->mutex);
        state_->capacityAvailable.wait_for(lock, std::chrono::milliseconds(10), [&] {
            return state_->stopping ||
                (state_->inFlightJobs < state_->limits.maxInFlightJobs &&
                 reservation <= state_->limits.maxInFlightBytes - state_->inFlightBytes);
        });
        if (state_->stopping)
            return common::Status::runtimeError("parallel compression executor is stopping");
    }
}

ParallelCompressionSnapshot ParallelCompressionExecutor::snapshot() const {
    std::lock_guard lock(state_->mutex);
    return {state_->limits.workers, state_->limits.maxInFlightJobs,
            state_->inFlightJobs, state_->inFlightBytes, state_->peakInFlightBytes,
            state_->peakInFlightJobs,
            state_->maxActiveWorkers, state_->limits.maxInFlightBytes,
            state_->totalCompleted};
}

ParallelCompressionExecutor& sharedParallelCompressionExecutor() {
    static ParallelCompressionExecutor executor;
    return executor;
}

}  // namespace cpnetflux::core::io
