# V3-LANE-RECOVERY-INTEROP-01：深圳—上海跨机恢复互操作

- 状态：验收通过，待提交与远端回读
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-RECOVERY-INTEROP-01
- 深圳 `gridflux-beta-shenzhen` 是权威构建端；上海 `gridflux-beta-shanghai` 是传输对端。
- 输入提交：`92bc148d2d0aeacdd0cb021abba918018af40699`（V3-LANE-RECOVERY-COVERAGE-01）
- worktree/分支：`/root/projects/CPNetFlux-worktrees/v3-lane-recovery-interop-01` / `codex/V3-LANE-RECOVERY-INTEROP-01`
- 代码提交与 GitHub 回读：待提交后补记。

## 要解决的问题

通俗地说，前面只证明了“同一台机器里坏一条数据通道可以只修它自己”；本任务确认经过真实深圳—上海网络后，两个方向也仍然只修复故障通道。

首次实测揭示了一个 30 秒等待问题：健康通道完成后，服务端清理连接会抹掉它的“已完成”状态；失败通道重连时，服务端误以为健康通道也要重新上线，随后整批重连。代码中 `unbind()` 清除了 `done`，而 `waitReady()` 只把 `ready` 当作可继续状态。现在清理时保留健康通道完成状态，等待逻辑接受已完成通道；旧连接迟到的清理也不能解绑新连接。另将数据集从 32 MiB 增至 64 MiB，因为 32 MiB 恰好未超过切段阈值，无法覆盖多段传输。

## 范围、非目标与影响文件

- 包含：持久批次状态修复、旧连接清理保护、单元回归、可重复跨机故障代理/runner、CTest 入口、双向数据通道 reset 验收。
- 不包含：物理 NIC、local bind、源地址/交换路径配置、TLS/生产认证互操作、吞吐或生产 readiness 结论。
- 不改既有 GridFTP 服务；不碰历史目录或 `/root/projects/CPSS(DCC)`；实验只使用 `/tmp/cpnetflux-runs/V3-LANE-RECOVERY-INTEROP-01`。
- 修改文件：`include/cpnetflux/core/io/persistent_directory_batch.h`、`src/core/io/persistent_directory_batch.cpp`、`src/protocol/control/control_server.cpp`、`tests/unit/persistent_data_session_test.cpp`、`tools/test/cpnetflux_fault_proxy.py`、`tools/test/run_cpnetflux_lane_recovery_interop.py`、`CMakeLists.txt`、本任务单及 `docs/coordination/BOARD.md`。

## 验收命令与结果

- 深圳构建目录：`/tmp/cpnetflux-runs/V3-LANE-RECOVERY-INTEROP-01/build`；Release、TLS/io_uring 开关开启、测试开启、桌面端关闭、跨机 CTest 显式开启。
- `cmake --build ... --target cpnetflux-gridftp-server cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux_unit_tests --parallel 2`：退出码 0。
- `cpnetflux_unit_tests --gtest_filter='PersistentDirectoryBatchTest.*' --gtest_brief=1`：7/7 通过。
- `ctest --test-dir .../build -R '^cpnetflux_lane_recovery_smoke$' --output-on-failure`：1/1 通过，6.43 秒。
- `ctest --test-dir .../build -R '^cpnetflux_lane_recovery_interop$' --output-on-failure`：1/1 通过，36.25 秒；提交后再跑并保存与提交 SHA 对应的结果。
- 双向命令：`CPNETFLUX_INPUT_COMMIT=92bc148d2d0aeacdd0cb021abba918018af40699 python3 tools/test/run_cpnetflux_lane_recovery_interop.py --build-dir /tmp/cpnetflux-runs/V3-LANE-RECOVERY-INTEROP-01/build --evidence-dir /tmp/cpnetflux-runs/V3-LANE-RECOVERY-INTEROP-01/bidirectional-final1 --server-side both`：两方向均通过。
- 深圳→上海、上海→深圳均为：源/目的 tree SHA 相等；3 个文件、67,230,715 字节；代理只 reset 目标 data connection 一次；`lane_local_recovery_count=1`、`control_reconnect_count=0`、`data_connect_count=3`、`persistent_lane_range_completed=[2,2]`。
- 双向证据：`/tmp/cpnetflux-runs/V3-LANE-RECOVERY-INTEROP-01/bidirectional-final1/matrix.json` 及各方向子目录中的 summary、proxy JSONL、server/client 日志和接收树。Tree SHA：`5816ca45fdc866157f2c8c9d0097009af11a3132eee66cd2fcdd745ab696c092`。
- CTest 证据：`/tmp/cpnetflux-runs/V3-LANE-RECOVERY-INTEROP-01/build/cpnetflux-lane-recovery-interop-evidence/matrix.json`。
- 实验前：深圳可用盘 62 GiB、可用内存约 2.5 GiB、2 CPU；上海可用盘 80 GiB、可用内存约 2.8 GiB、2 CPU。既有 2121/22310/2811 服务保持不动。
- 实验后：两端无本任务 server/proxy/client 残留进程，24600/24610 与 24700/24710 测试端口释放。

## 结果边界与下一步

这证明两台云服务器之间 plain TCP 单数据连接中断后的 lane-local 恢复；不证明物理链路隔离、控制连接中断后的局部恢复、TLS/认证互操作、性能提升或生产 readiness。

下一项软件开发计划：`V3-LANE-CONTROL-RECOVERY-01`。目前 data socket 中断且 control socket 仍活着时可以局部恢复；如果 lane 的 control socket 也断开，客户端会升级为整批控制重连，影响健康 lane。下一阶段要只为故障 lane 新建 control session，复用同一批次身份恢复未完成数据，并定向注入 control 连接故障验证健康 lane 和已完成数据保持不变。物理 NIC/local bind/不同物理链路验证继续等软件侧恢复能力完成后另立任务。
