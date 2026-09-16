#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cpnetflux/core/scheduler/work_item.h"

namespace cpnetflux::core::scheduler {

class WeightedRoundRobinTaskQueue {
   public:
    void setTaskWeight(const std::string& taskId, std::uint32_t weight);
    void push(WorkItem item);
    [[nodiscard]] std::optional<WorkItem> pop();
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;

   private:
    struct TaskBucket {
        std::deque<WorkItem> items;
        std::uint32_t weight = 1;
    };

    void rebuildSchedule();

    std::unordered_map<std::string, TaskBucket> tasks_;
    std::vector<std::string> order_;
    std::vector<std::string> schedule_;
    std::size_t cursor_ = 0;
};

}  // namespace cpnetflux::core::scheduler
