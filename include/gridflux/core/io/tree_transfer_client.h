#pragma once

#include "gridflux/common/status.h"
#include "gridflux/config/tree_transfer_options.h"

namespace gridflux::core::io {

[[nodiscard]] common::Status runTreeUploadClient(const config::TreeTransferOptions& options);
[[nodiscard]] common::Status runTreeDownloadClient(const config::TreeTransferOptions& options);

}  // namespace gridflux::core::io
