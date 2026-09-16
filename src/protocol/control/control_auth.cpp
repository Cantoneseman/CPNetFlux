#include "cpnetflux/protocol/control/control_auth.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

namespace cpnetflux::protocol::control {
namespace {

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

void trimLineEnding(std::string* value) {
    while (!value->empty() && (value->back() == '\n' || value->back() == '\r')) {
        value->pop_back();
    }
}

}  // namespace

common::Result<AuthMode> parseAuthMode(std::string_view value) {
    const std::string normalized = lower(value);
    if (normalized == "anonymous") {
        return AuthMode::Anonymous;
    }
    if (normalized == "token") {
        return AuthMode::Token;
    }
    return common::Status::invalidArgument("--auth-mode must be anonymous or token");
}

std::string authModeName(AuthMode mode) {
    switch (mode) {
        case AuthMode::Anonymous:
            return "anonymous";
        case AuthMode::Token:
            return "token";
    }
    return "unknown";
}

common::Result<std::string> loadTokenFile(const std::string& path) {
    if (path.empty()) {
        return common::Status::invalidArgument("--auth-token-file is required in token mode");
    }
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return common::Status::invalidArgument("auth token file cannot be read");
    }
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return common::Status::invalidArgument("auth token file must be a regular file");
    }

    struct stat statBuffer {};
    if (::stat(path.c_str(), &statBuffer) != 0) {
        return common::Status::invalidArgument("auth token file cannot be inspected");
    }
    if ((statBuffer.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        return common::Status::invalidArgument(
            "auth token file permissions must not allow group or other access");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return common::Status::invalidArgument("auth token file cannot be read");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string token = buffer.str();
    trimLineEnding(&token);
    if (token.empty()) {
        return common::Status::invalidArgument("auth token file is empty");
    }
    if (token.find('\0') != std::string::npos) {
        return common::Status::invalidArgument("auth token file contains invalid data");
    }
    return token;
}

}  // namespace cpnetflux::protocol::control
