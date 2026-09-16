#pragma once

#include <cstdint>
#include <string>

namespace cpnetflux::core::scheduler {

struct LinkProfile {
    std::string linkId = "link0";
    std::string remoteHost;
    std::string dataPortRange;
    double capacityGbps = 0.01;
    std::string localBindAddr;
    std::uint32_t initialConnections = 1;
    std::uint32_t maxConnections = 1;
    std::uint64_t targetQueueBytes = 0;
};

struct LinkState {
    LinkProfile profile;
    std::uint64_t readyBytes = 0;
    std::uint64_t inflightBytes = 0;
    double queueFillRatio = 0.0;
    std::uint64_t lowWatermarkHits = 0;
    std::uint64_t highWatermarkHits = 0;
    std::uint64_t rampUpCount = 0;
    std::uint64_t rampDownCount = 0;
    std::uint64_t sendPressureHighCount = 0;
    std::uint64_t writePressureHighCount = 0;
    std::uint64_t cpuPressureHighCount = 0;
    std::uint64_t pausedCount = 0;
    std::uint64_t retryCount = 0;
    double sendPressure = 0.0;
    double writePressure = 0.0;
    double cpuPressure = 0.0;
    double maxSendPressure = 0.0;
    double maxWritePressure = 0.0;
    double maxCpuPressure = 0.0;
    std::uint32_t currentConnections = 1;
    std::uint32_t targetConnections = 1;
    bool paused = false;
};

}  // namespace cpnetflux::core::scheduler
