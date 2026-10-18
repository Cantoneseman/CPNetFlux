# V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01：下载方向控制连接故障恢复

- 状态：in_progress
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01
- 权威开发端：深圳 `gridflux-beta-shenzhen`；上海仅作传输对端。
- 输入提交：`0f9bf8a28f9df2cfb0567bea998b5a930f9a8732`（控制连接恢复阶段的实现和验收记录）。
- worktree/分支：`/root/projects/CPNetFlux-worktrees/v3-lane-control-recovery-download-01` / `codex/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01`。

## 要解决的问题（易懂版）

上一阶段已经证明上传时，某条 lane 的控制连接断开可以只修复那条 lane。下载方向的代码会走相同的恢复入口，但下载还多了一层风险：接收端可能已经把部分 range 写入磁盘，重新接回时如果检查点、文件状态或 range 身份处理不一致，可能重复写入、漏掉剩余数据，或者让健康 lane 跟着重做。

本阶段先把这个风险变成双向、可重复的跨云测试，再根据测试结果补齐下载专用状态处理。目标是让下载控制连接故障时也能沿用原 batch ID、lane 编号和检查点，只继续未完成工作。

## 范围、非目标与验收标准

- 包含：下载方向的控制连接 RST 故障注入；深圳服务端和上海服务端双向运行；检查下载源/目的树 SHA-256、文件大小和文件数量；核对原 batch/lane 重挂接；核对已完成 range 不重复、未完成 range 能续传、健康 lane 不进入 retry。
- 允许修改：下载恢复状态机、控制/检查点统计、故障代理、跨云 runner、CTest、传输文档。
- 不包含：物理 NIC、local bind、源地址/路由/交换路径；TLS/生产认证互操作；吞吐和生产 readiness；把本阶段扩大成调度器重写。

验收最低标准：两方向均在定向控制 RST 后完成下载；源/目的树摘要一致；`lane_local_recovery_count=1`、`lane_control_reconnect_count=1`、`control_reconnect_count=0`、`reconnect_attempts=0`；总控制/数据连接只增加故障 lane 对应的一条；健康 lane 没有 retry attempt；下载端的文件与 range 统计显示没有重复或遗漏。

## 开发前检查

- 先检查深圳、上海的磁盘、可用内存、CPU、保留服务和测试端口；保留的 `2121` 以及两端既有 GridFTP/CPNetFlux 服务不得修改。
- 先用当前输入提交运行一次下载控制故障用例；如果现状已通过，先把结果记录为覆盖缺口，再决定是否需要实现改动，不能为了制造差异修改正确行为。
- 先写失败或缺失的自动化断言，再修改生产代码；实验输出只写 `/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01`，不把 payload 带入 Git。

## 预计受影响文件

- `tools/test/run_cpnetflux_lane_recovery_interop.py`
- `CMakeLists.txt`
- `src/core/io/tree_transfer_client.cpp`（仅当下载测试发现状态缺口）
- `tools/test/cpnetflux_fault_proxy.py`（仅当下载方向需要补充故障映射）
- `docs/DIRECTORY_TRANSFER.md`
- 本任务单与 `docs/coordination/BOARD.md`

## 验收产物

完成时补充：基线/红测或现状覆盖证据、实际改动文件、构建和 CTest 命令、双向下载矩阵 JSON、树 SHA、range/attempt 统计、资源和端口清理结果、代码与文档 commit SHA、GitHub push 及 `git ls-remote` 回读、未覆盖边界和下一步交接。