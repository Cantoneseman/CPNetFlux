#pragma once

#include "cpnetflux/common/status.h"
#include "cpnetflux/config/file_transfer_options.h"

namespace cpnetflux::core::io {

common::Status runFileTransferClient(const config::FileTransferOptions& options);

}  // namespace cpnetflux::core::io
