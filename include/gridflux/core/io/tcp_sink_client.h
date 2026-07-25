#pragma once

#include "gridflux/common/status.h"
#include "gridflux/config/sink_options.h"

namespace gridflux::core::io {

common::Status runTcpSinkClient(const config::SinkOptions& options);

}  // namespace gridflux::core::io
