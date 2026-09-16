#include "cpnetflux/core/scheduler/link_manager.h"

#include <algorithm>
#include <limits>

namespace cpnetflux::core::scheduler {
namespace {

}  // namespace

common::Status LinkManager::addLink(const LinkProfile& profile) {
    if (profile.linkId.empty()) {
        return common::Status::invalidArgument("link_id must not be empty");
    }
    if (profile.capacityGbps <= 0.0) {
        return common::Status::invalidArgument("link capacity must be greater than zero");
    }
    if (profile.initialConnections == 0 || profile.maxConnections == 0 ||
        profile.initialConnections > profile.maxConnections) {
        return common::Status::invalidArgument("invalid link connection bounds");
    }
    if (link(profile.linkId) != nullptr) {
        return common::Status::invalidArgument("duplicate link_id: " + profile.linkId);
    }

    LinkState state;
    state.profile = profile;
    state.currentConnections = profile.initialConnections;
    state.targetConnections = profile.initialConnections;
    if (state.profile.targetQueueBytes == 0) {
        const std::uint64_t defaultTarget =
            targetQueueBytes(state, 64ULL * 1024ULL * 1024ULL, 10);
        state.profile.targetQueueBytes = defaultTarget;
    }
    links_.push_back(state);
    return common::Status::ok();
}

LinkState* LinkManager::link(const std::string& linkId) {
    auto iter = std::find_if(links_.begin(), links_.end(),
                             [&](const LinkState& state) { return state.profile.linkId == linkId; });
    if (iter == links_.end()) {
        return nullptr;
    }
    return &(*iter);
}

const LinkState* LinkManager::link(const std::string& linkId) const {
    auto iter = std::find_if(links_.begin(), links_.end(),
                             [&](const LinkState& state) { return state.profile.linkId == linkId; });
    if (iter == links_.end()) {
        return nullptr;
    }
    return &(*iter);
}

std::vector<std::string> LinkManager::linkIds() const {
    std::vector<std::string> ids;
    ids.reserve(links_.size());
    for (const LinkState& state : links_) {
        ids.push_back(state.profile.linkId);
    }
    return ids;
}

common::Status LinkManager::setReadyBytes(const std::string& linkId, std::uint64_t bytes) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    state->readyBytes = bytes;
    state->queueFillRatio = queueFillRatio(*state);
    return common::Status::ok();
}

common::Status LinkManager::addReadyBytes(const std::string& linkId, std::uint64_t bytes) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    state->readyBytes += bytes;
    state->queueFillRatio = queueFillRatio(*state);
    return common::Status::ok();
}

common::Status LinkManager::consumeReadyBytes(const std::string& linkId, std::uint64_t bytes) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    if (bytes > state->readyBytes) {
        state->readyBytes = 0;
    } else {
        state->readyBytes -= bytes;
    }
    state->queueFillRatio = queueFillRatio(*state);
    return common::Status::ok();
}

common::Status LinkManager::addInflightBytes(const std::string& linkId, std::uint64_t bytes) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    state->inflightBytes += bytes;
    return common::Status::ok();
}

common::Status LinkManager::releaseInflightBytes(const std::string& linkId,
                                                 std::uint64_t bytes) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    if (bytes > state->inflightBytes) {
        state->inflightBytes = 0;
    } else {
        state->inflightBytes -= bytes;
    }
    return common::Status::ok();
}

common::Status LinkManager::updatePressure(const std::string& linkId, double cpuPressure,
                                           double writePressure, double sendPressure) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    state->cpuPressure = std::max(0.0, cpuPressure);
    state->writePressure = std::max(0.0, writePressure);
    state->sendPressure = std::max(0.0, sendPressure);
    state->maxCpuPressure = std::max(state->maxCpuPressure, state->cpuPressure);
    state->maxWritePressure = std::max(state->maxWritePressure, state->writePressure);
    state->maxSendPressure = std::max(state->maxSendPressure, state->sendPressure);
    if (state->cpuPressure >= 85.0) {
        ++state->cpuPressureHighCount;
    }
    if (state->writePressure >= 0.75) {
        ++state->writePressureHighCount;
    }
    if (state->sendPressure >= 0.75) {
        ++state->sendPressureHighCount;
    }
    return common::Status::ok();
}

common::Status LinkManager::setPaused(const std::string& linkId, bool paused) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    state->paused = paused;
    if (paused) {
        ++state->pausedCount;
    }
    return common::Status::ok();
}

common::Status LinkManager::setTargetConnections(const std::string& linkId,
                                                 std::uint32_t targetConnections) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    const std::uint32_t clamped =
        std::max<std::uint32_t>(1, std::min(targetConnections, state->profile.maxConnections));
    state->currentConnections = clamped;
    state->targetConnections = clamped;
    return common::Status::ok();
}

common::Status LinkManager::recordWatermarkHit(const std::string& linkId, bool lowWatermark,
                                               bool highWatermark) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    if (lowWatermark) {
        ++state->lowWatermarkHits;
    }
    if (highWatermark) {
        ++state->highWatermarkHits;
    }
    return common::Status::ok();
}

common::Status LinkManager::recordRamp(const std::string& linkId, bool rampUp, bool rampDown) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    if (rampUp) {
        ++state->rampUpCount;
    }
    if (rampDown) {
        ++state->rampDownCount;
    }
    return common::Status::ok();
}

common::Status LinkManager::addRetryCount(const std::string& linkId,
                                          std::uint64_t retryCount) {
    auto* state = link(linkId);
    if (state == nullptr) {
        return common::Status::invalidArgument("unknown link_id: " + linkId);
    }
    state->retryCount += retryCount;
    return common::Status::ok();
}

std::uint64_t LinkManager::estimatedBdpBytes(const LinkState& state,
                                             std::uint64_t defaultRttMs) const noexcept {
    const double bps = state.profile.capacityGbps * 1'000'000'000.0;
    const double seconds = static_cast<double>(defaultRttMs) / 1000.0;
    const double bytes = (bps * seconds) / 8.0;
    if (bytes <= 0.0) {
        return 0;
    }
    if (bytes >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(bytes);
}

std::uint64_t LinkManager::targetQueueBytes(const LinkState& state, std::uint64_t minWorkItemBytes,
                                            std::uint64_t defaultRttMs) const noexcept {
    const std::uint64_t estimated = estimatedBdpBytes(state, defaultRttMs);
    const std::uint64_t candidate = estimated > std::numeric_limits<std::uint64_t>::max() / 2
                                        ? std::numeric_limits<std::uint64_t>::max()
                                        : estimated * 2;
    return clampTargetQueueBytes(candidate, minWorkItemBytes);
}

double LinkManager::queueFillRatio(const LinkState& state) const noexcept {
    const std::uint64_t target = state.profile.targetQueueBytes;
    if (target == 0) {
        return 0.0;
    }
    return static_cast<double>(state.readyBytes) / static_cast<double>(target);
}

bool LinkManager::shouldPause(const LinkState& state) const noexcept {
    if (state.profile.targetQueueBytes == 0) {
        return false;
    }
    if (state.profile.targetQueueBytes >
        std::numeric_limits<std::uint64_t>::max() / 2ULL) {
        return state.readyBytes == std::numeric_limits<std::uint64_t>::max();
    }
    return state.readyBytes >= state.profile.targetQueueBytes * 2ULL;
}

std::uint64_t LinkManager::clampTargetQueueBytes(std::uint64_t candidate,
                                                 std::uint64_t minWorkItemBytes) noexcept {
    return std::max(candidate, minWorkItemBytes);
}

}  // namespace cpnetflux::core::scheduler
