#include <iostream>
#include <string_view>

#include "gridflux/protocol/control/control_options.h"
#include "gridflux/protocol/control/control_server.h"

int main(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "-h" || option == "--help") {
            std::cout << gridflux::protocol::control::controlServerUsage(argv[0]) << '\n';
            return 0;
        }
    }

    const auto options = gridflux::protocol::control::parseControlServerOptions(argc, argv);
    if (!options.isOk()) {
        std::cerr << options.status().message() << '\n'
                  << gridflux::protocol::control::controlServerUsage(argv[0]) << '\n';
        return 2;
    }

    const auto status = gridflux::protocol::control::runControlServer(options.value());
    if (!status.isOk()) {
        std::cerr << status.message() << '\n';
        return 1;
    }

    return 0;
}
