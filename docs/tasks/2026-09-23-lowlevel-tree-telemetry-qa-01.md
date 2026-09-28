# LOWLEVEL-TREE-TELEMETRY-QA-01：telemetry 契约独立质量复审

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：04 测试与质量；验收角色：00 总指挥
- 目标及理由：独立复审 03 的 `LOWLEVEL-TREE-TELEMETRY-PLAN-01` 结果，确认它是否足够作为后续最小 instrumentation 实现的输入，提前发现阶段定义或兼容边界中的不可验收项。
- 非目标：不改源码、测试、runner、schema、决策、BOARD/ROSTER 或旧证据；不构建、不运行 CTest/smoke/传输/profile；不 SSH、不访问云端、不创建 worktree、不提交 Git。
- 输入 commit（完整 SHA）、工作树状态：源码基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；共享文档 HEAD 仅作审计记录。任务只读，不要求干净工作树。
- 必读资料和证据路径：本任务；`docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-01-result.md`；`docs/tasks/2026-09-23-lowlevel-tree-stage-profile-00-supplement.md`；`docs/tasks/2026-09-23-lowlevel-design-qa-01-result.md`；`docs/tasks/2026-09-23-dir-perf-source-01-result.md`；`src/core/io/tree_transfer_client.cpp`；`src/core/io/file_transfer_client.cpp`；`src/core/io/file_download_client.cpp`；`include/cpnetflux/core/metrics/event_log.h`；`src/core/metrics/event_log.cpp`；`include/cpnetflux/core/metrics/transfer_phase_stats.h`。
- 允许修改的文件/目录；禁止修改的资源：只新增 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-01-result.md` 和 `...-last-message.md`；禁止写源码、测试、任务板、角色回执、云端、历史 GridFlux-Beta 树、`/root/projects/CPSS(DCC)` 或 Git index。
- 工作分支/worktree：无；只读复审。
- 前置条件、环境占用和停止条件：任务版本仍为 `R2026-09-23.4/v1` 且 03 结果可读；若契约仍依赖未定义的 01 决策，须标为 blocker/partial，不擅自批准实现。
- 验收标准及实际可执行命令：逐项给出 PASS/PARTIAL/BLOCKED，覆盖：事件 schema/version 与 JSON null；steady clock 与跨主机边界；run/file/attempt/worker/stream/control/transfer 关联；upload/download 阶段起止；226 与 data-channel/local finalize 嵌套；空文件、单 payload、resume 全跳过、首块失败、retry、并发重叠、日志丢失；旧 event 行兼容；telemetry on/off 不改变 transfer/integrity/wire 的验收方法。记录实际 `rg`/定向读取、文档门禁和未运行的 build/test。
- 云端运行目录、端口、资源上限、清理与归档方案：不适用，本任务不得 SSH。
- 预期产物路径：`docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-01-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-01-last-message.md`。

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务单、BOARD 和 03 的契约结果，进行独立只读复审。不要重复整篇报告；以缺口和验收门为主。结论必须明确“是否允许 03 创建 telemetry 实现任务”，若不允许，列出最小修订项；若允许，保留仍需 01/00 冻结的字段。不要运行构建、测试、SSH 或云端实验；只写结果文件和 CLI 摘要，并如实记录工具/CLI 限制。

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
