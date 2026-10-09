#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <string_view>
#include <string>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/tls_socket.h"

namespace cpnetflux::core::io::detail {

using ControlReadDeadline = std::optional<std::chrono::steady_clock::time_point>;

struct TreePipelineTerminalReply {
    int code = 0;
    std::string transferId;
    std::string text;
};

[[nodiscard]] std::optional<int> parseControlReplyCode(std::string_view line) noexcept;
[[nodiscard]] std::optional<TreePipelineTerminalReply> parseTreePipelineTerminalReply(
    int code, std::string_view text);
[[nodiscard]] common::Status validatePipelineEnableReply(int code);

[[nodiscard]] common::Result<std::string> readTreePipelineControlLine(
    TlsConnection* control, std::string* buffer, const std::atomic<bool>* cancelled,
    ControlReadDeadline deadline);

}  // namespace cpnetflux::core::io::detail
