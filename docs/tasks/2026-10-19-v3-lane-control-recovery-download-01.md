# V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01：下载方向控制连接故障恢复

- 状态：done
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01
- 权威开发端：深圳 `gridflux-beta-shenzhen`；上海仅作传输对端。
- 输入提交：`0f9bf8a28f9df2cfb0567bea998b5a930f9a8732`。
- worktree/分支：`/root/projects/CPNetFlux-worktrees/v3-lane-control-recovery-download-01` / `codex/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01`。
- 规划提交：`3d1977ea5b3b642c68fd6e75c5d7e81f259653cd`，已推送并由 `git ls-remote` 回读。
- 实现提交：`e8e3985925c5f8c1f494617fc3dd1da9d3dbf741`，已推送并由 `git ls-remote` 回读。

## 解决的实际问题（易懂版）

上一阶段已经证明上传时，某条 lane 的控制连接断开可以只修复那条 lane。本阶段把同一件事补到下载方向，并修正了验收脚本本身的两个实际问题：当上海是下载客户端时，脚本没有回收上海上的结果；重复运行还会留下旧的下载 manifest，导致下一次被误判为“重复 file ID”。

现在下载验收会在每个 case 开始前清理对应远端目录，测试结束时从实际下载客户端回收结果，再计算源/目的树摘要。这样测到的是本次传输的真实结果，不会被旧清单或错误的本地路径污染。

## 范围、非目标与验收标准

- 包含：下载方向的控制连接 RST 故障注入；深圳服务端和上海服务端双向运行；检查下载源/目的树 SHA-256、文件大小和文件数量；核对原 batch/lane 重挂接；核对已完成 range 不重复、未完成 range 能续传、健康 lane 不进入 retry；独立 CTest 入口。
- 不包含：物理 NIC、local bind、源地址/路由/交换路径；TLS/生产认证互操作；吞吐和生产 readiness；重写调度器或服务端批次协议。

## 先暴露问题、再修复验收入口

初次运行下载方向时，客户端日志显示传输完成，但 runner 先错误地对不存在的本地目录计算空树；修正结果回收后，第二个方向又复用了旧的远端 download manifest，客户端报 `duplicate or out-of-range persistent file ID`。清理远端 case、正确回收远端客户端结果后，当前输入版本在干净目录上完成了真正的双向控制故障验收。两处修复都属于测试入口，不改变生产传输状态机。

## 实际改动

- `tools/test/run_cpnetflux_lane_recovery_interop.py`：增加 `--transfer upload|download`；下载时按客户端所在端回收结果；每个远端 case 开始前清理远端目录；默认使用本阶段独立的证据/远端目录。
- `CMakeLists.txt`：新增 `cpnetflux_lane_control_recovery_download_interop`，复用同一控制 RST runner 执行双向下载。
- 没有修改 `src/` 生产代码，因为干净目录测试证明上一阶段的控制 lane 恢复逻辑已经适用于下载。

## 验收命令与结果

配置和构建：

```text
cmake -S /root/projects/CPNetFlux-worktrees/v3-lane-control-recovery-download-01 -B /tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01/build -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_DESKTOP=OFF -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_CROSS_DOMAIN_TESTS=ON -DCPNETFLUX_ENABLE_IO_URING=ON -DCPNETFLUX_ENABLE_TLS=ON
cmake --build /tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01/build --target cpnetflux-gridftp-server cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux_unit_tests --parallel 2
```

配置和构建均退出码 0。下载 CTest：

```text
CPNETFLUX_INPUT_COMMIT=0f9bf8a28f9df2cfb0567bea998b5a930f9a8732 ctest --test-dir /tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01/build -R '^cpnetflux_lane_control_recovery_download_interop$' --output-on-failure
```

结果 1/1 通过，用时 34.04 秒；上一阶段上传控制恢复 CTest 在同一输入上回归 1/1、33.79 秒；完整 `cpnetflux_unit_tests --gtest_brief=1` 为 342/342 通过。

最终下载矩阵：

`/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01/build/cpnetflux-lane-control-recovery-download-interop-evidence/20261018T174234Z-1792345354963629285/matrix.json`

深圳服务端和上海服务端两方向都得到同一树 SHA-256：`5816ca45fdc866157f2c8c9d0097009af11a3132eee66cd2fcdd745ab696c092`；3 个文件、67,230,715 字节；`lane_local_recovery_count=1`、`lane_control_reconnect_count=1`、`control_reconnect_count=0`、`reconnect_attempts=0`、`control_connect_count=3`、`data_connect_count=3`。两方向的四个大文件 range 均有完成记录，分配可能是 `[2,2]`、`[1,3]` 等，但健康 lane 的文件 `attempt` 均为 0，故障 lane 重挂接沿用同一 batch ID 和 lane 编号；下载端实际产生的 manifest 和文件均来自本次干净 case。

实验前后资源和端口已复核：深圳约 60 GiB、上海约 80 GiB 可用磁盘；均有约 2.5 GiB 以上可用内存和 2 个 CPU；测试端口已释放；深圳 `:2121`/`:22310`、上海 `:2121`/`:2811` 既有服务保持运行。实验输出仅写入 `/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01`，未进入 Git。

## 下一步计划：V3-LANE-RECOVERY-BUDGET-01

现在上传和下载都能只修故障 lane，但还要解决“重试次数是否真的受限”的问题。当前代码分别给 lane-local 重连和整批重连使用同一个 `--reconnect-attempts N`，极端情况下可能先消耗 N 次局部尝试，再额外消耗 N 次整批尝试；这与项目决策中“显式 N 次约束整个过程”的要求不一致。

下一阶段用故障代理连续注入多次断链，先写一个会失败的预算断言，再把局部和整批恢复接到同一个全局计数/截止时间：

1. `N=1` 时，无论故障发生在 lane 内还是整批边界，最多只发生一次恢复动作。
2. 预算耗尽后返回明确原因，不偷偷升级成更多重连。
3. `N=0` 的默认行为保持不自动重连；上传、下载和双向跨云统计都如实记录已使用预算。

物理 NIC、local bind 和不同物理链路仍按用户要求后置。