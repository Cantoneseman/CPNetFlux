#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include "cpnetflux/core/io/persistent_data_session.h"

namespace cpnetflux::core::io {
[[nodiscard]] bool samePersistentIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept;
[[nodiscard]] bool samePersistentRangeIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept;
struct DynamicFileLease {
    PersistentFileIdentity file;
    double queueWaitSeconds = 0;
};
// Owns scan metadata only. The ready ring and active credits have separate hard bounds.
class DynamicFileQueue {
 public:
    DynamicFileQueue(std::vector<PersistentFileIdentity> files, std::uint32_t channels,
                     std::uint32_t window, std::size_t capacity);
    [[nodiscard]] common::Result<std::optional<DynamicFileLease>> claim(std::uint32_t channel);
    [[nodiscard]] common::Status complete(std::uint32_t channel, const PersistentFileIdentity& file);
    void cancel(const common::Status& reason);
    [[nodiscard]] std::size_t highWatermark() const;
    [[nodiscard]] std::size_t outstanding() const;
 private:
    struct Ready { PersistentFileIdentity file; std::chrono::steady_clock::time_point admitted; };
    struct Active { PersistentFileIdentity file; std::uint32_t channel; };
    void fill();
    mutable std::mutex mutex_;
    std::vector<PersistentFileIdentity> files_;
    std::vector<std::optional<Ready>> ring_;
    std::vector<std::uint32_t> credits_;
    std::unordered_map<std::string, Active> active_;
    std::uint32_t window_;
    std::size_t next_ = 0, head_ = 0, tail_ = 0, size_ = 0, high_ = 0;
    common::Status status_ = common::Status::ok();
};
}  // namespace cpnetflux::core::io
