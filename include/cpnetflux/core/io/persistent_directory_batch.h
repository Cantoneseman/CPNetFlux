#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include "cpnetflux/core/io/dynamic_file_queue.h"

namespace cpnetflux::core::io {
// One authenticated directory batch; channel sockets remain owned by their connection threads.
class PersistentDirectoryBatch {
 public:
    PersistentDirectoryBatch(std::uint32_t total, std::uint32_t channels);
    ~PersistentDirectoryBatch();
    [[nodiscard]] common::Status attach(std::uint32_t channel);
    void bind(std::uint32_t channel, int controlFd, int dataFd, bool ready = true);
    void unbind(std::uint32_t channel);
    [[nodiscard]] common::Status waitReady(std::chrono::milliseconds timeout);
    [[nodiscard]] common::Status fileEvent(std::uint32_t channel, const PersistentFileIdentity& id,
                                          const common::Status& status, bool complete);
    [[nodiscard]] common::Status finish(std::uint32_t channel, std::chrono::milliseconds timeout);
    void cancel(const common::Status& status);
 private:
    struct Channel { bool attached = false, ready = false, done = false; int control = -1, data = -1; };
    struct File { PersistentFileIdentity id; std::uint32_t owner; bool done = false; };
    common::Status wait(bool finishing, std::chrono::milliseconds timeout);
    void failLocked(const common::Status& status);
    void checkControlsLocked();
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread monitor_;
    bool stopped_ = false, finished_ = false;
    std::vector<Channel> channels_;
    std::unordered_map<std::uint32_t, File> files_;
    std::unordered_set<std::string> paths_, transfers_;
    std::uint32_t total_, completed_ = 0;
    common::Status status_ = common::Status::ok();
};
}  // namespace cpnetflux::core::io
