#pragma once

#include <string>
#include <string_view>

#include "cpnetflux/common/status.h"

namespace cpnetflux::protocol::control {

enum class AuthMode {
    Anonymous,
    Token,
};

struct ControlAuthConfig {
    AuthMode mode = AuthMode::Anonymous;
    std::string user = "cpnetflux";
    std::string password = "cpnetflux";
    std::string token;
    std::string tokenFile;
};

[[nodiscard]] common::Result<AuthMode> parseAuthMode(std::string_view value);
[[nodiscard]] std::string authModeName(AuthMode mode);
[[nodiscard]] common::Result<std::string> loadTokenFile(const std::string& path);

}  // namespace cpnetflux::protocol::control
