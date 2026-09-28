# LOWLEVEL-TREE-TELEMETRY-PLAN-06：补齐规格验收向量与性能门

- 状态：ready
- 路线版本、任务版本：R2026-09-24.9 / v1
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：04 测试与质量
- 目标及理由：针对 QA-05 的四项明确阻塞，补齐 PLAN-05 的机械可验收内容，使实现角色能按固定向量开发和验证。
- 非目标：不改源码、测试、runner、decision、BOARD/ROSTER；不创建实现 worktree；不构建、不 SSH、不运行实验。
- 输入 commit：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 必读资料：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-05-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-05-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md`、`docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`。
- 允许修改：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-06-result.md`，不得修改 PLAN-05 或共享文档。
- 停止条件：输入文件变化或无法以完整 JSONL 字节、明确枚举/null 矩阵、可重复故障注入和固定样本门限闭合时，标 BLOCKED。
- 预期产物：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-06-result.md`

## 必须补齐的四项

1. **完整 schema 矩阵**：为 run/file_attempt/stream/worker 四 scope 与每个 stage/state 列 required/null/forbidden 字段、reason 枚举、attempt/ID 规则；覆盖 attempt-0 decision/Changed finalize、normal/no-range/empty/failure/retry/idle。
2. **完整 golden JSONL**：给出带固定键顺序、UTF-8、LF 的完整有效字节 fixture，至少包含 upload normal、download late stream ID 的 start→identity→paired terminal、payload_io N/A、idle continuous/tail/cancel/error、same-control retry 两 attempts、attempt-0 两例外；每个 fixture 有 validator 期望结果和反例。
3. **logger 故障注入**：冻结 start write、terminal write、O_APPEND/poison、summary/stderr write 四个注入点、注入方式、退出码、保留文件和五状态轴断言；只允许 evidence 变化。
4. **on/off 与性能门**：固定 loopback tree upload/download、fresh/no-range/resume 代表 case、方向、数据集、重复数、随机交错顺序；定义 hash/manifest/DATA frame/wire 等价性、失败排除和 overhead wall median/p95、CPU/GiB、JSONL bytes/GiB 阈值。不能只写“以后比较”。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 阻塞/未运行项：
- 下一步：
