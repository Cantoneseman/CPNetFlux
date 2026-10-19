# V3-LANE-MIXED-FAULT-STORM-BUDGET-01：连续数据与控制故障预算验收

- 状态：done；顺序混合故障代理、预算断言和双云矩阵已通过。
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

## 实际解决的问题

本阶段验证了此前没有覆盖的故障先后组合：第一条数据连接断开，随后另一条 lane 的控制连接断开，最后再次出现数据连接断开。代理按新建连接顺序记录并注入 `data#2 -> control(trigger data#3) -> data#4`，runner 检查故障类型、顺序、批次身份、恢复计数和最终完整性，而不是只看退出码。

结果证明数据恢复和控制重连仍共用同一个 `--reconnect-attempts N` 总预算。N=0/1/2 分别只恢复 0/1/2 次并明确报 `reconnect attempt budget exhausted`；N=3 完成三次恢复，控制 lane 重连计数为 1、全局 control reconnect 为 0，健康 lane 没有被错误升级为整批重连，上传/下载树哈希均为 `crc32c:f6f80ad4`。没有发现需要修改生产恢复状态机的缺陷。

## 最终验收证据

- 任务计划提交：`9cd64795cfeef0148598f8de664ef36169d138f6`；顺序混合测试实现提交：`b752e7b08545a77e3d97cf91ef3f12769033e407`；断言修正提交：`3d14b5c27531ade6f9ae9982b28a5dfd4b502f69`。实现与文档均已推送，最后提交远端回读同 SHA。
- Release/TLS/Cross-domain 构建：退出码 0；复用 `/tmp/cpnetflux-runs/V3-LANE-MIXED-FAULT-STORM-BUDGET-01/build`，构建二进制用于最终矩阵。
- Python 语法和代理单测：11/11 通过；lane recovery smoke、fault proxy unit 及既有预算回归 5/5 通过。
- C++ 单元：344 项中 343 通过，1 项 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 因本构建关闭 io_uring 条件跳过。
- 顺序混合故障跨机 CTest：`cpnetflux_lane_mixed_fault_storm_budget_interop` 1/1 通过，深圳/上海服务端 × 上传/下载 × N=0/1/2/3 共 16 个 case，耗时 216.36 秒。固定证据目录：`/tmp/cpnetflux-runs/V3-LANE-MIXED-FAULT-STORM-BUDGET-01/build/cpnetflux-lane-mixed-fault-storm-budget-interop-evidence/20261019T062046Z-1792390846671280902/`。
- 证据二次核验：矩阵 `input_commit=3d14b5c27531ade6f9ae9982b28a5dfd4b502f69`；16/16 case 的 reset 顺序、唯一 batch ID、精确预算、耗尽错误和成功树哈希均满足断言。
- 清理：深圳与上海 `24600–24799` 无本任务监听或 runner/代理遗留；生产端口 `2121` 仍在监听且未停止或改配。收尾时深圳约 33 GiB、上海约 73 GiB 可用。

## 边界与下一步

本阶段只证明 plain TCP 软件层的数据/控制连续故障和共享预算；没有证明 TLS/生产认证、吞吐、production readiness、物理 NIC、local bind 或不同物理链路。下一项开发计划是继续扩展更长的控制连接故障序列和健康 lane 保持断言；物理链路仍待软件恢复路线完成后另立服务器验证任务。
