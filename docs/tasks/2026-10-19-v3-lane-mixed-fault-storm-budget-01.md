# V3-LANE-MIXED-FAULT-STORM-BUDGET-01：连续数据与控制故障预算验收

- 状态：in progress；先登记范围，再扩展测试代理/runner 并做双云矩阵。
- 路线：R2026-10-16-V3-UNIFIED-MULTILINK；输入提交 `61cfe1b712aa2b4ae2b3f695d6d52028974b5d2d`。
- 执行位置：深圳现有 CPNetFlux worktree `/root/projects/CPNetFlux-worktrees/v3-lane-mixed-fault-budget-01`，由当前总指挥单人串行执行；上海只作传输对端。

## 易懂版问题

刚完成的连续故障测试只连续断开数据连接。控制连接走另一条恢复路径，所以还需要验证“数据断开 → 控制断开 → 再次数据断开”时，它们仍一起使用同一个重试次数，而不是各自重新开始计数。恢复控制连接时，还要确认没有把健康 lane 一起重连，且原批次身份保持不变。

## 范围与验收标准

- 扩展 fault proxy，以确定顺序注入数据连接 2 reset、数据连接 3 所属控制连接 reset、数据连接 4 reset；后一项只有前一项发生后才触发，不使用同时故障门控。
- 跨深圳/上海服务端、上传/下载和 N=0/1/2/3，覆盖 16 个 case。
- N=0：只注入第一数据故障，恢复 0 次，清楚报告预算耗尽。
- N=1：注入数据 reset 与随后控制 reset，恢复恰 1 次，第二次故障后清楚失败。
- N=2：再注入第三个数据 reset，恢复恰 2 次后清楚失败。
- N=3：三次故障均恢复并成功，最终树哈希一致，恢复预算恰 3。
- 每个 case 检查 reset 类型/顺序/时间、`recovery_attempts_used`、lane-local 与控制重连计数、错误摘要及最终完整性。预算至少允许控制恢复时，检查故障 lane 的重连沿用同一 batch ID，健康 lane 控制连接不重建且已完成文件不重做。
- 若全部通过，不改生产恢复代码；若失败，先定位为代理/断言缺陷或真实行为问题，只有红测实证生产缺陷后才扩大修改范围。
- 完成 Python 代理测试与语法、Release/TLS 构建、相关单测、lane recovery smoke、跨机矩阵、已有预算回归；记录固定提交、命令结果、证据路径、资源和清理状态。

## 非目标

不改调度策略或提升生产 readiness，不测吞吐、生产认证或物理链路，不配置物理 NIC/local bind，不触碰历史 GridFlux-Beta、CPSS(DCC)、其他 worktree 或深圳根目录未提交内容。

## 允许修改文件

- `docs/tasks/2026-10-19-v3-lane-mixed-fault-storm-budget-01.md`
- `docs/coordination/BOARD.md`
- `tools/test/cpnetflux_fault_proxy.py`
- `tools/test/run_cpnetflux_lane_recovery_interop.py`
- `tools/test/test_cpnetflux_fault_proxy.py`
- `CMakeLists.txt`
- 只有确认生产行为有缺陷并先更新本任务单时，才可改 `src/core/io/tree_transfer_client.cpp` 及必要测试。

## 环境与验收命令

- 证据：`/tmp/cpnetflux-runs/V3-LANE-MIXED-FAULT-STORM-BUDGET-01/`。
- 启动前重核两端磁盘、内存、24600–24799 端口和本任务进程；保持每端至少 10 GiB 可用。生产端口 2121 仅只读核验。
- `python3 -m py_compile tools/test/cpnetflux_fault_proxy.py tools/test/run_cpnetflux_lane_recovery_interop.py tools/test/test_cpnetflux_fault_proxy.py`
- `python3 tools/test/test_cpnetflux_fault_proxy.py`
- Release/TLS 构建、`cpnetflux_unit_tests`、`cpnetflux_lane_recovery_smoke`、`cpnetflux_fault_proxy_unit`、新增 16-case 跨机 CTest 和已有 N=0/1/2 预算回归。

## 后续边界

此阶段完成后，根据证据继续软件恢复边界，物理 NIC、local bind 和不同物理链路仍只在软件恢复开发完成后另立服务器验证任务。
