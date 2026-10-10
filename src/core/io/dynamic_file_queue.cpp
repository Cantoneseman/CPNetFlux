#include "cpnetflux/core/io/dynamic_file_queue.h"

#include <algorithm>
#include <unordered_set>
#include "cpnetflux/core/tree/tree_scan.h"

namespace cpnetflux::core::io {
bool samePersistentIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept {
    return a.fileId == b.fileId && a.generation == b.generation && a.totalSize == b.totalSize &&
        a.relativePath == b.relativePath && a.transferId == b.transferId &&
        a.chunkSize == b.chunkSize && a.mtimeUnixSeconds == b.mtimeUnixSeconds &&
        a.checksumAlgorithm == b.checksumAlgorithm;
}
DynamicFileQueue::DynamicFileQueue(std::vector<PersistentFileIdentity> files,
    std::uint32_t channels, std::uint32_t window, std::size_t capacity)
    : files_(std::move(files)), window_(window) {
    if (channels == 0 || channels > 8 || window == 0 || window > 16 || capacity == 0 || capacity > 256) {
        status_ = common::Status::invalidArgument("invalid dynamic queue bounds");
        return;
    }
    ring_.resize(capacity);
    credits_.resize(channels);
    std::unordered_set<std::uint32_t> ids;
    std::unordered_set<std::string> paths, transfers;
    for (const auto& file : files_) {
        if (file.fileId == 0 || file.generation == 0 || file.chunkSize == 0 ||
            !tree::validateTreeRelativePath(file.relativePath).isOk() || file.transferId.empty() ||
            !ids.insert(file.fileId).second || !paths.insert(file.relativePath).second ||
            !transfers.insert(file.transferId).second) {
            status_ = common::Status::invalidArgument("invalid or duplicate dynamic file identity");
            return;
        }
    }
    std::stable_sort(files_.begin(), files_.end(), [](const auto& a, const auto& b) {
        return a.totalSize != b.totalSize ? a.totalSize > b.totalSize : a.fileId < b.fileId;
    });
    fill();
}
void DynamicFileQueue::fill() {
    while (size_ < ring_.size() && next_ < files_.size()) {
        ring_[tail_] = Ready{std::move(files_[next_++]), std::chrono::steady_clock::now()};
        tail_ = (tail_ + 1) % ring_.size();
        ++size_;
        high_ = std::max(high_, size_);
    }
}
common::Result<std::optional<DynamicFileLease>> DynamicFileQueue::claim(std::uint32_t channel) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    if (channel >= credits_.size()) return common::Status::invalidArgument("invalid queue channel");
    if (credits_[channel] >= window_) return common::Status::runtimeError("dynamic channel credit exhausted");
    if (!size_) return std::optional<DynamicFileLease>{};
    auto ready = std::move(*ring_[head_]);
    ring_[head_].reset();
    head_ = (head_ + 1) % ring_.size();
    --size_;
    auto wait = std::chrono::duration<double>(std::chrono::steady_clock::now() - ready.admitted).count();
    active_.emplace(ready.file.fileId, Active{ready.file, channel});
    ++credits_[channel];
    fill();
    return std::optional<DynamicFileLease>{DynamicFileLease{std::move(ready.file), wait}};
}
common::Status DynamicFileQueue::complete(std::uint32_t channel, const PersistentFileIdentity& file) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    auto found = active_.find(file.fileId);
    if (found == active_.end() || found->second.channel != channel ||
        !samePersistentIdentity(found->second.file, file))
        return common::Status::invalidArgument("dynamic completion identity/owner mismatch");
    --credits_[channel];
    active_.erase(found);
    return common::Status::ok();
}
void DynamicFileQueue::cancel(const common::Status& reason) {
    std::lock_guard lock(mutex_);
    if (status_.isOk()) status_ = reason.isOk() ? common::Status::runtimeError("dynamic queue cancelled") : reason;
    for (auto& item : ring_) item.reset();
    size_ = 0;
    active_.clear();
    std::fill(credits_.begin(), credits_.end(), 0);
}
std::size_t DynamicFileQueue::highWatermark() const { std::lock_guard lock(mutex_); return high_; }
std::size_t DynamicFileQueue::outstanding() const { std::lock_guard lock(mutex_); return active_.size(); }
}  // namespace cpnetflux::core::io
