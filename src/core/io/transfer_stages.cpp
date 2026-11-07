#include "cpnetflux/core/io/transfer_stages.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string_view>
namespace cpnetflux::core::io {
struct StageGate::State {
    explicit State(std::uint32_t n) : limit(n) {}
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<const void*> waiting;
    std::uint32_t limit, active = 0;
};
StageGate::StageGate(std::uint32_t n) : state_(std::make_shared<State>(n)) {}
void StageGate::Permit::release() {
    if (!state_) return;
    { std::lock_guard lock(state_->mutex); --state_->active; }
    state_->changed.notify_all(); state_.reset();
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
    if (state->active < state->limit && state->waiting.empty()) {
        ++state->active; return Permit{state};
    }
    // Only callers with ready IO wait here, never a worker waiting for network or payload.
    const auto started=std::chrono::steady_clock::now();
    char ticket;
    state->waiting.push_back(&ticket);
    const auto ready=[&] { return state->active < state->limit && state->waiting.front()==&ticket; };
    const bool admitted = deadline ? state->changed.wait_until(lock,*deadline,ready)
                                  : (state->changed.wait(lock,ready),true);
    if (waited) *waited += std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    if (!admitted) {
        state->waiting.erase(std::find(state->waiting.begin(),state->waiting.end(),&ticket));
        lock.unlock(); state->changed.notify_all();
        return common::Status::runtimeError("IO stage admission deadline exceeded");
    }
    state->waiting.pop_front(); ++state->active;
    lock.unlock(); state->changed.notify_all();
    return Permit{state};
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
common::Result<std::shared_ptr<TransferStages>> TransferStages::create(std::uint32_t lanes,std::uint32_t depth) {
    if (!lanes || lanes>64 || !depth || depth>64)
        return common::Status::invalidArgument("stage lanes and depth must be 1..64");
    auto read=setting("CPNETFLUX_STAGE_READ_CONCURRENCY",0,64);
    auto write=setting("CPNETFLUX_STAGE_WRITE_CONCURRENCY",0,64);
    auto budget=setting("CPNETFLUX_STAGE_BUFFER_BYTES",64ULL*1024*1024,256ULL*1024*1024);
    if (!read.isOk()) return read.status(); if (!write.isOk()) return write.status(); if (!budget.isOk()) return budget.status();
    // Queue + producer/consumer + recycled/decoded/compressed owners. Excludes
    // transport pools, metadata/statistics and tmpfs, which are measured separately.
    const std::uint64_t bound=std::uint64_t(lanes)*(depth+6)*65536;
    if (budget.value()<bound) return common::Status::invalidArgument("stage payload ownership exceeds total buffer budget");
    auto stages=std::make_shared<TransferStages>(read.value(),write.value(),lanes);
    stages->networkLanes=lanes; stages->depth=depth; stages->bufferBudgetBytes=budget.value(); stages->ownershipBoundBytes=bound;
    return stages;
}
}
