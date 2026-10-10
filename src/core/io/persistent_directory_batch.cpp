#include "cpnetflux/core/io/persistent_directory_batch.h"
#include <algorithm>
#include <poll.h>
#include <sys/socket.h>
#include "cpnetflux/core/tree/tree_scan.h"
namespace cpnetflux::core::io {
PersistentDirectoryBatch::PersistentDirectoryBatch(std::uint32_t total, std::uint32_t channels)
    : total_(total) {
    if (!channels || channels > 8) status_ = common::Status::invalidArgument("invalid batch channels");
    else {
        channels_.resize(channels);
        monitor_ = std::thread([this] {
            std::unique_lock lock(mutex_);
            while (!stopped_ && status_.isOk() && !finished_) {
                checkControlsLocked();
                changed_.wait_for(lock,std::chrono::milliseconds(50));
            }
        });
    }
}
PersistentDirectoryBatch::~PersistentDirectoryBatch() {
    { std::lock_guard lock(mutex_); stopped_ = true; changed_.notify_all(); }
    if (monitor_.joinable()) monitor_.join();
}
common::Status PersistentDirectoryBatch::attach(std::uint32_t channel) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (channel >= channels_.size() || channels_[channel].attached) {
        auto error = common::Status::invalidArgument("duplicate or invalid batch channel");
        failLocked(error); return error;
    }
    channels_[channel].attached = true;
    return common::Status::ok();
}
void PersistentDirectoryBatch::bind(std::uint32_t channel, int controlFd, int dataFd, bool ready) {
    std::lock_guard lock(mutex_);
    channels_.at(channel).control = controlFd;
    channels_.at(channel).data = dataFd;
    channels_.at(channel).ready = ready;
    if (!status_.isOk()) {
        if (controlFd >= 0) (void)::shutdown(controlFd, SHUT_RDWR);
        if (dataFd >= 0) (void)::shutdown(dataFd, SHUT_RDWR);
    }
    changed_.notify_all();
}
void PersistentDirectoryBatch::unbind(std::uint32_t channel) {
    std::lock_guard lock(mutex_);
    channels_.at(channel).control = channels_.at(channel).data = -1;
}
void PersistentDirectoryBatch::failLocked(const common::Status& status) {
    if (status_.isOk()) status_ = status.isOk() ? common::Status::runtimeError("directory batch cancelled") : status;
    for (auto& c : channels_) {
        if (c.control >= 0) (void)::shutdown(c.control, SHUT_RDWR);
        if (c.data >= 0) (void)::shutdown(c.data, SHUT_RDWR);
    }
    changed_.notify_all();
}
void PersistentDirectoryBatch::cancel(const common::Status& status) {
    std::lock_guard lock(mutex_); failLocked(status);
}
void PersistentDirectoryBatch::checkControlsLocked() {
    if (finished_) return;
    // No pipelined commands are allowed while a directory batch owns these controls.
    for (const auto& c : channels_) if (c.control >= 0) {
        pollfd descriptor{c.control,POLLIN,0};
        if (::poll(&descriptor,1,0) > 0 && descriptor.revents != 0) {
            failLocked(common::Status::runtimeError("dynamic batch control disconnected/cancelled"));
            return;
        }
    }
}
common::Status PersistentDirectoryBatch::wait(bool finishing, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (!status_.isOk()) return status_;
        bool all = std::all_of(channels_.begin(), channels_.end(), [&](const auto& c) {
            return finishing ? c.done : c.ready;
        });
        if (all) {
            if (finishing && (completed_ != total_ || files_.size() != total_))
                failLocked(common::Status::invalidArgument("dynamic batch file count mismatch"));
            if (finishing && status_.isOk()) { finished_ = true; changed_.notify_all(); }
            return status_;
        }
        checkControlsLocked();
        if (!status_.isOk()) return status_;
        if (std::chrono::steady_clock::now() >= deadline) {
            failLocked(common::Status::runtimeError("dynamic batch channel timeout"));
            return status_;
        }
        changed_.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
    }
}
common::Status PersistentDirectoryBatch::waitReady(std::chrono::milliseconds timeout) { return wait(false, timeout); }
common::Status PersistentDirectoryBatch::fileEvent(std::uint32_t channel,
    const PersistentFileIdentity& id, const common::Status& status, bool complete) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (channel >= channels_.size() || !channels_[channel].ready || channels_[channel].done ||
        !id.fileId || id.fileId > total_ || !id.generation || id.transferId.empty() || !id.chunkSize ||
        !tree::validateTreeRelativePath(id.relativePath).isOk())
        return common::Status::invalidArgument("invalid dynamic batch file identity");
    auto found = files_.find(id.fileId);
    if (!complete) {
        if (found != files_.end() || paths_.contains(id.relativePath) || transfers_.contains(id.transferId))
            return common::Status::invalidArgument("duplicate dynamic batch file identity/path");
        paths_.insert(id.relativePath); transfers_.insert(id.transferId);
        files_.emplace(id.fileId, File{id,channel,false});
    } else {
        if (found == files_.end() || found->second.owner != channel || found->second.done ||
            !samePersistentIdentity(found->second.id,id))
            return common::Status::invalidArgument("stale or foreign dynamic file completion");
        if (!status.isOk()) { failLocked(status); return status; }
        found->second.done = true;
        ++completed_;
    }
    return common::Status::ok();
}
common::Status PersistentDirectoryBatch::finish(std::uint32_t channel, std::chrono::milliseconds timeout) {
    {
        std::lock_guard lock(mutex_);
        if (!status_.isOk()) return status_;
        if (channel >= channels_.size() || !channels_[channel].ready || channels_[channel].done)
            return common::Status::invalidArgument("invalid/duplicate batch finish");
        channels_[channel].done = true;
        changed_.notify_all();
    }
    return wait(true, timeout);
}
}  // namespace cpnetflux::core::io
