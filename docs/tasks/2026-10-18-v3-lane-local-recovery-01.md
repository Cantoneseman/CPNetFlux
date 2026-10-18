# V3-LANE-LOCAL-RECOVERY-01：单 lane 局部恢复

状态：planned
日期：2026-10-18
输入提交：c571de0150037a43346cfe12050a1db783ed10f6
计划分支：codex/V3-LANE-LOCAL-RECOVERY-01
前置实现：V3-LANE-FAILOVER-01

## 需要解决的实际问题

V3-LANE-FAILOVER-01 的 LinkSet/queue 测试证明失败 lane 的未完成 WorkLease 可以转交健康 lane；但深圳↔上海双向各 256 MiB 的单 data-socket 故障实测显示，每次故障仍触发 control_reconnect_count=2、data_connect_count=4。代码中 persistent tree 任一 TransportError 会进入 recordFailure/cancelAll，tryRecoveringPersistentDirectory 随后重新协商并重建整个 batch。这样虽然最终 SHA 正确，健康 lane 仍被重连，已完成工作还可能再次经历对账和补发。

## 本阶段范围

1. 将 persistent tree worker 的传输故障归属到具体 lane/linkId，而不是将单 lane 故障升级成整批失败。
2. 只 drain/disconnect 失败 lane，释放并重试其未提交 lease；保持健康 lane 的 socket、ControlClient、epoch 和已完成 range 不变。
3. 对失败 lane 做有界重连与同 linkId 的递增 epoch fencing；旧 epoch 晚到结果被拒绝；失败 lane 恢复后继续领取该 lane/队列工作。
4. 补单元与 loopback 故障注入测试，证明失败范围只重试一次、健康工作不重复。
5. 在重查深圳/上海资源与端口后，双向各传 256 MiB，仅重置一条 data session；核对 SHA、lane session 计数、control reconnect 前后值和 range attempt 记录。
6. 运行全量构建、单元及 CTest；token-auth/event-log 如需 token，只在测试进程环境生成并注入临时值，不记录其内容。

## 非目标

- 不配置物理 NIC、local bind、源地址、endpoint 或不同物理路由；该部分待本阶段代码完成后按用户要求在服务器验证。
- 不作物理链路独立性、吞吐提升、生产 readiness 或 4/8/10 lane 的声明。
- 除非实现证明必要，不改 wire protocol；不改变默认单链路兼容行为。

## 验收标准

- 可确定地注入 link0 故障时，link1 保持健康、连接标识和 epoch 不变，并继续完成已有 lease；只有 link0 未提交 lease 被回收。
- link0 新连接使用递增 epoch；旧 link0 lease 的完成结果被拒绝；已完成 range 不重复，未完成范围仅增加一次 attempt。
- loopback 与深圳→上海、上海→深圳真实传输均完成且 SHA-256 一致；每次云端只重置一个 data socket。
- 单 lane 故障前后 control_reconnect_count 不增加；data_connect_count 只增加失败 lane 的恢复连接数；summary/attempt telemetry 能区分 lane-local retry 和整批 retry。
- 相关完整 CMake build、单元测试和 CTest 通过；所有跳过/环境限制如实记录。
- 临时服务退出且测试端口释放；大 payload 与证据保留在 /tmp，不进入 GitHub。

## 预期受影响文件

- src/core/io/tree_transfer_client.cpp
- src/core/io/persistent_tree_transfer.cpp
- src/core/io/dynamic_file_queue.cpp
- include/cpnetflux/core/io/persistent_tree_transfer.h
- include/cpnetflux/core/io/dynamic_file_queue.h
- tests/unit/dynamic_file_queue_test.cpp
- tests/unit/persistent_tree_transfer_test.cpp
- docs/tasks/2026-10-18-v3-lane-local-recovery-01.md
- docs/coordination/BOARD.md
- docs/coordination/receipts/03-implementation.md

## 验收命令与证据位置

- cmake --build <isolated-build-dir> --parallel 2
- <isolated-build-dir>/cpnetflux_unit_tests --gtest_brief=1
- ctest --test-dir <isolated-build-dir> --output-on-failure
- 云端 JSON、日志、hash 与 lane fault 记录：/tmp/cpnetflux-runs/V3-LANE-LOCAL-RECOVERY-01-*/
- 任务开始前核实当前 origin、深圳/上海可用磁盘与内存、测试端口和服务状态；完成后记录服务/端口已停止核验。
