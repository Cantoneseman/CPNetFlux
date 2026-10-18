# V3-LANE-LOCAL-RECOVERY-01：单 lane 局部恢复

状态：done（代码、单元、定向 CTest、loopback 与深圳/上海双向云端故障验证完成）
日期：2026-10-18
输入提交：c571de0150037a43346cfe12050a1db783ed10f6
分支：codex/V3-LANE-LOCAL-RECOVERY-01
前置实现：V3-LANE-FAILOVER-01

## 本阶段解决的实际问题

V3-LANE-FAILOVER-01 已能回收失败 lane 的 WorkLease，但 persistent tree 的真实传输仍把任意 data TransportError 升级为整批 cancel/rebuild。单条 data socket 断开时，健康 lane 会被迫重连，已完成 range 可能重新对账和补发，摘要中的控制/数据连接计数也无法区分整批恢复与 lane-local 恢复。本阶段把故障范围收窄到发生故障的 data lane，并让重试复用同一控制会话和 batch 身份。

## 本阶段范围与非目标

本阶段实现：
1. DynamicFileQueue 提供 owner 的活动 lease 快照，只回收失败 lane 的未提交 lease。
2. PersistentDirectoryBatch 增加按 channel 的 fail/re-attach，清理失败 channel 的控制/数据 fd、活动 lease 和 ready/done 状态，保留健康 channel。
3. persistent tree sender/receiver 动态调度下不再对 transport fault 做全局 cancel；失败 lane 重新 EPSV/XDIR、复用原控制会话、递增 attempt，接收端保留 checkpoint 并回收旧 range slot。
4. callback、range slot 和 FILE_END 对 retry attempt 幂等处理，避免重复 begin/complete 被误判为协议错误。
5. 摘要增加 lane_local_recovery_scope、lane_local_recovery_count，并在 worker 完成后汇总实际 data_connect_count。
6. 新增 channel re-attach 单元覆盖，完成 TCP reset loopback 与深圳/上海双向互传验证。

非目标：不配置物理 NIC、local bind、源地址、endpoint 或不同物理路由；不据此声明生产 readiness、吞吐提升或物理链路独立性。物理路径验证留到软件恢复完成后的服务器配置阶段。

## 代码变更

- include/cpnetflux/config/file_transfer_options.h：内部 persistent lane retry generation。
- dynamic_file_queue.h/.cpp：owner 活动 lease snapshot。
- persistent_directory_batch.h/.cpp：按 channel fail/re-attach，保留健康 channel，允许 retry identity 幂等重放。
- persistent_tree_transfer.h/.cpp：传输错误只回收当前 lane；range checkpoint/slot 按 attempt 恢复。
- tree_transfer_client.cpp：lane-local retry、EPSV/XDIR/data reconnect、attempt 与 telemetry 汇总。
- control_server.cpp：DynamicDirectoryGuard 不再因单 lane transport fault 取消整批。
- tests/unit/persistent_data_session_test.cpp：失败 channel 重新 attach 后健康 range 保持提交的回归测试。

## 验收证据

资源核验（2026-10-18）：深圳根分区约 64 GiB 可用、可用内存约 2.5 GiB；上海根分区约 81 GiB 可用、可用内存约 2.8 GiB；两端均 2 CPU。临时服务使用 24600/24610（上海）和 24700/24710（深圳），未停止既有服务。

本机/深圳构建：
- cmake --build /tmp/cpnetflux-runs/V3-LANE-LOCAL-RECOVERY-01/build --parallel 2：成功。
- cpnetflux_unit_tests --gtest_brief=1：341/341 通过。
- 定向恢复过滤：53/53 通过。
- 定向 CTest（tree upload/download/resume/parallel/data-reuse/pending-window/control-reuse/unified-executor）：8/8 通过。
- token/event CTest（进程内临时 token）：2/2 通过。

loopback TCP reset：/tmp/local_fault_final.log 显示 2 个 64 MiB 文件 2/2 完成、SHA-256 一致、lane_local_recovery_count=1。本机两核和极短 loopback reset 会偶发牵连控制连接，故控制计数只作为辅助证据，云端结果作为跨机验收依据。

完整 CTest（临时 token，ctest --output-on-failure -j1）：379/382 通过。3 个环境型长连接超时：
- cpnetflux_reconnect_smoke：transfer total deadline exceeded（约 30.0 秒）。
- cpnetflux_v3_stability_matrix：TLS 组合在约 31.8 秒报告 TLS peer closed unexpectedly。
- cpnetflux_tree_pipeline_sidecar_smoke：约 30.0 秒报告 control connection closed。

深圳→上海（上海临时 server 24600，上海侧 ss -K ... sport=:24610 只断一条 data socket）：
- 正常互传：2×64 MiB，2/2，SHA-256 一致，lane_local_recovery_count=0、control_reconnect_count=0。
- 故障互传：result=pass、2/2、lane_local_recovery_count=1、data_connect_count=3、control_reconnect_count=0、resume_skipped_bytes=8060928。
- SHA：shenzhen-a.bin dbfaca2662cb70b69dfefd5ac95d1f54a73663092d46cefdc9609dc695a12c98；shenzhen-b.bin 07a1e6f3b84e57fbffcbc20ed126f43ceeaec19b8a1cdc0e63b3a75421e6dc54。

上海→深圳（深圳临时 server 24700，深圳侧 ss -K ... sport=:24710 只断一条 data socket）：
- 正常互传：2×64 MiB，2/2，SHA-256 一致，lane_local_recovery_count=0、control_reconnect_count=0。
- 故障互传：result=pass、2/2、lane_local_recovery_count=1、data_connect_count=3、control_reconnect_count=0、resume_skipped_bytes=8847360。
- SHA：shanghai-a.bin 5fd35741b8e5633dad8a247e9c7d021cbfeb521aef2ea5a819a2ca50590b18e7；shanghai-b.bin c414643d66fc63b7cfd063d0565d506a932a7860f19e0104269e1db15d346f84。

证据目录：/tmp/cpnetflux-runs/V3-LANE-LOCAL-RECOVERY-01-cross/；故障注入记录为 shanghai-reset-final.log 和 shenzhen-reset.log。临时 server、monitor 进程已停止，24600/24700 及数据端口已释放；既有服务未改动。

## 下一阶段计划

下一阶段建议 V3-LANE-RECOVERY-COVERAGE-01，解决当前仍缺的可重复软件验收问题：
1. 把 sport data-socket reset 固化成受控测试 harness，固定注入时机和目标 lane，避免 loopback 两核调度造成控制面假阳性。
2. 增加 2-lane/多 range 自动断线回归，断言健康 lane 的控制连接、epoch、已完成 range 不变，并断言失败 lane 只增加一次 attempt。
3. 补充 download 方向和 TLS/控制 pipeline 组合的 lane-local 指标断言，区分 lane_local_recovery_count 与整批 reconnect_attempts。
4. 复查完整 CTest 的 3 个 30 秒环境型超时；资源/延迟条件改善后再复跑稳定性矩阵。
5. 软件覆盖完成后，另立物理路径验证任务，才配置服务器的物理 NIC/local bind 和不同链路。

本阶段没有把物理网卡或不同物理链路当作完成条件；它们按当前路线暂缓。
