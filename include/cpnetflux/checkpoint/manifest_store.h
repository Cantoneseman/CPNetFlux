#pragma once

#include <string>

#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/common/status.h"

namespace cpnetflux::checkpoint {

class ManifestStore {
   public:
    [[nodiscard]] static common::Status saveAtomic(const std::string& path,
                                                   const TransferManifest& manifest);
    [[nodiscard]] static common::Result<TransferManifest> load(const std::string& path);
};

}  // namespace cpnetflux::checkpoint
