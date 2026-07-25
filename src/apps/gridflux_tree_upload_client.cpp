#include <iostream>
#include <chrono>

#include "gridflux/config/tree_transfer_options.h"
#include "gridflux/core/io/tree_transfer_client.h"
#include "gridflux/core/metrics/event_log.h"

int main(int argc, char** argv) {
    const auto options = gridflux::config::parseTreeTransferOptions(
        argc, argv, gridflux::config::TreeTransferRole::Upload);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << gridflux::config::treeTransferUsage(
                         argv[0], gridflux::config::TreeTransferRole::Upload)
                  << '\n';
        return 2;
    }
    (void)gridflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        gridflux::core::metrics::EventRecord{"gridflux-tree-upload-client",
                                             "tree_start",
                                             "",
                                             "upload",
                                             options.value().sourceDir,
                                             "pass",
                                             gridflux::core::metrics::ErrorCode::Ok,
                                             "",
                                             0.0,
                                             0});
    const auto start = std::chrono::steady_clock::now();
    const auto status = gridflux::core::io::runTreeUploadClient(options.value());
    (void)gridflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        gridflux::core::metrics::EventRecord{
            "gridflux-tree-upload-client",
            "tree_complete",
            "",
            "upload",
            options.value().sourceDir,
            status.isOk() ? "pass" : "fail",
            gridflux::core::metrics::classifyStatus(status),
            status.isOk() ? "" : status.message(),
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
            0});
    if (!status.isOk()) {
        std::cerr << status.message() << '\n';
        return 1;
    }
    return 0;
}
