# LOWLEVEL-TREE-TELEMETRY-PLAN-07：修正两个最小规格错误

- 状态：ready
- 路线版本、任务版本：R2026-09-24.9 / v1
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：04 测试与质量
- 目标及理由：修正 QA-06 明确定位的两个文档矛盾，保持 PLAN-06 其余内容不变。
- 非目标：不改源码、测试、runner、decision、BOARD/ROSTER；不创建实现 worktree；不构建、不 SSH、不运行实验。
- 输入 commit：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 必读资料：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-06-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-06-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-01-result.md`、固定三份源码。
- 允许修改：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-07-result.md`；不得原地修改 PLAN-06。
- 必须修正：
  1. 所有 `span_id` schema、范围、正反例和 fixture 断言统一为 `1..UINT64_MAX`；新增 `span_id=0` 拒绝向量。
  2. R-SAME-CONTROL 的 upload `data_connect` start/terminal 使用固定源码实际 stream ID（与同 fixture 其他 stream 行一致），不写 null；重新运行完整 6 block/44 行结构与语义 lint。
- 停止条件：任一输入变化、修正引入新字段/协议语义、lint 不能通过，标 BLOCKED。
- 预期产物：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-07-result.md`

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据：
- 阻塞/未运行项：
- 下一步：
