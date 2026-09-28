# PERF-TOOL-PLAN-03：按 QA-02 收窄工具规格修订

- 状态：done，等待 04 复审
- 路线版本、任务版本：`R2026-09-23.1 / v3`
- 发起人：00 总指挥；执行角色：02 实验与证据；验收角色：00，随后由 04 复审
- 目标：只处理 QA-02 对 B5–B8 中可由工具规格独立闭合的部分；把 03/01/05 不能代替的字段明确为硬依赖，不凭空批准实现。
- 非目标：不定义 01 的 timer/auth/GSI 语义，不伪造 03 的 native/effective 字段，不替 05 批准资源/归档；不改源码、runner、schema、dataset、analyzer、测试或实验，不 SSH、不构建、不清理。
- 输入 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；若 HEAD 或路线变化则停止并报告 superseded。
- 必读资料：本任务、`PERF-TOOL-PLAN-02 v2`、`PERF-TOOL-QA-02 v1`、`docs/PLAN_100M_PERFORMANCE.md`、必要的 01/03/05 回执。
- 允许修改：只追加 `docs/coordination/receipts/02-experiments.md` 的 `PERF-TOOL-PLAN-03 v3` 小节。

## 仅需修订的内容

1. B5：把 requested/effective/observed 的 validator 输入、coverage 缺失和 `verified_off` 的机械判定写成不依赖具体实现字段名的适配层；明确 03 尚未交付字段时只能 `unknown/ineligible`。
2. B6：把 auth/data-channel/expected streams 的 manifest 必填、实际证据引用和 `unmatched_config` 处理写成 validator 规则；不决定 GSI 或 TLS 等价语义。
3. B7：把 ledger 的逻辑状态、证据索引引用和 05 持久化根依赖拆开；明确没有真实 durability 证据时不得宣称通过。
4. B8：补纯规格级的 known-vector 输入/输出、非 ASCII/redaction、finite/zero/null 边界和需 04 执行的测试清单；不运行测试。

## 验收

- 逐条引用 QA-02 的 B5–B8 partial；明确保留的 01/03/05 依赖。
- 只追加 v3 回执；记录真实 HEAD/status、未运行项和 `git diff --check`。
- 不将规格修订写成代码、测试、构建或实验通过；完成后由 04 再审。

## 执行回执

- 02 已通过既有聊天 `01a0af8d-b578-7a60-b2b8-204f479ee49a` 实际执行，CLI 终态退出码 0。
- 已在 `docs/coordination/receipts/02-experiments.md` 追加 `PERF-TOOL-PLAN-03 v3`；HEAD 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，暂存区为空。
- 未运行代码、测试、构建、SSH、清理或实验。下一步由 04 复审 B5–B8。
