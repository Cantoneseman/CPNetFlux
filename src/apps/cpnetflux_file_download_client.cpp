#include <iostream>
#include <chrono>

#include "cpnetflux/config/file_download_options.h"
#include "cpnetflux/core/io/file_download_client.h"
#include "cpnetflux/core/metrics/event_log.h"

int main(int argc, char** argv) {
    const auto options = cpnetflux::config::parseFileDownloadOptions(argc, argv);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << cpnetflux::config::fileDownloadUsage(argv[0]) << '\n';
        return 2;
    }

    (void)cpnetflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        cpnetflux::core::metrics::EventRecord{"cpnetflux-file-download-client",
                                             "transfer_start",
                                             options.value().transferId,
                                             "download",
                                             options.value().path,
                                             "pass",
                                             cpnetflux::core::metrics::ErrorCode::Ok,
                                             "",
                                             0.0,
                                             0});
    const auto start = std::chrono::steady_clock::now();
    const auto status = cpnetflux::core::io::runFileDownloadClient(options.value());
    (void)cpnetflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        cpnetflux::core::metrics::EventRecord{
            "cpnetflux-file-download-client",
            "transfer_complete",
            options.value().transferId,
            "download",
            options.value().path,
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
