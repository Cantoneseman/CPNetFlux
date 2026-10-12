#include "cpnetflux/protocol/control/control_auth.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
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

common::Result<AccessPolicy> parseAccessPolicy(std::string_view value) {
    const std::string normalized = lower(value);
    if (normalized == "read-write") {
        return AccessPolicy::ReadWrite;
    }
    if (normalized == "read-only") {
        return AccessPolicy::ReadOnly;
    }
    if (normalized == "write-only") {
        return AccessPolicy::WriteOnly;
    }
    return common::Status::invalidArgument(
        "--access-policy must be read-write, read-only, or write-only");
}

const char* accessPolicyName(AccessPolicy policy) noexcept {
    switch (policy) {
        case AccessPolicy::ReadWrite:
            return "read-write";
        case AccessPolicy::ReadOnly:
            return "read-only";
        case AccessPolicy::WriteOnly:
            return "write-only";
    }
    return "read-write";
}

bool accessPolicyAllowsRead(AccessPolicy policy) noexcept {
    return policy != AccessPolicy::WriteOnly;
}

bool accessPolicyAllowsWrite(AccessPolicy policy) noexcept {
    return policy != AccessPolicy::ReadOnly;
}

bool constantTimeEquals(std::string_view left, std::string_view right) noexcept {
    constexpr std::size_t kMaxCredentialLength = 4096;
    std::uint64_t difference =
        static_cast<std::uint64_t>(left.size() ^ right.size());
    difference |= static_cast<std::uint64_t>(left.size() > kMaxCredentialLength);
    difference |= static_cast<std::uint64_t>(right.size() > kMaxCredentialLength);
    for (std::size_t index = 0; index < kMaxCredentialLength; ++index) {
        const std::uint8_t leftByte = index < left.size()
                                          ? static_cast<std::uint8_t>(left[index])
                                          : 0;
        const std::uint8_t rightByte = index < right.size()
                                           ? static_cast<std::uint8_t>(right[index])
                                           : 0;
        difference |= static_cast<std::uint64_t>(leftByte ^ rightByte);
    }
    return difference == 0;
}

common::Result<std::string> loadTokenFile(const std::string& path) {
    if (path.empty()) {
        return common::Status::invalidArgument("--auth-token-file is required in token mode");
    }

    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return common::Status::invalidArgument("auth token file cannot be opened safely");
    }

    struct stat statBuffer {};
    if (::fstat(fd, &statBuffer) != 0) {
        (void)::close(fd);
        return common::Status::invalidArgument("auth token file cannot be inspected");
    }
    if (!S_ISREG(statBuffer.st_mode)) {
        (void)::close(fd);
        return common::Status::invalidArgument("auth token file must be a regular file");
    }
    if ((statBuffer.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        (void)::close(fd);
        return common::Status::invalidArgument(
            "auth token file permissions must not allow group or other access");
    }
    if (statBuffer.st_size <= 0 || statBuffer.st_size > 4096) {
        (void)::close(fd);
        return common::Status::invalidArgument("auth token file size must be between 1 and 4096 bytes");
    }

    std::string token;
    token.reserve(static_cast<std::size_t>(statBuffer.st_size));
    char chunk[512];
    while (true) {
        const ssize_t count = ::read(fd, chunk, sizeof(chunk));
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)::close(fd);
            return common::Status::invalidArgument("auth token file cannot be read");
        }
        token.append(chunk, static_cast<std::size_t>(count));
        if (token.size() > 4096) {
            (void)::close(fd);
            return common::Status::invalidArgument("auth token file exceeds 4096 bytes");
        }
    }
    (void)::close(fd);

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
