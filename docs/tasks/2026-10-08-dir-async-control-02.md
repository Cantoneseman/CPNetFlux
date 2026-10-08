# DIR-ASYNC-CONTROL-02：目录控制通道有界异步调度

- 状态：in_progress；路线版本：R2026-10-08.1；任务版本：v1。
- 目标：在明确启用 `--control-pipeline-depth=1` 时，让客户端在当前文件的数据阶段并行发送下一文件的控制命令；服务端可继续接收该命令并异步运行文件数据任务。一个 pipeline lane 仅保留一个当前任务和一个 lookahead；控制连接数不得超过普通 worker 基线。
- 固定输入提交：`0a171f0776535f7434ddf29d16faca93e9095b87`（origin/codex/DIR-CONTROL-ASYNC-01，含已验证的 EPSV+STOR/RETR 命令批处理）。
- 工作区：`/tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-02/src`；分支：`codex/DIR-ASYNC-CONTROL-02`。
- 允许修改：`src/protocol/control/control_server.cpp`、`src/core/io/tree_transfer_client.cpp`、必要的私有头文件、对应 control/pipeline 单元测试、`tools/test/run_gridftp_tree_pipeline_sidecar_smoke.py`、本任务及结果文档、手动实验说明/脚本。超出范围先记录原因并审查。
- 非目标：不改数据帧、manifest/resume/checksum 语义、压缩/scheduler/IO backend、默认同步路径、外部 GridFTP；本轮不运行跨域性能矩阵、不宣称达到 GridFTP 90%。
- 安全边界：不修改 live root 或其他 worktree，不接触 GridFlux-Beta、CPSS(DCC)、上海或用户实验进程。构建和短时 loopback 验证前检查深圳活动进程与空间；若发现运行中的 CPNetFlux 性能实验，停止动态验证并只交源码/测试与命令。
- 协议设计：opt-in 后仍由一个控制循环独占控制连接读写（含 TLS）；每个 pipeline lane 上限为当前 transfer + 一个 queued transfer。数据工作在线程中执行，完成结果经有界通知队列回到控制循环；完成回复携带 transfer_id。输入命令按到达顺序处理，终态按真实完成顺序发出。断开控制连接时取消并 join 所有数据任务。同步模式保持原行为。
- 客户端设计：pipeline lane 只使用一个控制连接；lookahead 线程可发送命令但不读取控制回复。当前数据阶段结束后由唯一控制读取方按 transfer_id 收取当前终态并缓存 lookahead 的 EPSV/元数据/150 回复。下载 lookahead 仍核对远端 size/mtime；失败必须关闭该 lane 并让未连接的数据任务取消。多文件 worker 模式不得额外创建 pipeline-only 控制连接。
- 验收：
  1. 单测覆盖 transfer-id 回复匹配、lookahead 回复缓存/恢复、拒绝超出窗口和失败响应；既有 depth=0 控制语义不变。
  2. loopback integration 覆盖 opt-in 同一控制连接在首个传输数据未开始时可接受第二个 EPSV+STOR/RETR、pending command reject、EOF/cancel 有界退出；默认未 opt-in 时保持同步。
  3. tree upload/download depth=0/1、file_parallelism=1/8 验证退出码、manifest/file SHA-256、sidecar 和控制连接数；depth=1 不比同 worker 基线多建控制连接。
  4. 本任务内的构建、定向 CTest、故障 smoke 全记录实际命令与退出码；若活动实验/资源阻止动态验证，明确 blocked-validation，不宣称通过。
  5. 提供匹配的 CPNetFlux/GridFTP 手动对比脚本和精确启动命令。脚本记录提交/二进制 SHA-256、环境/配置/seed/方向/重复、逐 case 完整性及性能比；不由本任务自动启动长实验。达到每项 >=90% 是后续实验验收标准，不作为未执行结果。
- 输出：代码/测试、手动实验入口、`docs/tasks/2026-10-08-dir-async-control-02-result.md`。只提交已审查的明确文件，推送 GitHub 后回读远端 SHA。
