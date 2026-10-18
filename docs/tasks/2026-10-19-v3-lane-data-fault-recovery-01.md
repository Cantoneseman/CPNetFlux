# V3-LANE-DATA-FAULT-RECOVERY-01：数据连接断开后的快速恢复

- 状态：planned
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-DATA-FAULT-RECOVERY-01
- 权威开发端：深圳 gridflux-beta-shenzhen；上海仅作传输对端。
- 输入提交：0cc1696dffda995b078516ffece8a7e47834cbd3（V3-LANE-RECOVERY-BUDGET-01 实现提交）。
- 实现 worktree/分支：/root/projects/CPNetFlux-worktrees/v3-lane-data-fault-recovery-01 / codex/V3-LANE-DATA-FAULT-RECOVERY-01。

## 要解决的问题（易懂版）

当中间网络设备只切断文件数据连接、控制连接还保持时，客户端有时没有及时收敛到“继续恢复”或“明确失败”。Global scheduler 的本地复现会等满 30 秒传输 deadline 才报 transfer total deadline exceeded。用预算阶段输入提交之前的旧二进制也能复现，因此这是既有缺口。用户会看到传输卡住半分钟，且不知道恢复次数是否已经用完。

## 范围、非目标与验收标准

- 先对两版代码重跑相同数据链路 reset，保留命令、summary、proxy log 和耗时，避免把环境故障归因给最近改动。
- 追踪数据 socket 错误如何传到 sender/receiver、DynamicFileQueue、lane 完成状态和控制 FINAL/DRAIN；找出导致 deadline 前不退出或不恢复的具体等待点。
- 修复最小的状态通知/等待路径；不更改 lane selection、调度策略或传输协议，除非证据说明它们是必要原因。
- 上传和下载都要有可重复的 data-only reset 测试；用时明显小于 30 秒；恢复成功时树 SHA 完全一致，预算消耗不超过配置值；预算耗尽时错误明确并快速返回。
- 基线与修改后同环境对比，确认新行为由改动产生。

不包含物理 NIC/local bind、源地址/路由/交换机验证、多物理链路、生产 TLS/认证 readiness、吞吐优化、全量生产 readiness 声明。

## 建议的 TDD 顺序

1. 将 reconnect smoke 的最小上传 reset 用例拆出为独立证据运行，增加下载对应场景并固定 timeout 与输出字段；在旧输入提交确认红测。
2. 根据阻塞栈/线程等待和事件时间线定位停滞层，添加能区分 data 与 control 断开的日志/断言。
3. 先跑失败测试，再修改必要的唤醒、终止或 lane 状态收敛逻辑；补回归检查 N=0/N=1 预算。
4. 运行构建、342 个单元、lane recovery smoke、budget CTests 和新双云上传/下载故障矩阵。

## 预计受影响文件

- src/core/io/tree_transfer_client.cpp
- src/core/io/dynamic_file_queue.cpp（若证据确认队列重试/等待是原因）
- tools/test/cpnetflux_fault_proxy.py
- tools/test/run_cpnetflux_reconnect_smoke.py 或新增数据故障互操作 runner
- CMakeLists.txt
- docs/DIRECTORY_TRANSFER.md
- 本任务单与 docs/coordination/BOARD.md

## 输出证据

完成时记录输入与结果 commit、测试命令和退出状态、两端资源及端口检查、基线/修改后 elapsed 与错误、树 SHA 和预算计数、GitHub push 与远端 SHA 回读、尚未覆盖的边界和下一步交接。
