#include <iostream>
#include <chrono>

#include "cpnetflux/config/tree_transfer_options.h"
#include "cpnetflux/core/io/tree_transfer_client.h"
#include "cpnetflux/core/metrics/event_log.h"

int main(int argc, char** argv) {
    const auto options = cpnetflux::config::parseTreeTransferOptions(
        argc, argv, cpnetflux::config::TreeTransferRole::Download);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << cpnetflux::config::treeTransferUsage(
                         argv[0], cpnetflux::config::TreeTransferRole::Download)
                  << '\n';
        return 2;
    }
    (void)cpnetflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        cpnetflux::core::metrics::EventRecord{"cpnetflux-tree-download-client",
                                             "tree_start",
                                             "",
                                             "download",
                                             options.value().destDir,
                                             "pass",
                                             cpnetflux::core::metrics::ErrorCode::Ok,
                                             "",
                                             0.0,
                                             0});
    const auto start = std::chrono::steady_clock::now();
    const auto status = cpnetflux::core::io::runTreeDownloadClient(options.value());
    (void)cpnetflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        cpnetflux::core::metrics::EventRecord{
            "cpnetflux-tree-download-client",
            "tree_complete",
            "",
            "download",
            options.value().destDir,
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
