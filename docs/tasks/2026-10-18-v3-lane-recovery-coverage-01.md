# V3-LANE-RECOVERY-COVERAGE-01：可重复的 lane-local 恢复覆盖

- 状态：done
- 路线版本、任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-RECOVERY-COVERAGE-01
- 发起人：00 总指挥；执行角色：00 总指挥；验收角色：当前轮次无独立 04 回执
- 目标及实际问题：上一阶段已经证明单 data socket reset 可以在深圳—上海互传中局部恢复，但故障注入仍依赖一次性手工场景，覆盖不足以保护 2 lane、多 range、下载和 TLS+control pipeline 组合。本阶段把该故障固化为可重复 harness，并验证失败 lane 与健康 lane 的边界。
- 非目标：不配置或宣称验证物理 NIC、local bind、源地址、交换路径或不同物理链路；不把 2 CPU/30 秒长连接环境型超时自动改判为代码缺陷；不引入生产 readiness 或吞吐结论。
- 输入 commit（完整 SHA）、工作树状态：bd3a1b02d3dc4d16346dd8bf30d455274c295fc7；新 worktree 创建时干净。
- 必读资料和证据路径：docs/tasks/2026-10-18-v3-lane-local-recovery-01.md；tools/test/cpnetflux_fault_proxy.py；tools/test/run_cpnetflux_reconnect_smoke.py；/tmp/cpnetflux-runs/V3-LANE-LOCAL-RECOVERY-01-cross/（历史原始证据，只读）。
- 实际修改文件：tools/test/cpnetflux_fault_proxy.py、tools/test/run_cpnetflux_lane_recovery_smoke.py、CMakeLists.txt、include/cpnetflux/core/io/persistent_directory_batch.h、src/core/io/persistent_directory_batch.cpp、tests/unit/persistent_data_session_test.cpp、docs/tasks/2026-10-18-v3-lane-recovery-coverage-01.md、docs/coordination/BOARD.md。

## 实际完成的开发

1. 故障代理支持按 data connection 定位目标 lane，只对目标连接执行一次 reset，并输出 connection、target、转发字节和 reset 事件，保持旧调用方式兼容。
2. 新增三场景 smoke harness：plain upload、plain download、TLS + control pipeline upload。每场景使用 2 lane、多 range、2 个文件并行度，断开一次目标 data connection，检查树 hash、lane-local recovery、连接次数、控制连接是否复用以及两个 lane 的 range 完成证据。
3. 修复 persistent directory batch 的失败 lane 生命周期竞态：失败 channel 重新挂接前，健康 lane 的完成不再被全局 30 秒等待阻塞；最终重试 lane 仍负责全局完整性检查。新增单元测试覆盖健康 lane 先完成、失败 lane 后重挂接和最终完成。
4. 将 harness 注册为 `cpnetflux_lane_recovery_smoke` CTest，便于后续回归。

## 验收命令与证据

运行根目录：`/tmp/cpnetflux-runs/V3-LANE-RECOVERY-COVERAGE-01`；证据目录：`/tmp/cpnetflux-runs/V3-LANE-RECOVERY-COVERAGE-01/evidence2`。

- `cmake --build /tmp/cpnetflux-runs/V3-LANE-RECOVERY-COVERAGE-01/build --parallel 2`：退出码 0。
- `cpnetflux_unit_tests --gtest_filter='PersistentDirectoryBatchTest.*' --gtest_brief=1`：7/7 通过。
- `cpnetflux_unit_tests --gtest_brief=1`：342 个测试、53 个 suite，全部通过。
- `python3 tools/test/run_cpnetflux_lane_recovery_smoke.py --build-dir ... --evidence-dir .../evidence2`：3/3 通过（upload、download、TLS+pipeline upload）。三场景均记录 `lane_local_recovery_count=1`、`control_reconnect_count=0`、`data_connect_count=3`、两个非零 `persistent_lane_range_completed`，源/目的树摘要一致，目标 lane 恰好一次 reset。
- `ctest --test-dir /tmp/cpnetflux-runs/V3-LANE-RECOVERY-COVERAGE-01/build -R cpnetflux_lane_recovery_smoke --output-on-failure`：1/1 通过，6.42 秒。
- 全量 `ctest --test-dir .../build --output-on-failure -j1`：382/384 通过。剩余失败为已有的 `cpnetflux_reconnect_smoke`（transfer total deadline exceeded，30 秒）和 `cpnetflux_v3_stability_matrix`（TLS peer closed unexpectedly，30 秒）；本阶段不把它们改判为 lane-local 回归。`cpnetflux_tree_pipeline_sidecar_smoke` 已通过。

## 结果边界与交接

- transfer：三种 loopback 故障场景均完成；没有在本阶段启动新的跨机传输。
- integrity：每场景树 hash/SHA 一致；两个 lane 均有 range 完成记录。
- evidence：JSON summary、proxy JSONL、client/server 日志和 TLS 证书保留在 evidence2；payload 不入 Git。
- wire accounting：代理记录目标连接和 reset 位置，摘要记录 data connection、lane recovery、控制重连和 range 完成计数；本阶段不形成吞吐结论。
- 物理 NIC/local bind/不同物理链路验证按用户要求暂缓，必须等软件侧恢复覆盖完成后单独安排。

## 下一阶段：V3-LANE-RECOVERY-INTEROP-01

需要解决的问题是：当前恢复语义只在深圳 loopback 的可重复 harness 中验收，尚未证明经过真实深圳—上海链路、双向方向和现有资源约束时仍保持 lane-local 边界。下一阶段内容：

1. 重新只读核验深圳/上海空间、CPU、内存、端口和既有服务，使用独立临时目录与端口。
2. 从本分支固定提交构建，在深圳→上海、上海→深圳各执行 plain upload/download 中至少一组目标 lane reset，保留源/目的 SHA、proxy/服务日志和 JSON summary。
3. 对照本阶段断言：目标 data lane 恰好一次恢复，健康 lane 不掉线，控制连接不重连，两个 lane 都完成 range；失败按资源/链路/协议/完整性分层。
4. 仅在跨机软件恢复证据完成后，再另立物理 NIC/local bind/不同物理链路验证任务；不把逻辑 lane 数量等同于物理链路数量。

下一角色可直接从本分支和上述 evidence2 复用 harness；不要恢复大 payload，不要触碰历史 GridFlux-Beta 或 CPSS(DCC)。
