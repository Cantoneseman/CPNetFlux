#pragma once

#include "gridflux/common/status.h"
#include "gridflux/protocol/control/control_options.h"

namespace gridflux::protocol::control {

[[nodiscard]] common::Status runControlServer(const ControlServerOptions& options);

}  // namespace gridflux::protocol::control
