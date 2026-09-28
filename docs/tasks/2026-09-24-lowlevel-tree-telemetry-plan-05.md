# LOWLEVEL-TREE-TELEMETRY-PLAN-05：按 ARBITRATION-03 重写实现计划

- 状态：ready
- 路线版本、任务版本：R2026-09-24.9 / v1
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：04 测试与质量
- 目标及理由：把 ARBITRATION-03 的三项生命周期裁定写成可直接实现、可验证的目录 telemetry v2 规格，替代被 QA-04 阻塞的 PLAN-04。
- 非目标：不改源码、测试、runner、CMake、决策、BOARD/ROSTER；不创建实现 worktree；不构建、不 SSH、不运行传输或性能实验。
- 输入 commit：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；工作树含共享文档改动，禁止覆盖。
- 必读资料和证据路径：
  - `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md`
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-04-result.md`
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-04-result.md`
  - `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`
  - `src/core/io/tree_transfer_client.cpp`
  - `src/core/io/file_transfer_client.cpp`
  - `src/core/io/file_download_client.cpp`
- 允许修改的文件/目录：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-05-result.md`；不得原地修改 PLAN-04、decision、BOARD、ROSTER 或源码。
- 工作分支/worktree：本任务只写文档，不创建 worktree。
- 前置条件、停止条件：必须完整纳入 paired N/A（first_payload、interfile_idle）、late stream_id 配对、same-control retry、attempt/span/slot 隔离、三类 idle fixture、四/五状态轴、wire/eligibility、parser golden bytes 和 logger failure 门禁；发现新冲突则标记 BLOCKED。
- 验收标准及实际可执行命令：结果列出目标 JSONL 状态机、正反例、源码插点、PLAN-04→PLAN-05 差异、实现/测试文件白名单和停止条件；记录 `git rev-parse HEAD`、`git diff --cached --name-only`、`git diff --check`、固定三源码 `git diff --quiet 3b0820d..HEAD`、UTF-8/LF/尾随空白检查。所有动态测试必须明确 `NOT_RUN`。
- 预期产物：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-05-result.md`

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务单。只写 PLAN-05 结果文件，不修改共享决策或任务板，不实现 telemetry。ARBITRATION-03 是唯一新裁定输入：其 paired N/A 允许项必须成为严格窄规则，retry 只能复用实际 control，idle 必须闭合连续/尾部/取消/错误。完成后记录命令退出码和未运行项；若无法同时满足，停止并标 BLOCKED。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 失败/跳过/阻塞及其原因：
- 下一角色可直接执行的下一步：
