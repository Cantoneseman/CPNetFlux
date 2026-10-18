# V3-LANE-CONTROL-RECOVERY-01：控制连接断开后的单 lane 恢复

- 状态：in_progress
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-CONTROL-RECOVERY-01
- 权威开发端：深圳 `gridflux-beta-shenzhen`；上海仅作传输对端。
- 输入提交：`5aa0396d3b47bb2fc6d6d403b28ced62b2cd98ab`（V3-LANE-RECOVERY-INTEROP-01 文档验收提交）。
- worktree/分支：`/root/projects/CPNetFlux-worktrees/v3-lane-control-recovery-01` / `codex/V3-LANE-CONTROL-RECOVERY-01`。

## 要解决的问题（易懂版）

目前一条 lane 的数据连接断了、控制连接还活着时，客户端只修这条 lane。若这条 lane 的控制连接也断了，客户端无法再通过它发起续传，于是升级为整批重连，健康 lane 也被打断。此任务让客户端只给故障 lane 建立新的控制连接，重新协商后用原来的 batch ID 和 lane 编号挂回同一批传输；其它 lane 保持运行，已有完成的数据不重传。

## 范围、非目标与验收标准

- 包含：动态 persistent tree 的 lane-local control reconnect；沿用同一批次身份、channel/lane 和已有检查点；局部恢复失败时保留整批恢复作为兜底；单独统计 lane 控制重连；添加控制连接 RST 故障注入和可重复验收入口。
- 不包含：物理 NIC、local bind、源地址/路由/交换路径配置；TLS/生产认证互操作；吞吐/生产 readiness 结论；重写调度器或服务端批次协议。
- 云端验收：深圳与上海双向各运行一次定向控制连接断开；SHA/tree manifest 一致；故障 lane 恢复次数为 1；lane 控制连接重建次数为 1；批次级 `control_reconnect_count=0` 且 `reconnect_attempts=0`；总控制连接数只增加 1；数据连接总数只增加 1；健康 lane 的完成/range 状态保留。
- 自动化验收：先添加会失败的控制连接故障用例并观察基线失败；构建受影响目标；运行相关单元测试、lane recovery smoke 和新的跨机 CTest；保留输入提交、命令、日志、JSON 摘要和 tree hash。
- 实验前重新检查深圳/上海磁盘、内存、CPU、测试端口和保留服务；仅在 `/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-01` 写实验产物。实验后检查临时进程和端口均已释放。

## 预计受影响文件

- `src/core/io/tree_transfer_client.cpp`
- `tools/test/cpnetflux_fault_proxy.py`
- `tools/test/run_cpnetflux_lane_recovery_interop.py`
- `tests/unit/persistent_data_session_test.cpp`（若新状态需独立协议状态回归）
- `CMakeLists.txt`
- `docs/DIRECTORY_TRANSFER.md`
- 本任务单与 `docs/coordination/BOARD.md`

## 最终证据与交接

完成时补充代码和文档 commit SHA、GitHub push 及 `ls-remote` 回读、构建/CTest 命令、双向故障矩阵证据路径、实际摘要字段和限制。若测试或线路条件不满足，明确记录失败和可复现证据，不把部分通过描述为完整验收。