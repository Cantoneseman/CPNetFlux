# LOWLEVEL-TREE-TELEMETRY-PLAN-03

路线版本：`R2026-09-23.6`  
任务版本：`v1`  
固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`  
当前资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`

## 目标

根据 QA-02 与 ARCH-REVIEW-01，修订目录阶段 telemetry 的实现映射，形成 04 可独立复审、后续实现可逐项编码的 PLAN-03。实现仍须证明低开销、观测不改变传输语义，并为之后基于实测阶段证据选择目录热路径优化提供可靠观测。

## 输入

- `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`（R2026-09-23.6 语义规范）
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02-result.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-02-result.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-01-result.md`
- 固定源码提交 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`

## 非目标

- 不改源码、测试、runner、CMake、协议帧、manifest/resume/checksum/scheduler/compression/backend 或默认配置。
- 不创建 worktree、不构建、不运行 CTest/Smoke/benchmark、不 SSH、不启动跨域实验、不清理环境。
- 不将 QA-02/架构文档 PASS 解释为实现、正确性或性能验收。

## 允许修改范围

03 只写 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-03-result.md`。发现决策需进一步变化时停止，交 00 处理，不自行修改 decision、BOARD 或 ROSTER。

## 必须覆盖

1. 用固定源码 upload/download 调用顺序定义 `data_channel_finalize`、`transfer_complete_wait`、新增的下载 `download_mtime_finalize`、tree `manifest_finalize`；同一 attempt finalize 与 226 wait 相接不重叠。空文件和上游失败必须定义端点或 null 状态。
2. 精确抄录并检查 ARCH-REVIEW-01 的 `tree_stage_v1` 严格 JSON 键集合、`stream_identity` 独立键集合、版本策略、类型、nullability、scope/stage/state 和有效样例；不得遗漏/拓宽字段。
3. 完整登记 run/file/attempt/worker/control/stream/span/idle ID 的宽度、作用域、ordinal 起点、分配时机、失败消耗、唯一性；列明 stream identity 前后关联行为。
4. 精确列出 full resume skip、空文件、非空 no-missing-range、retry、连接失败、首块前失败、setup early return、interfile idle 的记录形状与互斥约束。
5. 明确 append-only logger 每 run 独占、线程并发整行原子 append、单次系统调用、短写 poisoned 行为、重复 terminal/rejection、flush 到内核页缓存（不承诺 fsync）和四维状态隔离；独立结果通道暴露 evidence/write/rejection/orphan 计数。
6. 给出 uint64 JSON 原始 token 验收表及 span 配对/坏行/未知字段/schema、identity 重复/错 slot/冲突 ID、terminal 状态互斥向量。
7. 区分文档规格验收与后续实现门禁：单元/parser/故障注入、真实 tree upload/download on/off 对照、无观测改变 hash/manifest/resume/frame 语义、性能 overhead 基准；本任务全部标 NOT_RUN。
8. 将 QA-02 十项逐一映射为 PLAN-03 的明确章节/结论/后续门禁，避免以“设计已闭合”掩盖实现未验证。

## 验收

- QA-02 的 BLOCKED 项和 ARCH-REVIEW-01 要求已逐条落实；新增阶段只限已批准的 `download_mtime_finalize`。
- 事件行可以由确定性 validator 唯一接受/拒绝；没有与源码调用顺序冲突或互相矛盾的区间。
- 固定源码至当前 HEAD 的实现路径差异仍为空；不触及任何源/测试路径。
- 结果 UTF-8、无尾随空白；HEAD/index 不变；`git diff --check` 通过。
- 所有构建、测试、真实传输、性能、云端项目状态如未运行，均明确 `NOT_RUN`；不得宣称实现准入已通过。

## 后续

交付后由 00 检查结果并派 04 执行 QA-03。仅当 QA-03 明确 PASS，才创建独立 `codex/LOWLEVEL-TREE-TELEMETRY-IMPL-01` worktree 和正式实现任务。
