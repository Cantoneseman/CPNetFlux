#include <chrono>
#include <iostream>

#include "gridflux/config/file_transfer_options.h"
#include "gridflux/core/io/file_transfer_client.h"
#include "gridflux/core/metrics/event_log.h"

int main(int argc, char** argv) {
    const auto options = gridflux::config::parseFileTransferOptions(
        argc, argv, gridflux::config::FileTransferRole::Client);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << gridflux::config::fileTransferUsage(argv[0],
                                                         gridflux::config::FileTransferRole::Client)
                  << '\n';
        return 2;
    }

    (void)gridflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        gridflux::core::metrics::EventRecord{"gridflux-file-client",
                                             "transfer_start",
                                             options.value().transferId,
                                             "upload",
                                             options.value().path,
                                             "pass",
                                             gridflux::core::metrics::ErrorCode::Ok,
                                             "",
                                             0.0,
                                             0});
    const auto start = std::chrono::steady_clock::now();
    const auto status = gridflux::core::io::runFileTransferClient(options.value());
    (void)gridflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        gridflux::core::metrics::EventRecord{
            "gridflux-file-client",
            "transfer_complete",
            options.value().transferId,
            "upload",
            options.value().path,
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
