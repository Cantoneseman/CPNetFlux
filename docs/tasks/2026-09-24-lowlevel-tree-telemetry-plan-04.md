# LOWLEVEL-TREE-TELEMETRY-PLAN-04

- 状态：ready
- 路线版本、任务版本：`R2026-09-24.8`，`v1`
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：00 总指挥，随后 04 独立 QA-04
- 目标及理由：依据固定源码、QA-02、ARCH-REVIEW-01/02 和 00 R2026-09-24.8 裁定，产出可供独立质量审查和后续实现逐项执行的完整目录阶段 telemetry 实现规格。关闭 skip/attempt-0/status/coverage 形状矛盾，使后续 profiling 能定位单文件与多文件目录关键路径，而不改变协议或 transfer 语义。
- 非目标：不改源码、测试、runner、CMake、协议、manifest/resume/checksum/scheduler/compression/backend、连接/worker 默认值；不建实现 worktree、不构建/跑 CTest/smoke/benchmark，不 SSH、实验、清理或提交。不直接实施 telemetry、lookahead、sendv 或 manifest 优化。
- 输入 commit（完整 SHA）、工作树状态：固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。主工作区有其他未提交协作文档；执行人不得覆盖这些文件或操作 index。
- 必读资料和证据路径：`docs/coordination/TASK_TEMPLATE.md`、`docs/coordination/BOARD.md`、`docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-03-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-02-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-01-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-02.md`。
- 允许修改的文件/目录；禁止修改的资源：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-04-result.md`。禁止改 decision、BOARD、ROSTER、架构结果、源码、tests、tools、CMake 和共享 index。
- 工作分支/worktree：只读主工作区规格核对；不切换分支、不建 worktree。
- 前置条件、环境占用和停止条件：确认输入为 R2026-09-24.8；发现固定源码/前序 QA 与裁定仍冲突，停止并报告，不自行扩展。PLAN-03 BLOCKED 结果只保留历史，不覆写或声称其通过。
- 验收标准及实际可执行命令：
  1. 给出 `tree_stage_v1` 和 `stream_identity` 严格键集、类型、null/scope/stage/state 规则、uint64 原 token 限制、配对/append-only/logger/error 隔离；将 ARCH-REVIEW-01 有效和反例逐项映射，不缩写成泛称。
  2. 以固定源码列 upload/download 控制顺序和阶段端点；包含 finalize/226 相接、download mtime、manifest finalize、空文件、retry、失败、interfile idle、preflight 和被排除 `resume_validation` 的精确范围。
  3. 写全生命周期向量：Completed gate 成功、Changed、control gate 失败、无 range all/partial streams、空文件、连接/SessionInit 失败、首 payload 前失败、retry、setup early return；attempt-0 的 decision `skipped` 语义及 Changed `manifest_finalize` 精确例外遵循 arbitration v1；其余仍严格 null 矩阵。
  4. 分离 `transfer_status`、原始/`process_status`、`integrity_status`、`evidence_status`、`wire_accounting_status`；用表列出每个向量的最终映射、性能 eligibility 与 logical/wire denominator。hash 缺失不判 mismatch；no-range wire 不造零。
  5. 给出确定性 parser/validator 正反向向量、JSONL 故障策略、非零 telemetry overhead 验收办法，并明确真实 tree 上传/下载 on/off、payload/hash/manifest/resume/frame 对照及全部动态事项均 `NOT_RUN`。
  6. QA-02 十项逐条索引到 PLAN-04 章节及实现/测试门禁；列出 PLAN-03 BLOCKED 的 supersede 关系、剩余风险和后续 QA-04 门槛。
  7. 结果只写指定文件；固定源码至资料 HEAD 实现路径差异为空；UTF-8/LF/无尾随空白、HEAD/index 不变、`git diff --check` 通过。此为文档验收，不可宣称实现准入；04 必须独立复审后才能考虑 implementation。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用；本任务禁止连接云端。
- 预期产物路径：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-04-result.md`

## 派给角色的消息

请续接 ROSTER 中登记的 03 核心实现聊天，严格按本任务完成规格结果，不做任何实现/测试/构建。将 PLAN-03 BLOCKED 作为 superseded 历史证据保留。特别注意 arbitration 对 attempt-0 decision、Changed manifest_finalize 和各独立状态轴的明确裁定。若其与严格字段、源码或 QA 约束仍冲突，停止并具体报告。只写结果路径；若用 CLI `--output-last-message`，必须输出到不同的 last-message 文件，避免覆盖正式正文。

## 执行回执

- 实际输入/输出 commit：待执行。
- 实际改动：待执行。
- 实际命令、退出码与证据文件：待执行。
- transfer / integrity / evidence / wire accounting（适用时）：必须在产物的状态向量表独立说明。
- 失败/跳过/阻塞及其原因：待执行。
- 剩余风险、未完成事项：待执行。
- 下一角色可直接执行的下一步：04 对 PLAN-04 执行独立 QA-04；PASS 前无源码 implementation。

## 验收与路线变化

00 验收 PLAN-04 文件完整性后再派 04。QA-04 明确 PASS 才建立 `codex/LOWLEVEL-TREE-TELEMETRY-IMPL-01` 独立 worktree。若路线变化，保留 PLAN-03 的 BLOCKED 原文与结果，新增版本，不覆盖历史。
