# LOWLEVEL-TREE-LOOKAHEAD-PLAN-REVISION-QA-04

- 任务版本：`R2026-09-24.11 / v2`
- 责任角色：04 测试与质量；交付：00 总指挥
- 状态：ready；只审查计划，不授权代码、worktree、构建、测试、SSH 或实验。

## 目标

独立复审 03 的 v2 修订计划，确认 QA-03 的三个阻塞是否已闭合，并判断是否允许 00 另发窄代码实现任务。审查必须以固定源码和任务文档为准，不把计划写成实现或性能结果。

## 输入

- 固定实现基线：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- 当前资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`
- 契约/QA：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-lookahead-revision-qa-02-result.md`
- 原计划/QA：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-03-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-qa-03-result.md`
- 修订计划：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-revision-04-result.md`
- 修订摘要：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-revision-04-last-message.md`

## 必查项

1. 固定源码没有 `control_id/controlId`：计划是否彻底改为进程内不可序列化 opaque candidate/ControlClient 句柄，是否明确 `transferId` 不作控制身份，且不新增 summary/event/reason/schema 字段。
2. 候选 eligibility 是否机械要求 `TreeFileStatus::Pending`，并覆盖 Completed、Changed、Failed、Transferring/未知状态、方向/路径/指纹/metadata 失败；非 Pending 不 reservation、不推进候选游标、不占资源。
3. 当前 `runTreeScheduler` lambda 的真实 `state.nextIndex++` 接缝是否由实际调用的统一 helper 明确替换；普通 claim、candidate reserve、cancel 归还、handoff consume 是否同锁且保证 index 不重复、不丢失、handoff 一次。
4. 既有 REVISION-02 约束是否仍完整：候选不发文件级命令、不建 data FD；upload/download handoff 后原命令顺序；worker/off/global depth；pending/FD/memory/getrlimit 硬门；白名单/禁改路径；外部输出不变；动态 NOT_RUN；无性能或 100 Mbps/100G 结论。
5. 任务结果是否记录固定输入、HEAD、index、哈希/文档门禁，并确认没有代码、worktree、构建、测试、SSH、实验或 Git 提交。

## 验收与交付

- 结果：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-revision-qa-04-result.md`；摘要：同名 `-last-message.md`。
- 结论只能是 `PASS`（允许 00 另发实现任务）、`PARTIAL`（列出可修订阻塞，不得实现）或 `BLOCKED`（固定输入/文档不可信）。
- 必须用 `git rev-parse HEAD`、`git diff --cached --name-only`、固定源码 parity、计划/摘要 SHA-256 稳定性、UTF-8/LF/尾随空白和 `git diff --check` 留证。
- 只读检查；不要改 PLAN-REVISION-04、源码、测试、CMake、BOARD、ROSTER、决策或 index。
