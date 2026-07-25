#pragma once

#include "gridflux/common/status.h"
#include "gridflux/config/file_download_options.h"

namespace gridflux::core::io {

common::Status runFileDownloadClient(const config::FileDownloadOptions& options);

}  // namespace gridflux::core::io
