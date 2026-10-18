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
    if (channel >= channels_.size() || channels_[channel].attached || finished_) {
        auto error = common::Status::invalidArgument("duplicate or invalid batch channel");
        failLocked(error); return error;
    }
    channels_[channel].attached = true;
    channels_[channel].ready = false;
    channels_[channel].done = false;
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
    auto& state = channels_.at(channel);
    state.control = state.data = -1;
    state.ready = false;
    state.done = false;
    state.attached = false;
    changed_.notify_all();
}
void PersistentDirectoryBatch::failChannelLocked(std::uint32_t channel, const common::Status& status) {
    if (channel >= channels_.size()) {
        failLocked(common::Status::invalidArgument("invalid batch channel"));
        return;
    }
    auto& state = channels_[channel];
    const int controlFd = state.control;
    const int dataFd = state.data;
    state.ready = false;
    state.done = false;
    state.control = state.data = -1;
    // A control-side disconnect must interrupt the paired data accept/worker.
    // A data-side failure keeps the control session alive for the lane retry.
    if (status.message().find("control") != std::string::npos && dataFd >= 0)
        (void)::shutdown(dataFd, SHUT_RDWR);
    (void)controlFd;
    for (auto& [fileId, file] : files_) {
        (void)fileId;
        for (auto lease = file.leases.begin(); lease != file.leases.end();) {
            if (lease->second.owner == channel) lease = file.leases.erase(lease);
            else ++lease;
        }
    }
    changed_.notify_all();
    (void)status;
}
common::Status PersistentDirectoryBatch::failChannel(
    std::uint32_t channel, const common::Status& status) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (channel >= channels_.size()) return common::Status::invalidArgument("invalid batch channel");
    failChannelLocked(channel, status);
    return common::Status::ok();
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
    for (std::uint32_t index = 0; index < channels_.size(); ++index) {
        const auto& c = channels_[index];
        if (c.control >= 0) {
            pollfd descriptor{c.control,POLLIN,0};
            if (::poll(&descriptor,1,0) > 0 && descriptor.revents != 0) {
                failChannelLocked(index, common::Status::transportError(
                    "dynamic batch channel control disconnected/cancelled"));
            }
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
            failLocked(common::Status::transportError("dynamic batch channel timeout"));
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
        !id.fileId || id.fileId > total_ || !id.generation || id.transferId.empty() ||
        !tree::validateTreeRelativePath(id.relativePath).isOk() ||
        (id.rangeId != 0 && (id.rangeCount == 0 || id.rangeId > id.rangeCount || id.rangeLength == 0)))
        return common::Status::invalidArgument("invalid dynamic batch file identity");
    auto found = files_.find(id.fileId);
    auto base = id;
    base.rangeId = base.rangeOffset = base.rangeLength = base.rangeCount = base.attempt = 0;
    if (!complete) {
        if (found == files_.end()) {
            if (paths_.contains(id.relativePath) || transfers_.contains(id.transferId))
                return common::Status::invalidArgument("duplicate dynamic batch file identity/path");
            paths_.insert(id.relativePath); transfers_.insert(id.transferId);
            found = files_.emplace(id.fileId,File{base,channel,id.rangeCount,{},false,{}}).first;
        } else if (!samePersistentIdentity(found->second.id,base) ||
                   found->second.rangeCount != id.rangeCount) {
            return common::Status::invalidArgument("stale dynamic file begin");
        } else if (found->second.done) {
            // A sender may retry after the receiver committed the range but before
            // the sender observed FILE_RESULT. Resume validation makes this replay
            // idempotent; the completion path below will not increment the batch twice.
            if (id.attempt == 0 || !found->second.ranges.contains(id.rangeId))
                return common::Status::invalidArgument("stale dynamic file begin");
            return common::Status::ok();
        }
        auto& file = found->second;
        if (file.leases.contains(id.rangeId) || file.ranges.contains(id.rangeId))
            return common::Status::invalidArgument("duplicate active range");
        for (const auto& [rangeId,lease] : file.leases) {
            if (id.rangeOffset < lease.id.rangeOffset+lease.id.rangeLength &&
                lease.id.rangeOffset < id.rangeOffset+id.rangeLength)
                return common::Status::invalidArgument("overlapping active range");
        }
        file.leases.emplace(id.rangeId,File::Lease{channel,id});
    } else {
        if (found == files_.end() || !samePersistentIdentity(found->second.id,base))
            return common::Status::invalidArgument("stale dynamic file completion");
        auto& file = found->second;
        if (file.done) {
            if (id.attempt == 0 || !file.ranges.contains(id.rangeId))
                return common::Status::invalidArgument("stale dynamic file completion");
            if (!status.isOk()) {
                if (status.code() == common::StatusCode::TransportError) failChannelLocked(channel, status);
                else failLocked(status);
                return status;
            }
            return common::Status::ok();
        }
        auto lease = file.leases.find(id.rangeId);
        if (lease == file.leases.end() || lease->second.owner != channel ||
            !samePersistentIdentity(lease->second.id,id))
            return common::Status::invalidArgument("foreign or stale range completion");
        if (!status.isOk()) {
            if (status.code() == common::StatusCode::TransportError) failChannelLocked(channel, status);
            else failLocked(status);
            return status;
        }
        file.leases.erase(lease);
        file.ranges.insert(id.rangeId);
        if (id.rangeId == 0 || file.ranges.size() == file.rangeCount) {
            file.done = true;
            ++completed_;
        }
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
common::Status PersistentDirectoryBatch::status() {
    std::lock_guard lock(mutex_);
    return status_;
}
}  // namespace cpnetflux::core::io
