# PERF-TOOL-PLAN-01：Runner 改造文件级规格

- 状态：done（02 已交付规格；未运行实验或测试）
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：02 实验与证据；验收角色：00 总指挥，后续由 04 独立审查
- 目标及理由：依据 PERF-QA-01 的实际 blocker 和 PERF-MATRIX-01 的设计，形成 runner/证据工具的文件级改造规格，使未来可以精确运行单文件及目录 100 Mbps 匹配矩阵，并避免 hash、timer、cleanup 或配置不匹配污染结果。
- 非目标：本轮不修改 runner/schema/test，不创建 case manifest，不执行 dry-run/preflight/传输/性能 case，不 SSH、不构建、不清理云端，不操作 BOARD/ROSTER/其他角色回执，不操作 Git index、不提交。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。开始执行时必须实时读取 HEAD/status；当前工作区含 00 的 plan/BOARD/task 文档和 02/03/04/05 回执改动，不能覆盖或暂存。
- 必读资料和证据路径：本任务、`docs/PLAN_100M_PERFORMANCE.md`、`docs/coordination/BOARD.md`、02 回执中的 PERF-MATRIX-01、04 回执中的 PERF-QA-01、05 回执中的 ENV-01；定向读取 `tools/experiments/gridftp_compare/{runner.py,schemas.py,preflight.py,dataset.py,test_gridftp_compare.py}` 和相关 smoke/证据保存逻辑。
- 允许修改的文件/目录；禁止修改的资源：只允许追加 `docs/coordination/receipts/02-experiments.md` 中 PERF-TOOL-PLAN-01 v1 小节。禁止修改计划、任务单、BOARD/ROSTER、其他角色回执、源码、runner、tests、历史证据或云端文件。
- 工作分支/worktree：只读规格任务；不建 worktree、不切分支、不操作共享 index。
- 前置条件、环境占用和停止条件：PERF-QA-01 已判定现矩阵不可直接执行；01 的 timer、auth/data-TLS、GSI 方向/并流、none/CRC effective verify 契约尚未交付。对这些项目只列出明确输入/阻塞及候选方案，不替 01 定义。若当前 HEAD/路线版本不同或任务被 superseded，停止并报告。
- 验收标准及实际可执行命令（不要只写“所有测试”）：规格须列出 runner/schema/dataset/preflight/test 的候选文件白名单；定义显式 case manifest 最小字段、配对 block/arm order、生成与验证方法、GridFTP 实际配置和流数证据、单文件 file SHA 与目录 canonical tree hash、transfer timer 应消费的边界字段、准备/hash/log/cleanup 排除规则、证据先持久化与 cleanup 返回码门禁、失败/blocked/unmatched/evidence partial 分类、回归测试入口及两阶段矩阵数量核算。每项标 `02 可决定` 或 `依赖 01/03/04`。完成后执行 `git rev-parse HEAD`、`git status --short --branch`、定向 `rg`/只读代码检查、`git diff --check`，并确认只追加本角色回执；不运行任何性能工具。
- 云端运行目录、端口、资源上限、清理与归档方案：不适用；禁止 SSH。ENV-01 记录上海 0 可用且无清理授权，不得据此规划立即执行。
- 预期产物路径：仅追加 `docs/coordination/receipts/02-experiments.md` 中 PERF-TOOL-PLAN-01 v1 小节。

## 派给角色的消息

请续接 ROSTER 中既有 02 聊天。读取本任务和 `docs/PLAN_100M_PERFORMANCE.md`，核对输入 HEAD/status 后，只写 runner/证据工具的文件级改造规格。PERF-QA-01 的 blocker 是审查事实；01 尚未冻结的 timer、校验与 auth/data-channel 语义须标作依赖，不要自行决定或修改代码。只追加自己的回执并通知 00；不运行 preflight、dry-run 或实验。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 文件白名单及改造点：
- 由 02 决定与必须等待 01/03/04 的事项：
- 失败/阻塞及其原因：
- 下一角色可直接执行的下一步：

## 验收与路线变化

00 总指挥验收，04 独立审查规格。此任务不批准修改 runner 或启动实验；若规格与 01 决策冲突，记录 superseded 点并停止依赖工作。
