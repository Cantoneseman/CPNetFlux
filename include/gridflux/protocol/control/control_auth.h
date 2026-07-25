#pragma once

#include <string>
#include <string_view>

#include "gridflux/common/status.h"

namespace gridflux::protocol::control {

enum class AuthMode {
    Anonymous,
    Token,
};

struct ControlAuthConfig {
    AuthMode mode = AuthMode::Anonymous;
    std::string user = "gridflux";
    std::string password = "gridflux";
    std::string token;
    std::string tokenFile;
};

[[nodiscard]] common::Result<AuthMode> parseAuthMode(std::string_view value);
[[nodiscard]] std::string authModeName(AuthMode mode);
[[nodiscard]] common::Result<std::string> loadTokenFile(const std::string& path);

}  // namespace gridflux::protocol::control
