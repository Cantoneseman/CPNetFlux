#pragma once

#include "cpnetflux/common/status.h"
#include "cpnetflux/config/file_transfer_options.h"
#include "cpnetflux/core/io/socket_utils.h"

namespace cpnetflux::core::io {

common::Status runFileTransferServer(const config::FileTransferOptions& options);
common::Status runFileTransferServerOnListener(const config::FileTransferOptions& options,
                                               UniqueFd listener);

}  // namespace cpnetflux::core::io
