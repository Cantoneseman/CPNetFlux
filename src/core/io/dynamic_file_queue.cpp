#include "cpnetflux/core/io/dynamic_file_queue.h"

#include <algorithm>
#include <unordered_set>
#include <string>
#include "cpnetflux/core/tree/tree_scan.h"

namespace cpnetflux::core::io {
bool samePersistentIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept {
    return a.fileId == b.fileId && a.generation == b.generation && a.totalSize == b.totalSize &&
        a.relativePath == b.relativePath && a.transferId == b.transferId &&
        a.chunkSize == b.chunkSize && a.mtimeUnixSeconds == b.mtimeUnixSeconds &&
        a.checksumAlgorithm == b.checksumAlgorithm && a.rangeId == b.rangeId &&
        a.rangeOffset == b.rangeOffset && a.rangeLength == b.rangeLength &&
        a.rangeCount == b.rangeCount && a.attempt == b.attempt;
}
bool samePersistentRangeIdentity(const PersistentFileIdentity& a, const PersistentFileIdentity& b) noexcept {
    return samePersistentIdentity(a, b);
}
static std::string activeKey(const PersistentFileIdentity& file) {
    return std::to_string(file.fileId) + ":" + std::to_string(file.generation) + ":" +
        std::to_string(file.rangeId) + ":" + std::to_string(file.attempt) + ":" +
        std::to_string(file.transferId.size()) + ":" + file.transferId + ":" +
        std::to_string(file.relativePath.size()) + ":" + file.relativePath;
}
DynamicFileQueue::DynamicFileQueue(std::vector<PersistentFileIdentity> files,
    std::uint32_t channels, std::uint32_t window, std::size_t capacity)
    : files_(std::move(files)), window_(window) {
    if (channels == 0 || channels > 64 || window == 0 || window > 16 || capacity == 0 || capacity > 256) {
        status_ = common::Status::invalidArgument("invalid dynamic queue bounds");
        return;
    }
    ring_.resize(capacity);
    credits_.resize(channels);
    std::unordered_map<std::uint32_t, std::vector<PersistentFileIdentity>> groups;
    std::unordered_map<std::string,std::uint32_t> paths,transfers;
    for (const auto& file : files_) {
        if (!file.fileId || !file.generation || !file.chunkSize || file.transferId.empty() ||
            !tree::validateTreeRelativePath(file.relativePath).isOk() ||
            (paths.contains(file.relativePath) && paths[file.relativePath] != file.fileId) ||
            (transfers.contains(file.transferId) && transfers[file.transferId] != file.fileId)) {
            status_ = common::Status::invalidArgument("invalid or aliased dynamic file identity");
            return;
        }
        paths[file.relativePath] = transfers[file.transferId] = file.fileId;
        groups[file.fileId].push_back(file);
    }
    for (auto& [fileId, ranges] : groups) {
        auto base = ranges.front();
        base.rangeId = base.rangeOffset = base.rangeLength = base.rangeCount = base.attempt = 0;
        if (ranges.front().rangeId == 0) {
            if (ranges.size() != 1) {
                status_ = common::Status::invalidArgument("duplicate whole-file identity"); return;
            }
            continue;
        }
        std::sort(ranges.begin(),ranges.end(),[](const auto& a,const auto& b) {
            return a.rangeOffset < b.rangeOffset;
        });
        std::unordered_set<std::uint64_t> ids;
        std::uint64_t nextOffset = 0;
        for (const auto& file : ranges) {
            auto candidate = file;
            candidate.rangeId = candidate.rangeOffset = candidate.rangeLength =
                candidate.rangeCount = candidate.attempt = 0;
            if (!samePersistentIdentity(base,candidate) || !file.rangeId ||
                file.rangeCount != ranges.size() || file.rangeId > file.rangeCount ||
                !ids.insert(file.rangeId).second || !file.rangeLength ||
                file.rangeOffset != nextOffset || file.rangeOffset > file.totalSize ||
                file.rangeLength > file.totalSize-file.rangeOffset) {
                status_ = common::Status::invalidArgument("ranges do not partition one stable file");
                return;
            }
            nextOffset += file.rangeLength;
        }
        if (nextOffset != base.totalSize) {
            status_ = common::Status::invalidArgument("incomplete range partition"); return;
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
    active_.emplace(activeKey(ready.file), Active{ready.file, channel});
    ++credits_[channel];
    fill();
    return std::optional<DynamicFileLease>{DynamicFileLease{std::move(ready.file), wait}};
}
common::Status DynamicFileQueue::complete(std::uint32_t channel, const PersistentFileIdentity& file) {
    std::lock_guard lock(mutex_);
    if (!status_.isOk()) return status_;
    auto found = active_.find(activeKey(file));
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
