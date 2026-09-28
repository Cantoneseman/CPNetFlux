# LOWLEVEL-TREE-TELEMETRY-PLAN-01：目录阶段 telemetry 契约与插点设计

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：04 测试与质量，最终由 00 收口
- 目标及理由：目录固定基线已证明 file-parallelism 变化可重复影响 wall，但现有 event log 的逐文件 elapsed 为 0、时间戳为秒级且缺 transfer_id，不能判断建连、首 payload、payload I/O、complete、manifest 或 inter-file idle 的因果成本。本任务把这些边界收敛成可直接实现和测试的最小契约。
- 非目标：不改源码、测试、runner、schema、协议帧、manifest/resume 语义、默认并行度或 scheduler；不实现 lookahead/preconnect；不 SSH、不构建、不运行传输或 GridFTP；不改旧证据、BOARD/ROSTER 或 Git index。
- 输入 commit（完整 SHA）、工作树状态：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；共享工作树只作资料读取，不能把当前文档 HEAD 当作源码输入。
- 必读资料和证据路径：本任务；`docs/tasks/2026-09-23-lowlevel-tree-stage-profile-00-supplement.md`；`docs/tasks/2026-09-23-lowlevel-tree-stage-profile-01-result.md`；`docs/tasks/2026-09-23-lowlevel-design-01-result.md`；`docs/tasks/2026-09-23-lowlevel-design-qa-01-result.md`；`docs/tasks/2026-09-23-dir-perf-source-01-result.md`；`docs/DECISIONS/2026-09-17-directory-data-plane-profiling.md`；相关 tree/file/event-log 源码。
- 允许修改的文件/目录；禁止修改的资源：只新增 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-01-result.md` 和 `...-last-message.md`；禁止写入源码 worktree、云端、历史 GridFlux-Beta 树、`/root/projects/CPSS(DCC)`、旧证据或共享 index。
- 工作分支/worktree：无代码 worktree；本任务为只读设计。若发现必须改变公开 API、协议或现有 summary 语义，停止并记录为后续架构决策，不自行改写。
- 前置条件、环境占用和停止条件：确认任务版本仍为 `R2026-09-23.4/v1`；任何输入资料缺失、源码与 `3b0820d` 不一致或角色会话无法只读工作时，报告 BLOCKED，不沿旧任务继续。
- 验收标准及实际可执行命令（不要只写“所有测试”）：结果必须逐项覆盖 upload/download 的 control_acquire、control_prepare、data_connect、first_payload、payload_io、transfer_complete_wait、manifest_finalize、inter-file idle；给出 run/file/attempt/stream/worker/transfer 关联键、同进程 monotonic clock 单位和事件顺序；明确空文件、单 payload、全部 resume 跳过、首块前失败、重试和并发重叠的 `null`/not_applicable/未完成语义；说明跨主机不能直接相减、并发 sum 不等于 wall。只允许 `rg`/定向源码读取和文档门禁，结果中记录实际命令、退出码与未运行的 build/test。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用；本任务不得 SSH 或创建运行目录。
- 预期产物路径：`docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-01-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-01-last-message.md`。

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务单、`BOARD.md` 和列出的固定证据。只做源码/文档定向读取，提交一份实现前设计：明确事件插入函数/文件、已有时钟与日志 API 的缺口、字段类型与关联方式、upload/download 对称性、空/resume/失败/重试/并发规则、向后兼容边界、最小单元/链路测试清单，以及实现后如何验证观测开关不改变传输字节和 hash。不要改代码，不要把 lookahead 或 manifest 优化写入本任务结论。完成后仅写结果文件和 CLI 摘要。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- transfer / integrity / evidence / wire accounting（适用时）：
- 失败/跳过/阻塞及其原因：
- 剩余风险、未完成事项：
- 下一角色可直接执行的下一步：

## 验收与路线变化

验收人、结论和依据：

若路线改变：记录替代任务、停止点、需保留的证据，不能覆写旧实验结果。
