#include <iostream>
#include <chrono>

#include "cpnetflux/config/tree_transfer_options.h"
#include "cpnetflux/core/io/tree_transfer_client.h"
#include "cpnetflux/core/metrics/event_log.h"

int main(int argc, char** argv) {
    const auto options = cpnetflux::config::parseTreeTransferOptions(
        argc, argv, cpnetflux::config::TreeTransferRole::Upload);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << cpnetflux::config::treeTransferUsage(
                         argv[0], cpnetflux::config::TreeTransferRole::Upload)
                  << '\n';
        return 2;
    }
    (void)cpnetflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        cpnetflux::core::metrics::EventRecord{"cpnetflux-tree-upload-client",
                                             "tree_start",
                                             "",
                                             "upload",
                                             options.value().sourceDir,
                                             "pass",
                                             cpnetflux::core::metrics::ErrorCode::Ok,
                                             "",
                                             0.0,
                                             0});
    const auto start = std::chrono::steady_clock::now();
    const auto status = cpnetflux::core::io::runTreeUploadClient(options.value());
    (void)cpnetflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        cpnetflux::core::metrics::EventRecord{
            "cpnetflux-tree-upload-client",
            "tree_complete",
            "",
            "upload",
            options.value().sourceDir,
            status.isOk() ? "pass" : "fail",
            cpnetflux::core::metrics::classifyStatus(status),
            status.isOk() ? "" : status.message(),
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
            0});
    if (!status.isOk()) {
        std::cerr << status.message() << '\n';
        return 1;
    }
    return 0;
}
