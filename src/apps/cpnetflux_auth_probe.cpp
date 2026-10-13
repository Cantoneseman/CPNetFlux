#include <iostream>
#include <string>

#include "cpnetflux/config/tree_transfer_options.h"
#include "cpnetflux/core/io/tree_transfer_client.h"

namespace {
std::string jsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char ch : value) {
        if (ch == '\\') result += "\\\\";
        else if (ch == '"') result += "\\\"";
        else if (ch == '\n') result += "\\n";
        else if (ch == '\r') result += "\\r";
        else result += ch;
    }
    return result;
}
}

int main(int argc, char** argv) {
    const auto options = cpnetflux::config::parseTreeTransferOptions(
        argc, argv, cpnetflux::config::TreeTransferRole::Upload);
    if (!options.isOk() || !options.value().authProbe) {
        std::cerr << (options.isOk() ? "--auth-probe is required" : options.status().message())
                  << '\n'
                  << cpnetflux::config::treeTransferUsage(
                         argv[0], cpnetflux::config::TreeTransferRole::Upload)
                  << '\n';
        return 2;
    }
    const auto result = cpnetflux::core::io::runTreeAuthProbe(options.value());
    std::cout << "{\"dns_control_port\":\"" << result.dnsControlPort
              << "\",\"tls_identity\":\"" << result.tlsIdentity
              << "\",\"login\":\"" << result.login
              << "\",\"capabilities\":\"" << result.capabilities
              << "\",\"authentication\":\"" << result.authentication
              << "\",\"error\":\"" << jsonEscape(result.error) << "\"}\n";
    return result.authentication == "authenticated" ? 0 : 1;
}
