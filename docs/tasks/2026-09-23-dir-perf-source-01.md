# DIR-PERF-SOURCE-01：目录串行路径的源码级优化选题

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.3 / v1`
- 发起人：00 总指挥；执行角色：03 核心实现；复核：00，后续由 04 独立审查实现任务
- 目标及理由：结合 HIST-FORENSIC-01 v2 中 dense/mixed 目录并发敏感证据，追踪当前代码的逐文件控制交互、数据连接、worker 排队和完成等待，提出一个可测、影响面受控的底层/架构优化候选。
- 非目标：不直接实现、不修改源码/测试，不构建/运行 smoke 或性能实验，不 SSH、不触碰云端、不改 runner、协议 wire format、默认安全/校验行为；不把 dirty 历史实现当当前源代码。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；执行前实查 HEAD/status/index，只读共享工作树。
- 必读资料和证据路径：本任务；`docs/tasks/2026-09-23-hist-forensic-01-result.md`（仅作为带限制的历史线索）；`docs/coordination/receipts/03-implementation.md`；`src/core/io/tree_transfer_client.cpp`、`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_client.cpp`、相应 control protocol/client 源码和树传输测试。
- 允许修改的文件/目录；禁止修改的资源：仅新增 `docs/tasks/2026-09-23-dir-perf-source-01-result.md`；不修改其他共享文件、角色回执、源码、测试、旧证据或云端资源。
- 工作分支/worktree：静态调查，不建分支/worktree。
- 前置条件、环境占用和停止条件：只根据当前固定 HEAD 源码作判断；若具体慢区间没有阶段数据支撑，将其写作假设并给出最小下一步观测，不将猜测包装成优化结论。安全配置 mismatch 的历史矩阵不可当正式性能证明。
- 验收标准及实际可执行命令（不要只写“所有测试”）：画清 tree worker 从选取 file 到完成/排队释放的调用顺序，区分 control reuse 命中、每文件数据连接、每文件协议往返和 native I/O；引用精确源文件/函数；指出低并行吞吐低、高并行接近链路上限支持什么/不支持什么；提供一个优先实现候选，说明预计改善目录的哪条路径、正确性/崩溃恢复/公平性风险、独立 C++ 测试与 smoke 命令；若无法选出实现，列最小 profiling task。不得建议纯靠提高默认并行度来伪装底层优化。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用；禁止 SSH。
- 预期产物路径：完整源码映射正文写入 `docs/tasks/2026-09-23-dir-perf-source-01-result.md`；CLI `--output-last-message` 使用独立 `docs/tasks/2026-09-23-dir-perf-source-01-last-message.md`，避免末条短摘要覆盖正文。

## 派给角色的消息

续接 ROSTER 中既有 03 聊天，独立读取当前源码并对照历史限制。只读分析，不开始实现。结果要能由总指挥据此决定是否下发一个独立 worktree 的窄实现任务，或者先派 profiling。完整分析写结果文件，不要将结果文件传给 `--output-last-message`；CLI 短摘要另写 last-message 文件。完成后回传实际终态和可执行的下一步建议。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 调用路径与候选：
- 失败/跳过/阻塞及其原因：
- 剩余风险、未完成事项：
- 下一角色可直接执行的下一步：

## 验收与路线变化

验收人、结论和依据：

若路线改变：记录替代任务、停止点、需保留的证据，不能覆写旧实验结果。
