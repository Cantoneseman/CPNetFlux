#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/scheduler/link_profile.h"

namespace cpnetflux::core::scheduler {

class LinkManager {
   public:
    [[nodiscard]] common::Status addLink(const LinkProfile& profile);
    [[nodiscard]] LinkState* link(const std::string& linkId);
    [[nodiscard]] const LinkState* link(const std::string& linkId) const;
    [[nodiscard]] std::vector<std::string> linkIds() const;

    [[nodiscard]] common::Status setReadyBytes(const std::string& linkId, std::uint64_t bytes);
    [[nodiscard]] common::Status addReadyBytes(const std::string& linkId, std::uint64_t bytes);
    [[nodiscard]] common::Status consumeReadyBytes(const std::string& linkId, std::uint64_t bytes);
    [[nodiscard]] common::Status addInflightBytes(const std::string& linkId, std::uint64_t bytes);
    [[nodiscard]] common::Status releaseInflightBytes(const std::string& linkId,
                                                      std::uint64_t bytes);
    [[nodiscard]] common::Status updatePressure(const std::string& linkId, double cpuPressure,
                                                double writePressure, double sendPressure);
    [[nodiscard]] common::Status setPaused(const std::string& linkId, bool paused);
    [[nodiscard]] common::Status setTargetConnections(const std::string& linkId,
                                                      std::uint32_t targetConnections);
    [[nodiscard]] common::Status recordWatermarkHit(const std::string& linkId, bool lowWatermark,
                                                    bool highWatermark);
    [[nodiscard]] common::Status recordRamp(const std::string& linkId, bool rampUp,
                                            bool rampDown);
    [[nodiscard]] common::Status addRetryCount(const std::string& linkId,
                                               std::uint64_t retryCount);

    [[nodiscard]] std::uint64_t estimatedBdpBytes(const LinkState& state,
                                                  std::uint64_t defaultRttMs) const noexcept;
    [[nodiscard]] std::uint64_t targetQueueBytes(const LinkState& state,
                                                 std::uint64_t minWorkItemBytes,
                                                 std::uint64_t defaultRttMs) const noexcept;
    [[nodiscard]] double queueFillRatio(const LinkState& state) const noexcept;
    [[nodiscard]] bool shouldPause(const LinkState& state) const noexcept;

   private:
    [[nodiscard]] static std::uint64_t clampTargetQueueBytes(std::uint64_t candidate,
                                                             std::uint64_t minWorkItemBytes) noexcept;

    std::vector<LinkState> links_;
};

}  // namespace cpnetflux::core::scheduler
