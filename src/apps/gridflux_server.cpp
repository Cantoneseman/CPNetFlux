#include <iostream>

#include "gridflux/config/sink_options.h"
#include "gridflux/core/io/tcp_sink_server.h"

int main(int argc, char** argv) {
    const auto options =
        gridflux::config::parseSinkOptions(argc, argv, gridflux::config::SinkRole::Server);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << gridflux::config::sinkUsage(argv[0], gridflux::config::SinkRole::Server)
                  << '\n';
        return 2;
    }

    const auto status = gridflux::core::io::runTcpSinkServer(options.value());
    if (!status.isOk()) {
        std::cerr << status.message() << '\n';
        return 1;
    }

    return 0;
}
