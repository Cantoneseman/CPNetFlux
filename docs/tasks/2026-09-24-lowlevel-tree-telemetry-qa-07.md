# LOWLEVEL-TREE-TELEMETRY-QA-07：PLAN-07 两项更正复审

- 状态：ready
- 路线版本、任务版本：R2026-09-24.9 / v1
- 发起人：00 总指挥；执行角色：04 测试与质量；验收角色：00 总指挥
- 目标及理由：独立核验 PLAN-07 是否准确修正 QA-06 指出的两个文档规格问题，并确认 PLAN-06 的其余输入未被隐式改写；通过后才决定是否授权目录 telemetry 实现。
- 非目标：不改源码、测试、runner、决策或 BOARD/ROSTER；不创建 worktree；不构建、测试运行态 parser、SSH、真实传输或性能实验。
- 输入 commit（完整 SHA）、工作树状态：固定源码基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，需执行时复核。
- 必读资料和证据路径：
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-07-result.md`（派单时 SHA-256 `2DEDDF4FECB60C221CB47428014F9550386FCF7DFF848E2B7EB282DFA3BF8EEF`）
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-06-result.md`
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-06-result.md`
  - `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md`
  - `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-01-result.md`
  - `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`
  - `src/core/io/file_transfer_client.cpp`
- 允许修改的文件/目录；禁止修改的资源：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-07-result.md`；保持 PLAN-06/07、decision、源码和共享状态文件只读。
- 工作分支/worktree：当前文档审查不创建 worktree，不切分支，不操作 Git index。
- 前置条件、环境占用和停止条件：确认 PLAN-07 SHA 与派单输入相同、HEAD 未变、源文件仍对应固定基线；任一变化则停止并 BLOCKED。发现任一规格仍有歧义则具体指出，不得放行实现。
- 验收标准及实际可执行命令（不要只写“所有测试”）：核验 `span_id` 仅为 `1..UINT64_MAX` 且完整 26 键 `span_id=0` 向量会拒绝；核验 R-SAME 四条 upload `data_connect` 都使用源代码真实 `stream_id=0`，retry 复用真实 `control_id` 并保持 attempt/span 区分；独立重跑 6 个 JSONL block / 44 行的键集、重复键、生命周期和样例语义 lint；复查输入 SHA 稳定、HEAD/index/source parity、UTF-8/LF/空白及 `git diff --check`。静态文档通过不等于 parser/传输/性能动态验收，全部如实记 `NOT_RUN`。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用；禁止 SSH 和云端操作。
- 预期产物路径：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-07-result.md`

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务单和 `docs/coordination/BOARD.md`，续接 ROSTER 中已存在的 04 聊天。只复审 PLAN-07 两个精确更正，并检查 PLAN-06 其余部分/固定架构输入无意外变化。结论 PASS 才建议进入独立 worktree 实现；否则列出可执行的最小修正。动态 parser、logger、传输、hash/resume、开销门和云端实验均应明确为 NOT_RUN；不得把 fixture lint 等同运行态通过。

## 执行回执

- 实际输入/输出 commit：
- 输入 SHA-256 与稳定性：
- 实际改动：
- 实际命令、退出码与证据文件：
- 静态规格结论；动态 NOT_RUN 项：
- 阻塞、风险与下一步：

## 验收与路线变化

验收人、结论和依据：

若路线改变：记录替代任务、停止点、需保留的证据，不能覆写旧实验结果。
