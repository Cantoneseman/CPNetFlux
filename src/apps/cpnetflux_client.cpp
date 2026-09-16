#include <iostream>

#include "cpnetflux/config/sink_options.h"
#include "cpnetflux/core/io/tcp_sink_client.h"

int main(int argc, char** argv) {
    const auto options =
        cpnetflux::config::parseSinkOptions(argc, argv, cpnetflux::config::SinkRole::Client);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << cpnetflux::config::sinkUsage(argv[0], cpnetflux::config::SinkRole::Client)
                  << '\n';
        return 2;
    }

    const auto status = cpnetflux::core::io::runTcpSinkClient(options.value());
    if (!status.isOk()) {
        std::cerr << status.message() << '\n';
        return 1;
    }

    return 0;
}
