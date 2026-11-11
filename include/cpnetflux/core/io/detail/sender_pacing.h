#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::io::detail {

// One sender lane owns this object. Limits application DATA admission, not wire packets.
class SenderPacer {
 public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::uint64_t kRate = 1500000000;
    explicit SenderPacer(std::uint64_t rate = 0) : rate_(rate) {}
    std::uint64_t frames = 0, wireBytes = 0, waits = 0, sleepCalls = 0;
    std::uint64_t waitNanoseconds = 0, requiredSpacingNanoseconds = 0, spacingViolations = 0;
    std::int64_t firstAdmissionNanoseconds = 0, lastAdmissionNanoseconds = 0;

    template<class Cancel, class Now, class Sleep>
    common::Status beforeSend(std::size_t bytes, Cancel cancel, Now now, Sleep sleep) {
        if (!rate_) return common::Status::ok();
        if (!bytes || bytes > 262212 || rate_ > kRate)
            return common::Status::invalidArgument("invalid sender pacing admission");
        auto status = cancel();
        if (!status.isOk()) return status;
        auto current = now();
        const auto began = current;
        const bool waiting = frames && current < next_;
        while (frames && current < next_) {
            sleep(std::min(next_ - current,
                std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(1))));
            ++sleepCalls;
            status = cancel();
            if (!status.isOk()) return status;
            current = now();
        }
        if (waiting) {
            ++waits;
            waitNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(current - began).count();
        }
        if (frames) {
            requiredSpacingNanoseconds += previousInterval_;
            if (current < next_) ++spacingViolations;
        }
        const auto at = std::chrono::duration_cast<std::chrono::nanoseconds>(current.time_since_epoch()).count();
        if (!frames) firstAdmissionNanoseconds = at;
        lastAdmissionNanoseconds = at;
        ++frames;
        wireBytes += bytes; // Attempts include a subsequent failed socket write.
        previousInterval_ = (bytes * 1000000000ULL + rate_ - 1) / rate_;
        // No credit/catch-up after a blocked socket, pause, or scheduling delay.
        next_ = current + std::chrono::nanoseconds(previousInterval_);
        return common::Status::ok();
    }
    template<class Cancel>
    common::Status beforeSend(std::size_t bytes, Cancel cancel) {
        return beforeSend(bytes, cancel, [] { return Clock::now(); },
            [](Clock::duration delay) { std::this_thread::sleep_for(delay); });
    }

 private:
    std::uint64_t rate_, previousInterval_ = 0;
    Clock::time_point next_{};
};

} // namespace cpnetflux::core::io::detail
