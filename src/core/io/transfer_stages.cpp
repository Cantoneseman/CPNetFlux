#include "cpnetflux/core/io/transfer_stages.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string_view>
#include <future>
#include <thread>
#include <vector>
#include <stdexcept>
namespace cpnetflux::core::io {
struct StageGate::State {
    explicit State(std::uint32_t n) : limit(n) {}
    std::mutex mutex;
    struct Waiter { std::condition_variable changed; };
    std::deque<Waiter*> waiting;
    std::uint32_t readyBypasses = 0;
    std::uint32_t limit, active = 0;
};
StageGate::StageGate(std::uint32_t n) : state_(std::make_shared<State>(n)) {}
void StageGate::Permit::release() {
    if (!state_) return;
    {
        std::lock_guard lock(state_->mutex);
        --state_->active;
        // Notify under the lock: the waiter owns this condition variable on its stack.
        if (!state_->waiting.empty()) state_->waiting.front()->changed.notify_one();
    }
    state_.reset();
}
StageGate::Permit::~Permit() { release(); }
StageGate::Permit& StageGate::Permit::operator=(Permit&& other) noexcept {
    if (this != &other) { release(); state_ = std::move(other.state_); }
    return *this;
}
common::Result<StageGate::Permit> StageGate::acquire(double* waited,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    if (deadline && std::chrono::steady_clock::now() >= *deadline)
        return common::Status::runtimeError("IO stage admission deadline exceeded");
    if (state_->limit == 0) return Permit{};
    auto state=state_;
    std::unique_lock lock(state->mutex);
    if (state->active < state->limit &&
        (state->waiting.empty() || state->readyBypasses < kReadyBypassLimit)) {
        if (!state->waiting.empty()) ++state->readyBypasses;
        ++state->active; return Permit{state};
    }
    // Only callers with ready IO wait here, never a worker waiting for network or payload.
    const auto started=std::chrono::steady_clock::now();
    State::Waiter ticket;
    if (state->waiting.empty()) state->readyBypasses = 0;
    state->waiting.push_back(&ticket);
    const auto ready=[&] { return state->active < state->limit && state->waiting.front()==&ticket; };
    const bool admitted = deadline ? ticket.changed.wait_until(lock,*deadline,ready)
                                  : (ticket.changed.wait(lock,ready),true);
    if (waited) *waited += std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    if (!admitted) {
        state->waiting.erase(std::find(state->waiting.begin(),state->waiting.end(),&ticket));
        if (state->active < state->limit && !state->waiting.empty())
            state->waiting.front()->changed.notify_one();
        return common::Status::runtimeError("IO stage admission deadline exceeded");
    }
    state->waiting.pop_front(); state->readyBypasses = 0; ++state->active;
    if (state->active < state->limit && !state->waiting.empty())
        state->waiting.front()->changed.notify_one();
    return Permit{state};
}
struct ReadyIoExecutor::State {
    struct Job {
        std::function<common::Status()> operation;
        std::promise<common::Status> done;
        std::chrono::steady_clock::time_point queued;
        std::optional<std::chrono::steady_clock::time_point> deadline;
        double* wait = nullptr;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<Job>> jobs;
    std::vector<std::thread> workers;
    std::uint32_t capacity, outstanding = 0;
    bool stopping = false;
    State(std::uint32_t concurrency, std::uint32_t bound) : capacity(bound) {
        try {
            while (concurrency--) workers.emplace_back([this] {
                for (;;) {
                    std::shared_ptr<Job> job;
                    {
                        std::unique_lock lock(mutex);
                        changed.wait(lock,[&] { return stopping || !jobs.empty(); });
                        if (jobs.empty()) return;
                        job=std::move(jobs.front()); jobs.pop_front();
                    }
                    const auto start=std::chrono::steady_clock::now();
                    if (job->wait) *job->wait += std::chrono::duration<double>(start-job->queued).count();
                    auto status=common::Status::runtimeError("ready IO job deadline exceeded");
                    if (!job->deadline || start < *job->deadline) {
                        try { status=job->operation(); }
                        catch (...) { status=common::Status::runtimeError("ready IO job threw an exception"); }
                    }
                    job->operation = {}; // destroy borrowed captures before run() can return
                    // Release capacity before completing the promise, so a caller
                    // can immediately submit again without a false capacity error.
                    { std::lock_guard lock(mutex); --outstanding; }
                    job->done.set_value(std::move(status));
                }
            });
        } catch (...) {
            { std::lock_guard lock(mutex); stopping=true; }
            changed.notify_all(); for (auto& worker:workers) worker.join(); throw;
        }
    }
    ~State() {
        { std::lock_guard lock(mutex); stopping=true; }
        changed.notify_all(); for (auto& worker:workers) worker.join();
    }
};
ReadyIoExecutor::ReadyIoExecutor(std::uint32_t concurrency,std::uint32_t capacity)
    : state_(nullptr) {
    if (!concurrency || concurrency>64 || capacity<concurrency || capacity>64)
        throw std::invalid_argument("ready IO executor requires 1..64 workers and workers..64 capacity");
    state_=std::make_unique<State>(concurrency,capacity);
}
ReadyIoExecutor::~ReadyIoExecutor() = default;
common::Status ReadyIoExecutor::run(std::function<common::Status()> operation,double* wait,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    if (deadline && std::chrono::steady_clock::now() >= *deadline)
        return common::Status::runtimeError("ready IO job deadline exceeded");
    auto job=std::make_shared<State::Job>();
    job->operation=std::move(operation); job->wait=wait; job->deadline=deadline;
    job->queued=std::chrono::steady_clock::now(); auto done=job->done.get_future();
    {
        std::lock_guard lock(state_->mutex);
        if (state_->stopping) return common::Status::runtimeError("ready IO executor stopped");
        if (state_->outstanding >= state_->capacity)
            return common::Status::invalidArgument("ready IO outstanding capacity exceeded");
        state_->jobs.push_back(std::move(job)); ++state_->outstanding;
    }
    state_->changed.notify_one();
    return done.get();
}

namespace {
common::Result<std::uint64_t> setting(const char* key, std::uint64_t fallback, std::uint64_t max) {
    const char* value=std::getenv(key); if (!value) return fallback;
    const std::string_view text(value); std::uint64_t parsed=0;
    auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),parsed);
    if (text.empty() || error!=std::errc{} || end!=text.data()+text.size() || parsed>max)
        return common::Status::invalidArgument(std::string(key)+" is outside its bounded unsigned range");
    return parsed;
}
}
common::Status TransferStages::preflight(std::uint32_t lanes, std::uint32_t frameCapacity) {
    auto depth=setting("CPNETFLUX_PAYLOAD_PIPELINE_DEPTH",2,64);
    if (!depth.isOk()) return depth.status();
    auto created=create(lanes,depth.value(),false,frameCapacity);
    return created.isOk() ? common::Status::ok() : created.status();
}
common::Result<std::shared_ptr<TransferStages>> TransferStages::create(std::uint32_t lanes,std::uint32_t depth,bool receiver,std::uint32_t frameCapacity) {
    if (frameCapacity != 65536 && frameCapacity != 262148)
        return common::Status::invalidArgument("unsupported stage frame capacity");
    if (!lanes || lanes>64 || !depth || depth>64)
        return common::Status::invalidArgument("stage lanes and depth must be 1..64");
    auto read=setting("CPNETFLUX_STAGE_READ_CONCURRENCY",0,64);
    auto write=setting("CPNETFLUX_STAGE_WRITE_CONCURRENCY",0,64);
    auto budget=setting("CPNETFLUX_STAGE_BUFFER_BYTES",64ULL*1024*1024,256ULL*1024*1024);
    if (!read.isOk()) return read.status(); if (!write.isOk()) return write.status(); if (!budget.isOk()) return budget.status();
    if (frameCapacity > 65536 && budget.value() > 64ULL*1024*1024)
        return common::Status::invalidArgument("large DATA payload budget must not exceed 64 MiB");
    // Queue + producer/consumer + recycled/decoded/compressed owners. Excludes
    // transport pools, metadata/statistics and tmpfs, which are measured separately.
    const std::uint64_t bound=std::uint64_t(lanes)*(depth+6)*frameCapacity;
    if (budget.value()<bound) return common::Status::invalidArgument("stage payload ownership exceeds total buffer budget");
    const char* selected=std::getenv("CPNETFLUX_STAGE_WRITE_EXECUTOR");
    const std::string_view executor=selected ? selected : "pool";
    if (executor != "pool" && executor != "admission")
        return common::Status::invalidArgument("CPNETFLUX_STAGE_WRITE_EXECUTOR must be pool or admission");
    auto stages=std::make_shared<TransferStages>(read.value(),write.value(),lanes,receiver && executor=="pool");
    stages->networkLanes=lanes; stages->depth=depth; stages->bufferBudgetBytes=budget.value(); stages->ownershipBoundBytes=bound;
    stages->frameCapacityBytes=frameCapacity;
    return stages;
}
}
