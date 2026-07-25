#pragma once

#include "gridflux/common/status.h"
#include "gridflux/config/file_transfer_options.h"
#include "gridflux/core/io/socket_utils.h"

namespace gridflux::core::io {

common::Status runFileTransferServer(const config::FileTransferOptions& options);
common::Status runFileTransferServerOnListener(const config::FileTransferOptions& options,
                                               UniqueFd listener);

}  // namespace gridflux::core::io
