#include "gridflux/config/sink_options.h"

#include <charconv>
#include <limits>
#include <string_view>

namespace gridflux::config {
namespace {

constexpr std::uint32_t kMaxConnections = 64;
constexpr std::uint32_t kMaxBufferSize = 16 * 1024 * 1024;

common::Result<std::uint64_t> parseUnsigned(std::string_view value, std::string_view name) {
    if (value.empty()) {
        return common::Status::invalidArgument(std::string(name) + " must not be empty");
    }

    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end) {
        return common::Status::invalidArgument(std::string(name) + " must be a decimal integer");
    }

    return parsed;
}

common::Status requireValue(int argc, int index, std::string_view option) {
    if (index + 1 >= argc) {
        return common::Status::invalidArgument(std::string(option) + " requires a value");
    }

    return common::Status::ok();
}

}  // namespace

common::Result<SinkOptions> parseSinkOptions(int argc, const char* const* argv, SinkRole role) {
    SinkOptions options;
    options.host = role == SinkRole::Server ? "0.0.0.0" : "127.0.0.1";

    bool hasBytes = false;
    for (int index = 1; index < argc; index += 2) {
        const std::string_view option(argv[index]);
        const common::Status valueStatus = requireValue(argc, index, option);
        if (!valueStatus.isOk()) {
            return valueStatus;
        }

        const std::string_view value(argv[index + 1]);
        if (option == "--host") {
            if (value.empty()) {
                return common::Status::invalidArgument("--host must not be empty");
            }
            options.host = std::string(value);
            continue;
        }

        if (option == "--port") {
            auto parsed = parseUnsigned(value, "--port");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > std::numeric_limits<std::uint16_t>::max()) {
                return common::Status::invalidArgument("--port must be in range 1..65535");
            }
            options.port = static_cast<std::uint16_t>(parsed.value());
            continue;
        }

        if (option == "--connections") {
            auto parsed = parseUnsigned(value, "--connections");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxConnections) {
                return common::Status::invalidArgument("--connections must be in range 1..64");
            }
            options.connections = static_cast<std::uint32_t>(parsed.value());
            continue;
        }

        if (option == "--bytes") {
            auto parsed = parseUnsigned(value, "--bytes");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0) {
                return common::Status::invalidArgument("--bytes must be greater than 0");
            }
            options.bytes = parsed.value();
            hasBytes = true;
            continue;
        }

        if (option == "--buffer-size") {
            auto parsed = parseUnsigned(value, "--buffer-size");
            if (!parsed.isOk()) {
                return parsed.status();
            }
            if (parsed.value() == 0 || parsed.value() > kMaxBufferSize) {
                return common::Status::invalidArgument(
                    "--buffer-size must be in range 1..16777216");
            }
            options.bufferSize = static_cast<std::uint32_t>(parsed.value());
            continue;
        }

        return common::Status::invalidArgument("unknown option: " + std::string(option));
    }

    if (!hasBytes) {
        return common::Status::invalidArgument("--bytes is required");
    }

    return options;
}

std::string sinkUsage(const char* programName, SinkRole role) {
    const char* hostText = role == SinkRole::Server ? "<bind-ip>" : "<server-ip>";
    return std::string("Usage: ") + programName + " --host " + hostText +
           " --port <port> --connections <N> --bytes <total-bytes> --buffer-size <bytes>";
}

}  // namespace gridflux::config
