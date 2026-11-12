#pragma once

#include <cstdio>
#include <mutex>
#include <string_view>

namespace cpnetflux::core::io::detail {
// Keep structured records whole even when stdout and stderr share one log file.
inline void emitDiagnosticRecord(std::FILE* stream, std::string_view record) noexcept {
    static std::mutex outputMutex;
    try {
        std::lock_guard lock(outputMutex);
        (void)std::fwrite(record.data(), 1, record.size(), stream);
        (void)std::fflush(stream);
    } catch (...) {
        // A missing/partial diagnostic is rejected by evidence gates, not transfer semantics.
    }
}
}  // namespace cpnetflux::core::io::detail
