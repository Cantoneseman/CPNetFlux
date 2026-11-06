#include "cpnetflux/core/io/payload_pipeline.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

namespace cpnetflux::core::io {
namespace {
using common::Status;

struct PipelineState {
    explicit PipelineState(std::size_t byteBudget, std::size_t chunkBudget, bool observe, ProducerWakePolicy wake)
        : maxBytes(byteBudget), maxChunks(chunkBudget),
          maxRecycledChunks(chunkBudget == std::numeric_limits<std::size_t>::max()
              ? chunkBudget : chunkBudget + 1),
          maxRecycledBytes(byteBudget + std::min(
              chunkBudget ? byteBudget / chunkBudget : 0,
              std::numeric_limits<std::size_t>::max() - byteBudget)), observeWaits(observe), wakePolicy(wake) {
        if (byteBudget == 0 || chunkBudget == 0) {
            terminal = Status::invalidArgument("payload pipeline budgets must be positive");
            cancelled = true;
        }
    }

    const std::size_t maxBytes;
    const std::size_t maxChunks;
    // A waiting consumer can leave depth+1 free buffers and one producer buffer.
    const std::size_t maxRecycledChunks;
    const std::size_t maxRecycledBytes;
    mutable std::mutex mutex;
    std::condition_variable canPush;
    std::condition_variable canPop;
    std::deque<PreparedPayload> queue;
    std::vector<std::vector<std::uint8_t>> recycledBuffers;
    std::size_t recycledCapacityBytes = 0;
    std::size_t queuedBytes = 0;
    std::size_t highWatermarkBytes = 0;
    std::uint64_t producerWaitCount = 0;
    std::uint64_t consumerWaitCount = 0;
    const bool observeWaits;
    const ProducerWakePolicy wakePolicy;
    double producerWaitSeconds = 0, consumerWaitSeconds = 0;
    bool closed = false;
    bool cancelled = false;
    Status terminal = Status::ok();
};

}  // namespace

struct BoundedPayloadPipeline::State : PipelineState {
    using PipelineState::PipelineState;
};

BoundedPayloadPipeline::BoundedPayloadPipeline(std::size_t maxBytes, std::size_t maxChunks, bool observeWaits,
                                               ProducerWakePolicy wakePolicy)
    : state_(std::make_shared<State>(maxBytes, maxChunks, observeWaits, wakePolicy)) {}

common::Status BoundedPayloadPipeline::push(PreparedPayload payload,
                                           std::vector<std::uint8_t>* reusableBuffer) {
    const auto payloadBytes = payload.bytes.size();
    auto state = state_;
    std::unique_lock<std::mutex> lock(state->mutex);
    if (payloadBytes > state->maxBytes) {
        return Status::invalidArgument("payload exceeds pipeline byte budget");
    }
    const auto canPush = [&] {
        return state->cancelled || state->closed ||
            (state->queue.size() < state->maxChunks &&
             payloadBytes <= state->maxBytes - state->queuedBytes);
    };
    if (!canPush()) {
        ++state->producerWaitCount;
        const auto started = state->observeWaits ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        // A blocked SPSC producer can refill the same bounded slots together.
        // Close/cancel must bypass this hysteresis, including a partial drain.
        state->canPush.wait(lock, [&] {
            return state->cancelled || state->closed || (canPush() &&
                (state->wakePolicy == ProducerWakePolicy::AvailableCapacity || state->queue.empty()));
        });
        if (state->observeWaits) state->producerWaitSeconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    }
    if (state->cancelled || !state->terminal.isOk()) return state->terminal;
    if (state->closed) return Status::runtimeError("payload pipeline is closed");
    state->queuedBytes += payloadBytes;
    state->highWatermarkBytes = std::max(state->highWatermarkBytes, state->queuedBytes);
    state->queue.push_back(std::move(payload));
    if (reusableBuffer != nullptr) {
        *reusableBuffer = {};
        if (!state->recycledBuffers.empty()) {
            *reusableBuffer = std::move(state->recycledBuffers.back());
            state->recycledBuffers.pop_back();
            state->recycledCapacityBytes -= reusableBuffer->capacity();
        }
    }
    lock.unlock();
    state->canPop.notify_one();
    return Status::ok();
}

common::Result<std::optional<PreparedPayload>> BoundedPayloadPipeline::pop(
    std::vector<std::uint8_t> reusableBuffer) {
    auto state = state_;
    std::unique_lock<std::mutex> lock(state->mutex);
    const auto capacity = reusableBuffer.capacity();
    if (!state->closed && !state->cancelled && capacity != 0 &&
        state->recycledBuffers.size() < state->maxRecycledChunks &&
        capacity <= state->maxRecycledBytes - state->recycledCapacityBytes) {
        state->recycledBuffers.push_back(std::move(reusableBuffer));
        state->recycledCapacityBytes += capacity;
    }
    const auto canPop = [&] {
        return state->cancelled || !state->queue.empty() || state->closed;
    };
    if (!canPop()) {
        ++state->consumerWaitCount;
        const auto started = state->observeWaits ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        state->canPop.wait(lock, canPop);
        if (state->observeWaits) state->consumerWaitSeconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    }
    if (state->cancelled || !state->terminal.isOk()) return state->terminal;
    if (state->queue.empty()) return std::optional<PreparedPayload>{};
    PreparedPayload payload = std::move(state->queue.front());
    state->queue.pop_front();
    state->queuedBytes -= payload.bytes.size();
    const bool wakeProducer = state->wakePolicy == ProducerWakePolicy::AvailableCapacity ||
        state->queue.empty();
    lock.unlock();
    if (wakeProducer) state->canPush.notify_one();
    return std::optional<PreparedPayload>(std::move(payload));
}

void BoundedPayloadPipeline::close() {
    auto state = state_;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->cancelled) state->closed = true;
        state->recycledBuffers.clear();
        state->recycledCapacityBytes = 0;
    }
    state->canPush.notify_all();
    state->canPop.notify_all();
}

void BoundedPayloadPipeline::cancel(common::Status reason) {
    auto state = state_;
    if (reason.isOk()) reason = Status::runtimeError("payload pipeline cancelled");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->cancelled = true;
        state->closed = true;
        state->terminal = std::move(reason);
        state->queue.clear();
        state->queuedBytes = 0;
        state->recycledBuffers.clear();
        state->recycledCapacityBytes = 0;
    }
    state->canPush.notify_all();
    state->canPop.notify_all();
}

std::size_t BoundedPayloadPipeline::queuedBytes() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->queuedBytes;
}

std::size_t BoundedPayloadPipeline::highWatermarkBytes() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->highWatermarkBytes;
}

std::size_t BoundedPayloadPipeline::recycledCapacityBytes() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->recycledCapacityBytes;
}

std::uint64_t BoundedPayloadPipeline::producerWaitCount() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->producerWaitCount;
}

std::uint64_t BoundedPayloadPipeline::consumerWaitCount() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->consumerWaitCount;
}

double BoundedPayloadPipeline::producerWaitSeconds() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->producerWaitSeconds;
}
double BoundedPayloadPipeline::consumerWaitSeconds() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->consumerWaitSeconds;
}
common::Status BoundedPayloadPipeline::status() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->terminal;
}

}  // namespace cpnetflux::core::io
