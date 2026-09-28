#pragma once

#include "cpnetflux/common/status.h"
#include "cpnetflux/config/file_download_options.h"

namespace cpnetflux::core::io {

common::Status runFileDownloadClient(const config::FileDownloadOptions& options);

}  // namespace cpnetflux::core::io
