#pragma once

#include <cstdint>
#include <string>

#include "gridflux/common/status.h"

namespace gridflux::config {

enum class SinkRole {
    Server,
    Client,
};

struct SinkOptions {
    std::string host;
    std::uint16_t port = 9000;
    std::uint32_t connections = 1;
    std::uint64_t bytes = 0;
    std::uint32_t bufferSize = 65536;
};

common::Result<SinkOptions> parseSinkOptions(int argc, const char* const* argv, SinkRole role);
std::string sinkUsage(const char* programName, SinkRole role);

}  // namespace gridflux::config
