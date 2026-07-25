#pragma once

#include <string>

#include "gridflux/checkpoint/transfer_manifest.h"
#include "gridflux/common/status.h"

namespace gridflux::checkpoint {

class ManifestStore {
   public:
    [[nodiscard]] static common::Status saveAtomic(const std::string& path,
                                                   const TransferManifest& manifest);
    [[nodiscard]] static common::Result<TransferManifest> load(const std::string& path);
};

}  // namespace gridflux::checkpoint
