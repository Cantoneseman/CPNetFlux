# V3-LANE-FAULT-STORM-BUDGET-01：连续 lane 故障下重试预算验收

- 状态：done；连续故障代理、预算断言和跨机矩阵均已通过。
- 路线：R2026-10-16-V3-UNIFIED-MULTILINK；输入代码提交 `c76033331c9ad52349f220cffab4cb462e994642`（V3-LANE-MIXED-FAULT-BUDGET-01）。
- 执行位置：深圳既有 worktree `/root/projects/CPNetFlux-worktrees/v3-lane-mixed-fault-budget-01`；上海只作传输对端。深圳根目录存在另一项 V3 实现的未提交改动，依 AGENTS 单人串行例外保护。

## 易懂版问题

此前已验证两条数据通道几乎同时出错，以及数据通道和控制通道同时出错时，重试次数都不会超过设置值。但还没有验证“修好一次后又坏、再修好后又坏”这种连续过程。这个任务会按顺序制造最多 3 次数据通道断开，检查 `--reconnect-attempts N` 是否真是整个传输共用的上限：N=0 时不恢复；N=1 时恢复一次后第二次断开应明确失败；N=2 时恢复两次后第三次断开应明确失败；N=3 时三次都应恢复并完成传输。

## 范围与验收标准

- 扩展现有 CPNetFlux fault proxy 和跨机 runner，支持按新建立的数据连接顺序依次注入 3 次 reset，不等待多个连接同时就绪。
- 覆盖深圳/上海作服务端 × 上传/下载 × N=0/1/2/3，共 16 个用例。
- 每个 case 从代理日志证明 reset 按连接序号递增、时间严格递增；预算 0/1/2 时分别实际注入 1/2/3 次 reset，恢复次数精确等于 N 并清楚报告预算耗尽；N=3 时 3 次 reset 和 3 次恢复后成功，最终目录哈希与源一致。
- 测试同时核对 `recovery_attempts_used`、`lane_local_recovery_count`、错误摘要和连接重建数；不能只依据进程退出码。
- 先运行新测试与当前版本得到行为结果。若预算计数和失败摘要均正确，不改生产状态机；只有行为红测复现缺陷时，才先更新任务单并做最小生产代码修复。
- 运行 Python 语法/代理测试、Release+TLS 构建、完整 C++ 单元测试、lane recovery smoke、故障代理测试、新增 16-case 跨机 CTest，以及与本改动相关的恢复预算回归。记录每条命令退出码、case summary、日志目录和两端资源前后状态。
- 完成后停止本任务启动的代理/服务，确认端口 `24600–24799` 无遗留监听且生产端口 `2121` 仍正常监听。

## 非目标

不改变 lane 调度或恢复架构，除非红测证明确有错误；不做吞吐优化、TLS/生产认证互操作或生产 readiness 结论；不配置物理 NIC、local bind 或不同物理链路；不触碰历史 `/root/projects/GridFlux-Beta`、`/root/projects/CPSS(DCC)`、其他 worktree 或深圳根目录现有 dirty 改动。

## 允许修改文件

- `docs/tasks/2026-10-19-v3-lane-fault-storm-budget-01.md`
- `docs/coordination/BOARD.md`
- `tools/test/cpnetflux_fault_proxy.py`
- `tools/test/run_cpnetflux_lane_recovery_interop.py`
- `tools/test/test_cpnetflux_fault_proxy.py`
- `CMakeLists.txt`
- 仅在行为红测证实时，先补记范围后允许 `src/core/io/tree_transfer_client.cpp` 和必要的行为测试。

## 环境与命令

- 实验目录：`/tmp/cpnetflux-runs/V3-LANE-FAULT-STORM-BUDGET-01/`，由现有 mixed-fault 构建配置派生并重新记录本轮代码身份。
- 两端每轮启动前检查 `/`、`/tmp` 至少留 10 GiB、内存、测试端口、已有任务进程；生产端口仅只读检查。
- Python：`python3 -m py_compile tools/test/cpnetflux_fault_proxy.py tools/test/run_cpnetflux_lane_recovery_interop.py tools/test/test_cpnetflux_fault_proxy.py`；`python3 tools/test/test_cpnetflux_fault_proxy.py`。
- CTest：`ctest --test-dir /tmp/cpnetflux-runs/V3-LANE-FAULT-STORM-BUDGET-01/build --output-on-failure -R 'cpnetflux_fault_proxy_unit|cpnetflux_lane_recovery_smoke|cpnetflux_lane_fault_storm_budget_interop'`，再运行任务范围内的共享预算回归。

## 下一步交接

## 实际解决的问题

本阶段补上了此前缺失的“连续故障”验证：第一条数据连接恢复后，第二条连接再次断开，恢复后第三条连接再次断开。代理现在按新建立的数据连接顺序只重置下一条目标连接，日志带有 `sequence`、数据连接编号和时间；runner 不再只看退出码，而是检查每次 reset、预算消耗、恢复计数、错误摘要和最终树校验。

结果证明 `--reconnect-attempts N` 在连续故障下仍是整个传输共用的严格上限。N=0/1/2 时分别发生 1/2/3 次断开，实际恢复 0/1/2 次后明确报告 `reconnect attempt budget exhausted`；N=3 时三次断开和三次恢复全部完成，上传和下载树哈希均为 `crc32c:f6f80ad4`。因此没有发现需要修改生产恢复状态机的缺陷。

## 最终验收证据

- 计划提交：`4fb97205badec7cedf5bb159369fd13190b0177c`；实现与测试提交：`19a449868e1650b75214a9db59c692b71ee7f272`；均已推送，后者 `git ls-remote` 回读同 SHA。
- Release/TLS/Cross-domain 配置与构建：退出码 0；服务端 SHA-256 `3c49230d1290c6aa6bcfe15e1cb7bd479e030c0d26a795b30c1704af69d1fb98`，上传客户端 `afd66df74853e920d3226079a41cccea963ace941b8888ee13dd9c1658829190`，下载客户端 `e08221936020d524dfd01056889b048fef7981da5e40d0e67dc5588377e76f7c`。
- Python `py_compile` 和代理单元测试：9/9 通过；lane recovery smoke 与 fault proxy CTest：2/2 通过。
- C++ 单元：344 项中 343 通过，1 项 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 因本构建关闭 io_uring 条件跳过。
- 既有共享预算回归：`cpnetflux_lane_recovery_budget_interop`、`cpnetflux_lane_recovery_budget_two_interop`、`cpnetflux_zero_recovery_budget_interop` 3/3 通过。
- 连续故障跨机 CTest：`cpnetflux_lane_fault_storm_budget_interop` 1/1 通过，16 个 case（深圳/上海服务端 × 上传/下载 × N=0/1/2/3），耗时约 219.68 秒。固定证据目录：`/tmp/cpnetflux-runs/V3-LANE-FAULT-STORM-BUDGET-01/build/cpnetflux-lane-fault-storm-budget-interop-evidence/20261019T055050Z-1792389050194128943/`。
- 清理：两端 `24600–24799` 无本任务监听或 runner/代理遗留；生产端口 `2121` 仍在监听且未停止或改配。最终深圳约 38 GiB、上海约 75 GiB 可用。

## 边界与下一步

本阶段只证明 plain TCP 软件层连续 data-lane 故障和共享预算；没有证明 TLS/生产认证、吞吐、production readiness、物理 NIC、local bind 或不同物理链路。下一项开发计划为扩展控制连接的连续故障覆盖，继续验证健康 lane 和 batch 身份保持；物理链路仍待软件故障恢复路线完成后单独安排。
