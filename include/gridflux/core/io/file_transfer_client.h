#pragma once

#include "gridflux/common/status.h"
#include "gridflux/config/file_transfer_options.h"

namespace gridflux::core::io {

common::Status runFileTransferClient(const config::FileTransferOptions& options);

}  // namespace gridflux::core::io
