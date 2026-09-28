# LOWLEVEL-TREE-TELEMETRY-IMPL-01：目录阶段 telemetry 窄实现

- 状态：ready
- 路线版本、任务版本：R2026-09-24.9 / v1
- 发起人：00 总指挥；执行角色：03 核心实现；独立审查：04 测试与质量；验收角色：00 总指挥
- 目标及理由：按已通过 QA-07 的契约，在 tree/file 上传下载关键路径加入可验证的目录阶段 telemetry，使后续能定位单文件与多文件目录的控制、数据连接、首 payload、payload、完成等待、manifest finalize 和 interfile idle 成本，再依据证据做底层优化。
- 非目标：不把 telemetry 当性能优化；不改 wire protocol、framed data、manifest/resume/checksum 语义、scheduler/compression 默认值、IO backend、runner 统计语义或旧事件字段；不启动云端构建、SSH、真实跨域实验。
- 输入 commit（完整 SHA）、工作树状态：源码固定输入 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；当前资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；工作树文档 dirty，源码相对固定输入无差异，执行前必须复核。
- 必读资料和证据路径：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-07-result.md`（QA-07 复核 SHA-256 `2DEDDF4FECB60C221CB47428014F9550386FCF7DFF848E2B7EB282DFA3BF8EEF`）、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-07-result.md`（SHA-256 `39891B0D168166FCBC21C81009804AB788B76B39D259BDF8DEEFE705324DF7FE`）、`docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-06-result.md`、`docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`、`docs/DECISIONS/2026-09-23-lowlevel-design.md`、`docs/DECISIONS/2026-09-23-100m-evidence-driven-optimization.md`、`src/core/io/tree_transfer_client.cpp`、`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_client.cpp`、`include/cpnetflux/core/metrics/event_log.h`、`src/core/metrics/event_log.cpp`、现有 metrics/manifest 相关测试。
- 允许修改文件/目录：
  - `src/core/io/tree_transfer_client.cpp`
  - `src/core/io/file_transfer_client.cpp`
  - `src/core/io/file_download_client.cpp`
  - 与 recorder/JSON serializer 直接对应的窄范围 `include/cpnetflux/core/...`、`src/core/metrics/...`
  - 专属 parser/validator、golden fixture、logger fault-injection 单测及明确的 CMake 测试注册
- 禁止修改：协议帧/握手格式、manifest/resume/checksum、scheduler/compression、IO backend、实验 runner/旧 schema、云端目录和共享 BOARD/ROSTER。
- 工作分支/worktree：必须从当前工作区创建独立 `codex/LOWLEVEL-TREE-TELEMETRY-IMPL-01` worktree；不得在 `D:\Project\CPNetFlux` 共享目录切分支或操作 index。
- 前置条件、停止条件：执行前确认 PLAN-07/QA-07 输入 SHA 与任务单、HEAD、源码基线稳定；若变化停止。任何无法保持旧传输结果、hash、manifest/resume 或协议行为的改动停止并报告。先完成 parser/validator 与 fault injection，再进入 loopback；不得把建议范围扩展为 lookahead 或 sendv。
- 验收命令及顺序：
  1. 专属 raw-token parser/golden：6 blocks/44 rows、26/18 键集、span_id=0 reject、重复键/未知键/整数词法。
  2. paired lifecycle/ID validator：late stream ID、same-control retry、attempt-0、idle continuous/tail/cancel/error。
  3. logger 四类注入：`start_write`、`terminal_write`、`append_poison`、`summary_write`，确认原始传输状态不被 evidence 错误改写。
  4. mixed old/new consumer、loopback tree upload/download fresh/no-range/resume_partial。
  5. telemetry on/off 等价性：退出码、hash、manifest、DATA frame/wire accounting、resume 结果一致。
  6. overhead gate：fresh/resume_partial 每 cell 10 对，至少 8 对 valid；wall median ≤5%、p95 ≤10%；CPU/GiB median ≤5%、p95 ≤10%；JSONL on median ≤4 MiB/GiB、p95 ≤8 MiB/GiB。no_range 只作握手/control overhead，不作 payload 吞吐结论。
  - 实际使用的 CMake/CTest 命令、退出码、日志和 skipped/blocked 原因必须写入实现回执；未运行不得写通过。
- 云端运行目录、端口、资源上限、清理与归档方案：本任务不使用云端；loopback 仅使用独立本地构建目录和临时目录，不能污染共享工作树。
- 预期产物：独立 worktree 的代码/测试变更、实现回执 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-impl-01-result.md`，以及完整测试日志路径。

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务单、BOARD、PLAN-07 和 QA-07，核对版本及固定输入后创建独立 worktree。先实现最小 recorder/schema/serializer 与 validator，再接入已有 tree/file 插点；保留旧事件消费者兼容性。每一步只修改白名单，禁止顺带优化协议或目录调度。完成后写回执并通知 00；04 随后独立审查代码和动态门禁。若发现契约与固定源码仍有冲突，停止并报告具体行号。

## 执行回执

- 实际输入/输出 commit：
- worktree/分支与实际改动：
- 实际命令、退出码与证据文件：
- transfer / integrity / evidence / wire accounting：
- 失败/跳过/阻塞及其原因：
- 性能与开销门：
- 剩余风险、未完成事项：
- 下一角色可直接执行的下一步：

## 验收与路线变化

验收人、结论和依据：

若路线改变：记录替代任务、停止点、需保留的证据，不能覆写旧实验结果。
