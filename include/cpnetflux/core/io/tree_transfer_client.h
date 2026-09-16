#pragma once

#include "cpnetflux/common/status.h"
#include "cpnetflux/config/tree_transfer_options.h"

namespace cpnetflux::core::io {

[[nodiscard]] common::Status runTreeUploadClient(const config::TreeTransferOptions& options);
[[nodiscard]] common::Status runTreeDownloadClient(const config::TreeTransferOptions& options);

}  // namespace cpnetflux::core::io
