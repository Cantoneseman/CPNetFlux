#pragma once

#include "cpnetflux/common/status.h"
#include "cpnetflux/config/tree_transfer_options.h"

namespace cpnetflux::core::io {

struct TreeAuthProbeResult {
    std::string dnsControlPort = "pending";
    std::string tlsIdentity = "pending";
    std::string login = "pending";
    std::string capabilities = "pending";
    std::string authentication = "unknown";
    std::string error;
};

[[nodiscard]] common::Status runTreeUploadClient(const config::TreeTransferOptions& options);
[[nodiscard]] common::Status runTreeDownloadClient(const config::TreeTransferOptions& options);
[[nodiscard]] TreeAuthProbeResult runTreeAuthProbe(const config::TreeTransferOptions& options);

}  // namespace cpnetflux::core::io
