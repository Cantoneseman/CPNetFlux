# LOWLEVEL-TREE-TELEMETRY-ARBITRATION-03：架构收口

- 状态：in_progress
- 路线版本、任务版本：R2026-09-24.9 / v1
- 发起人：00 总指挥；执行角色：01 架构与需求；验收角色：00 总指挥
- 目标及理由：依据 QA-04 的 BLOCKED 复审，冻结三处与固定源码时序冲突的最小可实现契约，解除或确认 telemetry implementation 闸门。
- 非目标：不改源码、测试、runner、云端、BOARD/ROSTER；不启动构建、实验或 SSH；不重新评估旧性能结论。
- 输入 commit：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；共享工作树文档改动保留。
- 必读资料和证据路径：
  - `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-04-result.md`
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-04-result.md`
  - `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`
  - `src/core/io/tree_transfer_client.cpp`
  - `src/core/io/file_transfer_client.cpp`
  - `src/core/io/file_download_client.cpp`
- 允许修改的文件/目录：仅新增本任务结果文件 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md`；不得改决策、任务板或源码。
- 前置条件、停止条件：必须分别回答以下三项，并给出唯一推荐形状；若仍有不可同时满足的约束，标记 BLOCKED，不进入实现。
  1. no-range：固定 `first_payload` 起点在 data connection 完成、ResumeResponse 后才知 no missing range、JSONL append-only 及 N/A 单行四者如何取舍。必须明确上传/下载、首块前失败、空文件。
  2. retry：固定源码重试复用原 `control_id` 的事实，区分 control 生命周期与 attempt/span/stream；不得声称每次 retry 新建 control。
  3. interfile idle：连续文件、worker 尾部、取消/错误的 append-only 起点与 terminal 形状；不得回写。
- 验收标准及实际可执行命令：结果文件必须列出推荐契约、被放弃选项及理由、事件/状态字段示例、对 PLAN-04 的逐条修改清单；执行 `git diff --check`、UTF-8/尾随空白检查、`git diff --quiet 3b0820d..HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp`；均需记录实际结果。
- 预期产物路径：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md`

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务单和 `docs/coordination/BOARD.md`，核对版本。只做架构裁定并写结果文件；不要修改共享决策、任务板、源码或测试。结果必须允许 03 直接据此改写 PLAN-04，允许 04 再次独立复审。若契约仍冲突，停止并明确阻塞点。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 失败/跳过/阻塞及其原因：
- 下一角色可直接执行的下一步：
