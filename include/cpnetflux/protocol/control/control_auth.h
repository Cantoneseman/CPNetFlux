#pragma once

#include <string>
#include <string_view>

#include "cpnetflux/common/status.h"

namespace cpnetflux::protocol::control {

enum class AuthMode {
    Anonymous,
    Token,
};

enum class AccessPolicy {
    ReadWrite,
    ReadOnly,
    WriteOnly,
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
[[nodiscard]] common::Result<AccessPolicy> parseAccessPolicy(std::string_view value);
[[nodiscard]] const char* accessPolicyName(AccessPolicy policy) noexcept;
[[nodiscard]] bool accessPolicyAllowsRead(AccessPolicy policy) noexcept;
[[nodiscard]] bool accessPolicyAllowsWrite(AccessPolicy policy) noexcept;
[[nodiscard]] bool constantTimeEquals(std::string_view left, std::string_view right) noexcept;
[[nodiscard]] common::Result<std::string> loadTokenFile(const std::string& path);

}  // namespace cpnetflux::protocol::control
