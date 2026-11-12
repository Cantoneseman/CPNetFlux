#pragma once

#include "cpnetflux/core/io/socket_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace cpnetflux::core::io::detail {

inline bool rdmaCompletionDeadlineExpired(
    SocketDeadline deadline, std::chrono::steady_clock::time_point now) noexcept {
    return deadline && now >= *deadline;
}

inline int rdmaCompletionEventPollTimeoutMs(
    SocketDeadline deadline, std::chrono::steady_clock::time_point now) noexcept {
    constexpr int kPollSliceMs = 1000;
    if (!deadline) return kPollSliceMs;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        *deadline - now);
    if (remaining.count() <= 0) return 0;
    return static_cast<int>(std::min<std::int64_t>(remaining.count(), kPollSliceMs));
}

}  // namespace cpnetflux::core::io::detail
