#include "cpnetflux/core/scheduler/task_queue.h"

#include <algorithm>

namespace cpnetflux::core::scheduler {
namespace {

std::uint32_t normalizedWeight(std::uint32_t weight) noexcept {
    return weight == 0 ? 1 : weight;
}

}  // namespace

void WeightedRoundRobinTaskQueue::setTaskWeight(const std::string& taskId, std::uint32_t weight) {
    auto& bucket = tasks_[taskId];
    bucket.weight = normalizedWeight(weight);
    if (std::find(order_.begin(), order_.end(), taskId) == order_.end()) {
        order_.push_back(taskId);
    }
    rebuildSchedule();
}

void WeightedRoundRobinTaskQueue::push(WorkItem item) {
    auto& bucket = tasks_[item.taskId];
    if (std::find(order_.begin(), order_.end(), item.taskId) == order_.end()) {
        order_.push_back(item.taskId);
    }
    bucket.items.push_back(std::move(item));
    if (bucket.weight == 0) {
        bucket.weight = 1;
    }
    rebuildSchedule();
}

std::optional<WorkItem> WeightedRoundRobinTaskQueue::pop() {
    if (schedule_.empty()) {
        return std::nullopt;
    }
    for (std::size_t attempts = 0; attempts < schedule_.size(); ++attempts) {
        const std::string& taskId = schedule_[cursor_];
        cursor_ = (cursor_ + 1) % schedule_.size();
        auto bucketIter = tasks_.find(taskId);
        if (bucketIter == tasks_.end()) {
            continue;
        }
        auto& bucket = bucketIter->second;
        if (bucket.items.empty()) {
            continue;
        }
        WorkItem item = std::move(bucket.items.front());
        bucket.items.pop_front();
        return item;
    }
    return std::nullopt;
}

bool WeightedRoundRobinTaskQueue::empty() const noexcept {
    for (const auto& [taskId, bucket] : tasks_) {
        if (!bucket.items.empty()) {
            return false;
        }
    }
    return true;
}

void WeightedRoundRobinTaskQueue::clear() noexcept {
    tasks_.clear();
    order_.clear();
    schedule_.clear();
    cursor_ = 0;
}

void WeightedRoundRobinTaskQueue::rebuildSchedule() {
    schedule_.clear();
    for (const std::string& taskId : order_) {
        const auto bucketIter = tasks_.find(taskId);
        if (bucketIter == tasks_.end()) {
            continue;
        }
        const std::uint32_t weight = normalizedWeight(bucketIter->second.weight);
        for (std::uint32_t repeat = 0; repeat < weight; ++repeat) {
            schedule_.push_back(taskId);
        }
    }
    if (!schedule_.empty()) {
        cursor_ %= schedule_.size();
    } else {
        cursor_ = 0;
    }
}

}  // namespace cpnetflux::core::scheduler
