#include "cpnetflux/core/scheduler/task_queue.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

cpnetflux::core::scheduler::WorkItem makeItem(const std::string& taskId, const std::string& fileId,
                                             std::uint64_t offset) {
    cpnetflux::core::scheduler::WorkItem item;
    item.taskId = taskId;
    item.fileId = fileId;
    item.relativePath = fileId;
    item.offset = offset;
    item.length = 1;
    return item;
}

}  // namespace

TEST(SchedulerTaskQueueTest, WeightedRoundRobinKeepsTaskOrder) {
    cpnetflux::core::scheduler::WeightedRoundRobinTaskQueue queue;
    queue.setTaskWeight("task-a", 1);
    queue.setTaskWeight("task-b", 3);

    queue.push(makeItem("task-a", "a-1", 0));
    queue.push(makeItem("task-b", "b-1", 0));
    queue.push(makeItem("task-b", "b-2", 1));
    queue.push(makeItem("task-a", "a-2", 1));
    queue.push(makeItem("task-b", "b-3", 2));

    std::vector<std::string> observed;
    for (int index = 0; index < 5; ++index) {
        auto item = queue.pop();
        ASSERT_TRUE(item.has_value());
        observed.push_back(item->taskId);
    }

    ASSERT_EQ(observed.size(), 5U);
    EXPECT_EQ(observed[0], "task-a");
    EXPECT_EQ(observed[1], "task-b");
    EXPECT_EQ(observed[2], "task-b");
    EXPECT_EQ(observed[3], "task-b");
    EXPECT_EQ(observed[4], "task-a");
    EXPECT_TRUE(queue.empty());

    queue.clear();
    EXPECT_TRUE(queue.empty());
}
