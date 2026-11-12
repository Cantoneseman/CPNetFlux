#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cerrno>
#include <type_traits>
#include <sys/timerfd.h>
#include <unistd.h>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/socket_utils.h"

namespace cpnetflux::core::io::detail {

// One sender lane owns this object. Limits application DATA admission, not wire packets.
class SenderPacer {
 public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::uint64_t kRate = 1500000000;
    explicit SenderPacer(std::uint64_t rate = 0) : rate_(rate) {}
    std::uint64_t frames = 0, wireBytes = 0, waits = 0, sleepCalls = 0;
    std::uint64_t waitNanoseconds = 0, requiredSpacingNanoseconds = 0, spacingViolations = 0;
    std::uint64_t timerFdCreates = 0, timerArmCalls = 0, timerReadCalls = 0;
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
            const auto delay = std::min(next_ - current,
                std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(1)));
            ++sleepCalls;
            if constexpr (std::is_void_v<std::invoke_result_t<Sleep, Clock::duration>>) {
                sleep(delay);
            } else {
                status = sleep(delay);
                if (!status.isOk()) return status;
            }
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
            [&](Clock::duration delay) { return waitTimer(delay, cancel); });
    }

 private:
    template<class Cancel>
    common::Status waitTimer(Clock::duration delay, Cancel cancel) {
        // One lazy, blocking timer per lane. No task/thread timer-slack changes.
        if (!timer_.isValid()) {
            const int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
            if (fd < 0) return common::Status::systemError("sender timerfd_create failed", errno);
            timer_.reset(fd);
            ++timerFdCreates;
        }
        const auto ns = std::max<std::int64_t>(1,
            std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count());
        itimerspec timer{};
        timer.it_value.tv_sec = ns / 1000000000;
        timer.it_value.tv_nsec = ns % 1000000000;
        ++timerArmCalls;
        if (::timerfd_settime(timer_.get(), 0, &timer, nullptr) != 0)
            return common::Status::systemError("sender timerfd_settime failed", errno);
        for (;;) {
            std::uint64_t expirations = 0;
            ++timerReadCalls;
            const auto count = ::read(timer_.get(), &expirations, sizeof(expirations));
            if (count < 0) {
                const int error = errno;
                if (error != EINTR)
                    return common::Status::systemError("sender timerfd read failed", error);
                const auto status = cancel();
                if (!status.isOk()) return status;
                continue; // Still wait for the same armed deadline.
            }
            if (count != sizeof(expirations) || !expirations)
                return common::Status::systemError("sender timerfd short expiration read", EIO);
            return common::Status::ok();
        }
    }
    UniqueFd timer_;
    std::uint64_t rate_, previousInterval_ = 0;
    Clock::time_point next_{};
};

} // namespace cpnetflux::core::io::detail
