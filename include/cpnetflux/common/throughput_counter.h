#pragma once

#include <chrono>
#include <cstdint>

namespace cpnetflux::common {

class ThroughputCounter {
   public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void start(TimePoint now) noexcept;
    void stop(TimePoint now) noexcept;
    void addBytes(std::uint64_t bytes) noexcept;

    [[nodiscard]] std::uint64_t bytes() const noexcept;
    [[nodiscard]] double elapsedSeconds(TimePoint now) const noexcept;
    [[nodiscard]] double bytesPerSecond(TimePoint now) const noexcept;
    [[nodiscard]] double gigabitsPerSecond(TimePoint now) const noexcept;

   private:
    TimePoint start_{};
    TimePoint end_{};
    std::uint64_t bytes_ = 0;
    bool started_ = false;
    bool stopped_ = false;
};

}  // namespace cpnetflux::common
