# LOWLEVEL-TREE-TELEMETRY-ARCH-REVIEW-02：resume 生命周期裁定复核

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.7`，`v1`
- 发起人：00 总指挥；执行角色：01 架构与需求；验收角色：00 总指挥
- 目标及理由：独立核验 00 对两类 resume skip 的生命周期裁定是否与固定源码调用顺序、append-only 记录约束及 transfer/integrity/evidence 三维映射相容；给 03 提供可直接落实到 PLAN-04 的最终映射。PLAN-03 已因固定源码冲突 BLOCKED，不作为实现输入。
- 非目标：不改源码、测试、runner、CMake、协议、resume/checksum 行为；不启动实现、构建、实验、SSH、清理；不改路线或任务板，不重写旧结果。
- 输入 commit（完整 SHA）、工作树状态：固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。项目工作树含其他协作文档修改，禁止覆盖；只新增本任务结果。
- 必读资料和证据路径：`docs/coordination/TASK_TEMPLATE.md`、`docs/coordination/BOARD.md`、`docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-lifecycle-decision.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-03-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-01-result.md`、`docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-02-result.md`；只读核对固定源码中的 `processUploadFile`、`processDownloadFile`、`controlForFile`、worker 任务分配、stream 握手和 manifest 更新调用顺序。
- 允许修改的文件/目录；禁止修改的资源：仅新增 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`。不得修改 BOARD、ROSTER、decision、旧任务结果、源码、测试或共享 index。
- 工作分支/worktree：只读主工作区资料核对；不切换分支、不建 worktree。
- 前置条件、环境占用和停止条件：只使用固定源码和已列文档；发现需要改变 00 已冻结的用户目标、schema v1 严格字段或传输行为时，停止并把冲突与可选方案写入结果，不自行拓宽路线。
- 验收标准及实际可执行命令（不要只写“所有测试”）：逐项追踪已 Completed manifest 文件在 controlForFile 前后的实际操作、校验成功/失败分支、worker/control 的可观测边界；追踪无 missing range 的 attempt 在 data_connect/SessionInit 后的状态；明确 attempt_id、attempt_kind、worker/control/stream ID、span 与状态的唯一映射；分别给出 transfer_status、integrity_status、evidence_status 的确定规则，缺证据不得判 mismatch；界定 profiling eligibility/coverage，不能把范围外的开销称为已观测。结果中列源码位置和适用/不适用向量。检查 UTF-8、无尾随空白；写前后 `git rev-parse HEAD` 一致、index 未变、`git diff --check` 通过。所有动态测试均标 `NOT_RUN`。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用；禁止 SSH/云端工作。
- 预期产物路径：`docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`

## 派给角色的消息

请续接 ROSTER 中已有的 01 架构聊天，在 `D:\Project\CPNetFlux` 读取本任务单、决策和固定源码调用路径。重点审查 00 对已完成文件 skip 的边界：`controlForFile` 先于 Completed 判定，因此若校验失败后继续传输，是否仍能如实区分校验控制操作与传输 control 阶段；不得声称“active transfer 总能从 control_acquire 开始观测”而没有源码依据。再确认 handshake 后无 missing range 的 attempt 如何独立映射 transfer、integrity、evidence。若现有严格 schema 无法表达，请给出最小、兼容且可验证的裁定建议并停止，不要直接改决策或实现。只写结果文件；记录证据来源与未运行项。

## 执行回执

- 实际输入/输出 commit：待执行。
- 实际改动：待执行。
- 实际命令、退出码与证据文件：待执行。
- transfer / integrity / evidence / wire accounting（适用时）：需分别给出 skip 分支映射；wire accounting 不适用时写明原因。
- 失败/跳过/阻塞及其原因：待执行。
- 剩余风险、未完成事项：待执行。
- 下一角色可直接执行的下一步：给出 PLAN-04 应逐项采用的事件和 eligibility 规则；任何未闭合设计返回 00。

## 验收与路线变化

验收人：00 总指挥。仅将可追溯且与固定源码一致的裁定纳入 PLAN-04；不构成实现、测试或实验通过。

若路线改变：由 00 更新 decision、BOARD 和关联任务版本；本任务不覆盖 PLAN-03 BLOCKED 证据。
