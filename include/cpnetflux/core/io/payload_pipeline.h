#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/io/parallel_compression_executor.h"

namespace cpnetflux::core::io {

struct PreparedPayload {
    std::uint64_t offset = 0;
    std::vector<std::uint8_t> bytes;
    std::uint64_t chunkIndex = 0;
    std::uint32_t frameChecksum = 0;
    std::optional<ParallelCompressionExecutor::Task> compressionTask;
    std::size_t logicalBytes = 0;

    [[nodiscard]] std::size_t byteSize() const noexcept {
        return compressionTask ? logicalBytes : bytes.size();
    }
};

class BoundedPayloadPipeline {
   public:
    BoundedPayloadPipeline(std::size_t maxBytes, std::size_t maxChunks);

    [[nodiscard]] common::Status push(PreparedPayload payload);
    [[nodiscard]] common::Result<std::optional<PreparedPayload>> pop();

    void close();
    void cancel(common::Status reason);

    [[nodiscard]] std::size_t queuedBytes() const;
    [[nodiscard]] std::size_t highWatermarkBytes() const;
    [[nodiscard]] std::uint64_t producerWaitCount() const;
    [[nodiscard]] std::uint64_t consumerWaitCount() const;
    [[nodiscard]] common::Status status() const;

   private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace cpnetflux::core::io
