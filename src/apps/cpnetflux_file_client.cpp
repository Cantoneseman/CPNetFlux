#include <chrono>
#include <iostream>

#include "cpnetflux/config/file_transfer_options.h"
#include "cpnetflux/core/io/file_transfer_client.h"
#include "cpnetflux/core/metrics/event_log.h"

int main(int argc, char** argv) {
    const auto options = cpnetflux::config::parseFileTransferOptions(
        argc, argv, cpnetflux::config::FileTransferRole::Client);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << cpnetflux::config::fileTransferUsage(argv[0],
                                                         cpnetflux::config::FileTransferRole::Client)
                  << '\n';
        return 2;
    }

    (void)cpnetflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        cpnetflux::core::metrics::EventRecord{"cpnetflux-file-client",
                                             "transfer_start",
                                             options.value().transferId,
                                             "upload",
                                             options.value().path,
                                             "pass",
                                             cpnetflux::core::metrics::ErrorCode::Ok,
                                             "",
                                             0.0,
                                             0});
    const auto start = std::chrono::steady_clock::now();
    const auto status = cpnetflux::core::io::runFileTransferClient(options.value());
    (void)cpnetflux::core::metrics::writeEventLog(
        options.value().eventLogPath,
        cpnetflux::core::metrics::EventRecord{
            "cpnetflux-file-client",
            "transfer_complete",
            options.value().transferId,
            "upload",
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
