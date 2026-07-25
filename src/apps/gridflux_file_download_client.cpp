#include <iostream>
#include <chrono>

#include "gridflux/config/file_download_options.h"
#include "gridflux/core/io/file_download_client.h"
#include "gridflux/core/metrics/event_log.h"

int main(int argc, char** argv) {
    const auto options = gridflux::config::parseFileDownloadOptions(argc, argv);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << gridflux::config::fileDownloadUsage(argv[0]) << '\n';
        return 2;
    }

    (void)gridflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        gridflux::core::metrics::EventRecord{"gridflux-file-download-client",
                                             "transfer_start",
                                             options.value().transferId,
                                             "download",
                                             options.value().path,
                                             "pass",
                                             gridflux::core::metrics::ErrorCode::Ok,
                                             "",
                                             0.0,
                                             0});
    const auto start = std::chrono::steady_clock::now();
    const auto status = gridflux::core::io::runFileDownloadClient(options.value());
    (void)gridflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        gridflux::core::metrics::EventRecord{
            "gridflux-file-download-client",
            "transfer_complete",
            options.value().transferId,
            "download",
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
