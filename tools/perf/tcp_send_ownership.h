#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cpnetflux::diagnostic {

// Diagnostic-only state machine. No production socket behavior is changed.
class SendOwnership {
 public:
    static constexpr std::size_t kSlots = 32, kCalls = 4096;
    explicit SendOwnership(std::uint32_t first = 0) : next_(first) {}
    bool begin(std::size_t slot, std::size_t bytes) {
        if (!available(slot) || !bytes) return false;
        slots_[slot] = {bytes, 0, true};
        return true;
    }
    bool accepted(std::size_t slot, std::size_t bytes) {
        if (slot >= kSlots || !slots_[slot].active || !bytes ||
            bytes > slots_[slot].remaining || !canSend()) return false;
        auto& state = slots_[slot];
        calls_[next_ % kCalls] = {next_, slot, bytes, true};
        ++next_; ++pending_; ++state.references; ++successfulCalls;
        state.remaining -= bytes;
        heldBytes += bytes;
        if (heldBytes > peakHeldBytes) peakHeldBytes = heldBytes;
        return true;
    }
    bool complete(std::uint32_t first, std::uint32_t last, bool copied) {
        const auto span = std::uint32_t(last-first);
        if (span >= kCalls || std::uint64_t(span)+1 > pending_) return false;
        // Validate the entire inclusive, potentially wrapping range before release.
        for (std::uint32_t i=0;i<=span;++i) {
            const auto id=std::uint32_t(first+i);
            const auto& call=calls_[id % kCalls];
            if (!call.live || call.id != id) return false;
        }
        for (std::uint32_t i=0;i<=span;++i) {
            auto& call=calls_[std::uint32_t(first+i) % kCalls];
            auto& slot=slots_[call.slot];
            --slot.references; --pending_; heldBytes-=call.bytes;
            if (copied) { ++copiedCalls; copiedBytes+=call.bytes; }
            else { ++unmarkedCalls; unmarkedBytes+=call.bytes; }
            if (!slot.references && !slot.remaining) slot.active=false;
            call.live=false;
        }
        return true;
    }
    bool available(std::size_t slot) const { return slot < kSlots && !slots_[slot].active; }
    bool canSend() const { return !calls_[next_ % kCalls].live; }
    std::size_t pending() const { return pending_; }
    std::uint32_t next() const { return next_; }
    std::uint64_t heldBytes = 0, peakHeldBytes = 0, copiedCalls = 0, copiedBytes = 0;
    std::uint64_t unmarkedCalls = 0, unmarkedBytes = 0, successfulCalls = 0;
 private:
    struct Slot { std::size_t remaining=0, references=0; bool active=false; };
    struct Call { std::uint32_t id=0; std::size_t slot=0, bytes=0; bool live=false; };
    std::array<Slot,kSlots> slots_{};
    std::array<Call,kCalls> calls_{};
    std::size_t pending_=0;
    std::uint32_t next_;
};
}  // namespace cpnetflux::diagnostic
