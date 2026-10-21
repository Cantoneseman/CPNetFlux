#include "cpnetflux/core/io/payload_pipeline.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

namespace cpnetflux::core::io {
namespace {
using common::Status;

struct PipelineState {
    explicit PipelineState(std::size_t byteBudget, std::size_t chunkBudget)
        : maxBytes(byteBudget), maxChunks(chunkBudget) {
        if (byteBudget == 0 || chunkBudget == 0) {
            terminal = Status::invalidArgument("payload pipeline budgets must be positive");
            cancelled = true;
        }
    }

    const std::size_t maxBytes;
    const std::size_t maxChunks;
    mutable std::mutex mutex;
    std::condition_variable canPush;
    std::condition_variable canPop;
    std::deque<PreparedPayload> queue;
    std::size_t queuedBytes = 0;
    std::size_t highWatermarkBytes = 0;
    std::uint64_t producerWaitCount = 0;
    std::uint64_t consumerWaitCount = 0;
    bool closed = false;
    bool cancelled = false;
    Status terminal = Status::ok();
};

}  // namespace

struct BoundedPayloadPipeline::State : PipelineState {
    using PipelineState::PipelineState;
};

BoundedPayloadPipeline::BoundedPayloadPipeline(std::size_t maxBytes, std::size_t maxChunks)
    : state_(std::make_shared<State>(maxBytes, maxChunks)) {}

common::Status BoundedPayloadPipeline::push(PreparedPayload payload) {
    const auto payloadBytes = payload.byteSize();
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
        state->canPush.wait(lock, canPush);
    }
    if (state->cancelled || !state->terminal.isOk()) return state->terminal;
    if (state->closed) return Status::runtimeError("payload pipeline is closed");
    state->queuedBytes += payloadBytes;
    state->highWatermarkBytes = std::max(state->highWatermarkBytes, state->queuedBytes);
    state->queue.push_back(std::move(payload));
    lock.unlock();
    state->canPop.notify_one();
    return Status::ok();
}

common::Result<std::optional<PreparedPayload>> BoundedPayloadPipeline::pop() {
    auto state = state_;
    std::unique_lock<std::mutex> lock(state->mutex);
    const auto canPop = [&] {
        return state->cancelled || !state->queue.empty() || state->closed;
    };
    if (!canPop()) {
        ++state->consumerWaitCount;
        state->canPop.wait(lock, canPop);
    }
    if (state->cancelled || !state->terminal.isOk()) return state->terminal;
    if (state->queue.empty()) return std::optional<PreparedPayload>{};
    PreparedPayload payload = std::move(state->queue.front());
    state->queue.pop_front();
    state->queuedBytes -= payload.byteSize();
    lock.unlock();
    state->canPush.notify_one();
    return std::optional<PreparedPayload>(std::move(payload));
}

void BoundedPayloadPipeline::close() {
    auto state = state_;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->cancelled) state->closed = true;
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

std::uint64_t BoundedPayloadPipeline::producerWaitCount() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->producerWaitCount;
}

std::uint64_t BoundedPayloadPipeline::consumerWaitCount() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->consumerWaitCount;
}

common::Status BoundedPayloadPipeline::status() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->terminal;
}

}  // namespace cpnetflux::core::io
