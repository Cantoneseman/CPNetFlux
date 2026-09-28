# PERF-TOOL-PLAN-02：按质量审查修订 runner 规格

- 状态：done
- 路线版本、任务版本：`R2026-09-23.1 / v2`
- 发起人：00 总指挥；执行角色：02 实验与证据；验收角色：00 总指挥，随后由 04 复审
- 目标：只修订 `PERF-TOOL-PLAN-01 v1` 的工具规格，闭合 04 `PERF-TOOL-QA-01 v1` 指出的可由 02 负责的歧义：confirmation manifest 按实际晋升格生成、失败/资源/preflight/evidence/cleanup 后的停止状态、attempt/retry 关联、wire accounting 与 `performance_eligible` 的独立关系、compression off 的 effective 观测要求、run-level durable ledger。
- 非目标：不定义 01 负责的 timer/auth/data-TLS/GSI/effective verify 契约；不改源码、runner/schema/dataset/analyze/test；不运行 Python/CMake/CTest、preflight、SSH、构建、实验或清理；不修改 PLAN、BOARD、ROSTER 或其他回执；不操作 Git index、不提交。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；执行前实时核对并保留共享文档改动。
- 必读资料：本任务单、`docs/tasks/2026-09-23-perf-tool-plan-01.md`、02 回执中的 `PERF-TOOL-PLAN-01 v1`、04 回执中的 `PERF-TOOL-QA-01 v1`、`docs/PLAN_100M_PERFORMANCE.md`。
- 允许修改：只允许追加 `docs/coordination/receipts/02-experiments.md` 的 `PERF-TOOL-PLAN-02 v2` 小节。
- 验收：修订必须逐项引用 QA blocker；明确 selected confirmation 格点如何生成 expected count 和不可变 manifest；定义 attempt/retry 不污染 block；定义 transfer/integrity/evidence/wire/config/timer/cleanup/performance eligibility 的独立判定；明确 compression off 的 requested/effective/observed evidence；定义 run-level ledger 在 case cleanup 前持久化、回读、hash 和批次停止状态；对 01/03/04/05 依赖保持显式。执行 `git rev-parse HEAD`、`git status --short --branch`、定向只读检查、`git diff --check`；不运行性能工具。
- 停止条件：路线/HEAD 变化则报告 superseded；不能替 01 冻结事件或替 03 提供 native 字段。
- 预期产物：仅追加 02 回执。

## 派给角色的消息

请续接 ROSTER 中既有 02 聊天。读取本任务和 `PERF-TOOL-QA-01 v1`，只追加 v2 规格修订回执；逐项解决可由 02 决定的六个 blocker，保留 01/03/04/05 依赖。不改代码、不运行测试/实验/SSH、不修改其他文档。

## 执行回执

- 实际输入/输出 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，HEAD 未变。
- 实际改动：02 仅追加 `docs/coordination/receipts/02-experiments.md` 的 `PERF-TOOL-PLAN-02 v2`；未改源码、测试、任务板或 Git index。
- 实际命令、退出码与证据文件：真实既有 02 聊天经 `codex exec resume` 执行，最终 exit 0；复核 `git rev-parse HEAD`、status、暂存区、定向内容和 `git diff --check`，均通过（仅有换行提示）。证据见 02 回执 v2 小节。
- QA blocker 对应修订：已覆盖 B1–B8，包括按 `K` 生成 `5K/15K` confirmation、逐类 STOPPED 状态、wire 与 `performance_eligible` 解耦、单 attempt、compression off effective 观测、GridFTP 认证/通道字段、hash-chained run ledger 和 golden vector/timer 边界。
- 仍依赖 01/03/04/05 的事项：01 的 timer/auth/data-channel/GSI/effective verify，03 的 native 字段，04 的 v2 独立复审，05 的资源/归档/服务证据仍未完成。
- 失败/阻塞及其原因：本任务无文档门禁失败；代码、测试、SSH、构建、实验和清理均按非目标未执行。上海可用空间为 0，且 01 既有聊天的 active-writer 仍未释放。
- 下一角色可直接执行的下一步：04 续接 `PERF-TOOL-QA-02`，仅复审本节 B1–B8 及其可执行性，不启动实现或实验。

## 验收与路线变化

00 验收，04 复审；本任务不授权实现或实验。若与 01 后续 decision 冲突，保留旧回执、标记冲突并停止依赖工作。
