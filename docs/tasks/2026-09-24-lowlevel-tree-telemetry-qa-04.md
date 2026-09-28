# LOWLEVEL-TREE-TELEMETRY-QA-04

- 状态：ready
- 路线版本、任务版本：`R2026-09-24.8`，`v1`
- 发起人：00 总指挥；执行角色：04 测试与质量；验收角色：00 总指挥
- 目标及理由：独立审查 PLAN-04 是否已闭合固定源码生命周期、严格 JSON/schema、attempt-0 两项例外、transfer/process/integrity/evidence/wire 分轴和实现门禁；为 telemetry implementation 提供明确 PASS/PARTIAL/BLOCKED 结论。
- 非目标：不改源码、测试、runner、CMake、decision、BOARD、ROSTER；不创建 worktree、不实现、不构建、不跑 CTest/smoke/benchmark、不 SSH、不启动实验或清理。
- 输入 commit（完整 SHA）、工作树状态：固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。共享工作树有既有文档改动，禁止覆盖或操作 index。
- 必读资料和证据路径：`docs/coordination/TASK_TEMPLATE.md`、`docs/coordination/BOARD.md`、`docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-02.md`、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-04.md`、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-04-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-02-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`、固定源码三文件。
- 允许修改的文件/目录；禁止修改的资源：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-04-result.md`。CLI 末消息若使用须输出到独立 last-message 文件，不能覆盖结果。
- 工作分支/worktree：只读主工作区；不切换分支、不建 worktree。
- 前置条件、环境占用和停止条件：只审查 R2026-09-24.8 / PLAN-04；若存在任何 schema、源码顺序、状态轴或覆盖率矛盾，停止并给出最小修订，不批准实现。
- 验收标准及实际可执行命令：逐项核对 PLAN-04 的 strict key/null/type/parser 向量、finalize/226/mtime/manifest 顺序、Completed decision 与 Changed attempt-0 finalize、no-range attempt>=1、空文件/失败/retry/interfile idle、四轴与 wire/eligibility、logger failure isolation、旧消费者和 on/off hash/frame 回归门。执行 `git rev-parse HEAD`、`git diff --cached --name-only`、固定源码 `git diff --quiet 3b0820d..HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp`、`git diff --check`；动态测试全部 NOT_RUN。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用。
- 预期产物路径：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-04-result.md`

## 派给角色的消息

请续接既有 04 质量聊天，独立审查 PLAN-04，不做实现或实验。结论必须区分文档契约 PASS 与实现/动态测试 NOT_RUN；若不 PASS，逐条列出 blocker 和最小修订。只写指定 result；CLI last-message 另存。不要将 03 既有聊天派单失败或 00 补齐规格误报为 03 执行证据。

## 执行回执

- 实际输入/输出 commit：待执行。
- 实际改动：待执行。
- 实际命令、退出码与证据文件：待执行。
- transfer / integrity / evidence / wire accounting：独立审查 PLAN-04 的状态轴和 eligibility。
- 失败/跳过/阻塞及其原因：待执行。
- 剩余风险、未完成事项：待执行。
- 下一角色可直接执行的下一步：仅 QA PASS 后才派 telemetry implementation；否则返回 00 修订。
