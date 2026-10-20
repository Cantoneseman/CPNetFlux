#include "cpnetflux/core/io/persistent_directory_batch.h"
#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include <algorithm>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include "cpnetflux/core/tree/tree_scan.h"
namespace cpnetflux::core::io {
PersistentDirectoryBatch::PersistentDirectoryBatch(std::uint32_t total, std::uint32_t channels)
    : total_(total) {
    if (!channels || channels > kPersistentTreeMaxChannels) status_ = common::Status::invalidArgument("invalid batch channels");
    else {
        channels_.resize(channels);
        monitor_ = std::thread([this] {
            std::unique_lock lock(mutex_);
            while (!stopped_ && status_.isOk() && !finished_) {
                checkConnectionsLocked();
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
    channels_[channel].failed = false;
    channels_[channel].dataDisconnected = false;
    return common::Status::ok();
}
void PersistentDirectoryBatch::bind(std::uint32_t channel, int controlFd, int dataFd, bool ready) {
    std::lock_guard lock(mutex_);
    channels_.at(channel).control = controlFd;
    channels_.at(channel).data = dataFd;
    channels_.at(channel).ready = ready;
    channels_.at(channel).dataDisconnected = false;
    channels_.at(channel).dataActive = false;
    if (!status_.isOk()) {
        if (controlFd >= 0) (void)::shutdown(controlFd, SHUT_RDWR);
        if (dataFd >= 0) (void)::shutdown(dataFd, SHUT_RDWR);
    }
    changed_.notify_all();
}
void PersistentDirectoryBatch::markDataActive(std::uint32_t channel) {
    std::lock_guard lock(mutex_);
    channels_.at(channel).dataActive = true;
}
void PersistentDirectoryBatch::unbind(std::uint32_t channel, int expectedControlFd) {
    std::lock_guard lock(mutex_);
    auto& state = channels_.at(channel);
    if (expectedControlFd >= 0 && state.control != expectedControlFd) return;
    state.control = state.data = -1;
    state.ready = false;
    // Preserve done=true for a healthy lane that finished before a sibling
    // retries. The retry must wait only for the failed lane to reattach.
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
    state.failed = true;
    state.attached = false;
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
    const bool reportTargetConflict =
        status_.message().find("output already exists") != std::string::npos;
    for (auto& c : channels_) {
        // Keep controls available for the authoritative semantic terminal reply.
        // Data sockets still stop immediately so siblings cannot keep writing.
        if (c.control >= 0 && !reportTargetConflict) (void)::shutdown(c.control, SHUT_RDWR);
        if (c.data >= 0) (void)::shutdown(c.data, SHUT_RDWR);
    }
    changed_.notify_all();
}
void PersistentDirectoryBatch::cancel(const common::Status& status) {
    std::lock_guard lock(mutex_); failLocked(status);
}
void PersistentDirectoryBatch::checkConnectionsLocked() {
    if (finished_) return;
    // No pipelined commands are allowed while a directory batch owns these controls.
    for (std::uint32_t index = 0; index < channels_.size(); ++index) {
        auto& channel = channels_[index];
        // A completed lane may close its control connection after receiving its
        // terminal reply while sibling lanes are still draining. Its done state
        // remains part of the batch barrier and must not be reclaimed as a fault.
        if (channel.failed || channel.done) continue;
        if (channel.control >= 0) {
            pollfd descriptor{channel.control,POLLIN,0};
            if (::poll(&descriptor,1,0) > 0 && descriptor.revents != 0) {
                // POLLIN means a normal control reply may be queued. Inspect the
                // socket without consuming it; only EOF or a fatal read error is
                // a control disconnect.
                std::uint8_t byte = 0;
                const auto received = ::recv(channel.control,&byte,sizeof(byte),MSG_PEEK|MSG_DONTWAIT);
                if (received == 0 || (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                    failChannelLocked(index, common::Status::transportError(
                        "dynamic batch channel control disconnected/cancelled"));
                    continue;
                }
            }
        }
        if (channel.data >= 0 && !channel.dataDisconnected) {
            pollfd descriptor{channel.data,POLLIN,0};
            if (::poll(&descriptor,1,0) <= 0 || descriptor.revents == 0) continue;
            // A positive peek means the peer may already have sent its first frame;
            // leave it queued for receivePersistentTree. EOF/reset is safe to detect
            // without consuming application data.
            std::uint8_t byte = 0;
            const auto received = ::recv(channel.data,&byte,sizeof(byte),MSG_PEEK|MSG_DONTWAIT);
            if (received == 0 || (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                // Once data receive has started, let the protocol receiver
                // distinguish DirectoryEnd from a truncated stream. Before that,
                // EOF is a lane failure and must trigger reattachment promptly.
                if (channel.dataActive)
                    channel.dataDisconnected = true;
                else
                    failChannelLocked(index, common::Status::transportError(
                        "dynamic batch channel data disconnected/cancelled"));
            }
        }
    }
}
common::Status PersistentDirectoryBatch::wait(
    bool finishing, std::chrono::milliseconds timeout, std::uint32_t observingChannel) {
    std::unique_lock lock(mutex_);
    if (observingChannel >= channels_.size())
        return common::Status::invalidArgument("invalid batch wait channel");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (!status_.isOk()) return status_;
        checkConnectionsLocked();
        if (!status_.isOk()) return status_;
        if (channels_[observingChannel].failed)
            return common::Status::transportError("dynamic batch channel disconnected/cancelled");
        bool all = std::all_of(channels_.begin(), channels_.end(), [&](const auto& c) {
            return finishing ? c.done : (c.ready || c.done);
        });
        if (all) {
            if (finishing && (completed_ != total_ || files_.size() != total_))
                failLocked(common::Status::invalidArgument("dynamic batch file count mismatch"));
            if (finishing && status_.isOk()) { finished_ = true; changed_.notify_all(); }
            return status_;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            std::ostringstream detail;
            detail << "dynamic batch channel timeout completed=" << completed_ << "/" << total_ << " channels=";
            for (std::size_t index = 0; index < channels_.size(); ++index) {
                const auto& channel = channels_[index];
                if (index) detail << ",";
                detail << index << "{ready=" << channel.ready << ",done=" << channel.done
                       << ",attached=" << channel.attached << ",failed=" << channel.failed
                       << ",control=" << channel.control << ",data=" << channel.data << "}";
            }
            failLocked(common::Status::transportError(detail.str()));
            return status_;
        }
        changed_.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
    }
}
common::Status PersistentDirectoryBatch::waitReady(
    std::uint32_t channel, std::chrono::milliseconds timeout) {
    return wait(false, timeout, channel);
}
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
    (void)timeout;
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (channel >= channels_.size() || !channels_[channel].ready)
        return common::Status::invalidArgument("invalid/duplicate batch finish");
    if (channels_[channel].done) return common::Status::ok();
    channels_[channel].done = true;
    const bool allChannelsDone = std::all_of(channels_.begin(), channels_.end(),
        [](const auto& state) {
            return state.done || (!state.attached && !state.failed);
        });
    if (allChannelsDone) {
        if (completed_ != total_ || files_.size() != total_) {
            failLocked(common::Status::invalidArgument("dynamic batch file count mismatch"));
        } else {
            finished_ = true;
        }
    }
    changed_.notify_all();
    return status_;
}
common::Status PersistentDirectoryBatch::status() {
    std::lock_guard lock(mutex_);
    return status_;
}
bool PersistentDirectoryBatch::allChannelsDetached() {
    std::lock_guard lock(mutex_);
    return std::all_of(channels_.begin(), channels_.end(),
        [](const auto& channel) { return !channel.attached; });
}
bool PersistentDirectoryBatch::completeAndDetached() {
    std::lock_guard lock(mutex_);
    return finished_ && std::all_of(channels_.begin(), channels_.end(),
        [](const auto& channel) { return !channel.attached; });
}
}  // namespace cpnetflux::core::io
