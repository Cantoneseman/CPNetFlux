# LOWLEVEL-TREE-TELEMETRY-PLAN-02

状态：ready，路线 `R2026-09-23.5`
执行角色：03 核心实现；独立审查：04 测试与质量
固定输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`

## 目标

在不改源码的前提下，修订 PLAN-01 为可机械审查的目录阶段 telemetry v2 设计。唯一规范来源是 `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`。结果必须能直接指导后续实现与测试，不能继续保留互相冲突的旧口径。

## 非目标

不实现 telemetry，不修改旧决策正文、源码、测试、runner、BOARD、ROSTER 或共享 Git index；不创建 worktree，不构建，不 SSH，不做性能实验，不做 lookahead、manifest 优化或协议语义改变。

## 输入资料

- `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-01-result.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-01-result.md`
- `docs/DECISIONS/2026-09-17-directory-data-plane-profiling.md`
- PLAN-01 已定位的 tree/file transfer 源码与 `tools/demo/run_alpha_demo.py`

## 输出与验收

只允许写：

- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02-result.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02-last-message.md`（若 CLI 能生成）

结果必须明确列出 v1→v2 的差异，并逐项冻结：

1. 撤销 `first_payload <= payload_io` 通用不等式，给出两个独立区间和合法慢首块示例。
2. 固定 `data_channel_finalize`、`transfer_complete_wait`、`manifest_finalize` 的边界、重叠和禁止相加规则。
3. 规定 append-only start/end 生命周期、崩溃时 incomplete/evidence_gap、写失败对四维状态的隔离。
4. 给出 run/file/attempt/worker/control/stream/transfer/path 的 required/null matrix。
5. 固定空文件、完整 resume skip、重试 attempt、首块前失败及并发多 stream 的 ID/state 语义。
6. 固定下载 stream ID 在 SessionInit 后追加绑定的生命周期。
7. 固定 uint64 纳秒解析、非负和 elapsed 一致性验证规则。
8. 固定每条新行 `error_code` 与旧消费者混合日志兼容方式。

结果只做文档/路径门禁，报告是否读取了输入、源基线、未解决的 01/04 复核项；不声称实现、测试或实验通过。

## 派发正文

读取本任务、本路线决策、PLAN-01 result 和 QA-01 result 后执行。不要重抄 PLAN-01 全文；直接交付 v2 规则与 v1→v2 差异。仅写本任务列出的 result/summary 文件，不改源码、测试、旧决策、BOARD/ROSTER、worktree 或 index。完成后说明剩余的 01 架构复核和 QA-02 闸门，并停止。
