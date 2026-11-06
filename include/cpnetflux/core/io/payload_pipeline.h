#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <vector>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::io {

struct PreparedPayload {
    std::uint64_t offset = 0;
    std::vector<std::uint8_t> bytes;
    std::uint64_t chunkIndex = 0;
    std::uint32_t frameChecksum = 0;
    // Receiver writers can retain a checksum prefix or a decoded payload without copying it.
    std::size_t logicalOffset = 0;
    std::size_t logicalBytes = 0;
    bool checksumPresent = false;
};

// EmptyQueue batches only wakeups after backpressure; it never waits to fill a queue.
// It is intended for a continuously draining SPSC consumer.
enum class ProducerWakePolicy { AvailableCapacity, EmptyQueue };

class BoundedPayloadPipeline {
   public:
    BoundedPayloadPipeline(std::size_t maxBytes, std::size_t maxChunks, bool observeWaits = false,
                           ProducerWakePolicy wakePolicy = ProducerWakePolicy::AvailableCapacity);

    // Optional SPSC handback; the consumer must finish using every byte first.
    [[nodiscard]] common::Status push(PreparedPayload payload,
                                      std::vector<std::uint8_t>* reusableBuffer = nullptr);
    [[nodiscard]] common::Result<std::optional<PreparedPayload>> pop(
        std::vector<std::uint8_t> reusableBuffer = {});

    [[nodiscard]] double producerWaitSeconds() const;
    [[nodiscard]] double consumerWaitSeconds() const;
    void close();
    void cancel(common::Status reason);

    [[nodiscard]] std::size_t queuedBytes() const;
    [[nodiscard]] std::size_t highWatermarkBytes() const;
    [[nodiscard]] std::size_t recycledCapacityBytes() const;
    [[nodiscard]] std::uint64_t producerWaitCount() const;
    [[nodiscard]] std::uint64_t consumerWaitCount() const;
    [[nodiscard]] common::Status status() const;

   private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace cpnetflux::core::io
