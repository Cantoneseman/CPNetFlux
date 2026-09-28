#pragma once

#include <cstdint>
#include <string>

#include "cpnetflux/common/status.h"

namespace cpnetflux::config {

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

}  // namespace cpnetflux::config
