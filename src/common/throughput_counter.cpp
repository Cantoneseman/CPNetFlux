#include "cpnetflux/common/throughput_counter.h"

namespace cpnetflux::common {

void ThroughputCounter::start(TimePoint now) noexcept {
    start_ = now;
    end_ = now;
    bytes_ = 0;
    started_ = true;
    stopped_ = false;
}

void ThroughputCounter::stop(TimePoint now) noexcept {
    end_ = now;
    stopped_ = true;
}

void ThroughputCounter::addBytes(std::uint64_t bytes) noexcept { bytes_ += bytes; }

std::uint64_t ThroughputCounter::bytes() const noexcept { return bytes_; }

double ThroughputCounter::elapsedSeconds(TimePoint now) const noexcept {
    if (!started_) {
        return 0.0;
    }

    const TimePoint effectiveEnd = stopped_ ? end_ : now;
    const auto elapsed = std::chrono::duration<double>(effectiveEnd - start_);
    return elapsed.count();
}

double ThroughputCounter::bytesPerSecond(TimePoint now) const noexcept {
    const double seconds = elapsedSeconds(now);
    if (seconds <= 0.0) {
        return 0.0;
    }

    return static_cast<double>(bytes_) / seconds;
}

double ThroughputCounter::gigabitsPerSecond(TimePoint now) const noexcept {
    return bytesPerSecond(now) * 8.0 / 1'000'000'000.0;
}

}  // namespace cpnetflux::common
