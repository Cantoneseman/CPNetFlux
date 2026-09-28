#pragma once

#include "cpnetflux/common/status.h"
#include "cpnetflux/config/sink_options.h"

namespace cpnetflux::core::io {

common::Status runTcpSinkServer(const config::SinkOptions& options);

}  // namespace cpnetflux::core::io
