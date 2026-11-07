#pragma once
#include <chrono>
#include <optional>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <deque>
#include <functional>
#include "cpnetflux/common/status.h"
namespace cpnetflux::core::io {
class StageGate {
    struct State;
 public:
    // Ready operations may reuse capacity before a sleeping waiter up to this
    // bound. Then the oldest waiter must run; permits never span queue waits.
    static constexpr std::uint32_t kReadyBypassLimit = 32;
    class Permit {
     public:
        Permit() = default;
        Permit(Permit&&) noexcept = default;
        Permit& operator=(Permit&&) noexcept;
        Permit(const Permit&) = delete;
        ~Permit();
     private:
        friend class StageGate;
        explicit Permit(std::shared_ptr<State> state) : state_(std::move(state)) {}
        void release();
        std::shared_ptr<State> state_;
    };
    explicit StageGate(std::uint32_t concurrency);
    common::Result<Permit> acquire(double* waitedSeconds = nullptr,
        std::optional<std::chrono::steady_clock::time_point> deadline = {});
 private:
    std::shared_ptr<State> state_;
};
// Jobs only borrow ready payload; run() does not return until that borrow ends.
class ReadyIoExecutor {
    struct State;
    std::unique_ptr<State> state_;
 public:
    ReadyIoExecutor(std::uint32_t concurrency, std::uint32_t capacity);
    ~ReadyIoExecutor();
    common::Status run(std::function<common::Status()> operation, double* queueWaitSeconds = nullptr,
        std::optional<std::chrono::steady_clock::time_point> deadline = {});
};
struct TransferStages {
    std::uint32_t readConcurrency = 0, writeConcurrency = 0, networkLanes = 0, depth = 0;
    std::uint64_t bufferBudgetBytes = 0, ownershipBoundBytes = 0;
    StageGate reads, writes;
    std::shared_ptr<ReadyIoExecutor> writerPool;
    // A lane owns at most one producer and writer. Caps at or above the lane
    // count need no admission lock; the requested policy is still reported.
    TransferStages(std::uint32_t read, std::uint32_t write, std::uint32_t lanes, bool usePool = false)
        : readConcurrency(read), writeConcurrency(write), networkLanes(lanes),
          reads(read && read < lanes ? read : 0), writes(write && write < lanes ? write : 0) {
            if (usePool && write && write < lanes) writerPool = std::make_shared<ReadyIoExecutor>(write,lanes);
          }
    bool readAdmissionRequired() const { return readConcurrency && readConcurrency < networkLanes; }
    bool writeAdmissionRequired() const { return writeConcurrency && writeConcurrency < networkLanes; }
    static common::Result<std::shared_ptr<TransferStages>> create(std::uint32_t lanes, std::uint32_t depth, bool receiver = false);
};
}
