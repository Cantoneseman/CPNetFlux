#pragma once

#include "cpnetflux/common/status.h"
#include "cpnetflux/protocol/control/control_options.h"

namespace cpnetflux::protocol::control {

[[nodiscard]] common::Status runControlServer(const ControlServerOptions& options);

}  // namespace cpnetflux::protocol::control
