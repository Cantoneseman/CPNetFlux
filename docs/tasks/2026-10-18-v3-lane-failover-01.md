# V3-LANE-FAILOVER-01：LinkSet 运行负载记账与队列级 lane 故障接管

状态：done
日期：2026-10-18
输入提交：2a7615991a396e7d2856ab47920eda208c90d826
工作区：/root/projects/CPNetFlux-worktrees/v3-multilink-range-01
分支：codex/V3-LANE-FAILOVER-01

## 本阶段解决的实际问题

V3-FLOWCUT-01 已将 GlobalScheduler 连接到逻辑 LinkSet，但 DynamicFileQueue 的活跃 WorkLease 字节只进入旧 LinkManager，LinkSet 的 inflightBytes 不随 claim/commit/retry 更新，调度器因此无法按实际负载分配 lane。本阶段将 LinkSet accounting 接入动态 claim、incoming claim、完成、拒绝、终态失败、重试、恢复及取消；回归测试验证了负载释放和未完成工作切换到健康 lane。断开的旧 lease 不能提交，lane 恢复后使用递增 epoch。

## 本轮范围与验收

- 同步 queue lease 和 LinkSet inflight accounting；保留 LinkManager 兼容视图。
- 测试低负载 lane 选择、断开 lane 的 unfinished work 接管、旧 lease fencing、恢复 epoch，以及取消时释放 LinkSet lease。
- 运行全量构建、单元测试、CTest；在两台阿里云服务器双向传输并对每次传输只重置一个 data session。
- 非目标：物理 NIC、local bind、不同物理路径配置和性能提升验证；wire protocol 改动；多 lane 性能矩阵。

## 修改文件

- include/cpnetflux/core/io/dynamic_file_queue.h
- src/core/io/dynamic_file_queue.cpp
- tests/unit/dynamic_file_queue_test.cpp
- 本任务记录、任务板、实现回执和下一阶段任务单

## 验收结果

- TDD 红灯阶段新增的负载/故障接管测试在实现前按预期失败，日志：/tmp/cpnetflux-runs/V3-LANE-FAILOVER-01-red.log。
- 最终全量 CMake 构建通过：/tmp/cpnetflux-runs/V3-LANE-FAILOVER-01-build-final.log。
- 单元测试 340 项，339 通过、1 项 io_uring 条件跳过：/tmp/cpnetflux-runs/V3-LANE-FAILOVER-01-unit-final.log。
- CTest 381/381，0 失败、1 项 io_uring 条件跳过；token-auth/event-log 用仅在测试进程环境注入的临时随机值运行，未写入仓库或证据：/tmp/cpnetflux-runs/V3-LANE-FAILOVER-01-ctest-final.log。
- 深圳→上海与上海→深圳各传输 256 MiB；每次只对目标 data TCP session 做故障注入，最终均 result=pass、2/2 LinkSet lane 接受、range completion=[2,2]，源/目的 SHA-256 一致。
  - 深圳→上海 SHA-256：a8c1906883db8da9d1f1378873e230cf647157d9c8bfd7ce2837ad5d1e1e5836；control_reconnect_count=2，data_connect_count=4，recovery_payload_bytes=11534336。
  - 上海→深圳 SHA-256：ff81afe80419ec7442cda8ece2d35eb64c1844a25a81cc9105508ed36c56810a；control_reconnect_count=2，data_connect_count=4，recovery_payload_bytes=45088768。
  - JSON/日志位于两端 /tmp/cpnetflux-runs/V3-LANE-FAILOVER-01-cross/logs/；传输文件留在两端 /tmp/cpnetflux-runs/V3-LANE-FAILOVER-01-cross/。
- 结果边界：队列层 lane 接管与端到端传输恢复已经验证，但控制面遥测显示两方向各发生一次整批控制重连、两条数据会话都重新建立。此结果不能证明健康 lane 在故障期间保持原连接；应继续收口 lane-local recovery。
- 深圳和上海本任务服务、测试进程及端口已停止并核实空闲。临时负载和日志留在 /tmp，不纳入 Git。
- 不据此宣称物理路径差异、物理 NIC 切换、性能提升或生产 readiness。

## 下一阶段交接

下一项为 V3-LANE-LOCAL-RECOVERY-01：将当前任一 TransportError 会触发 cancelAll、再由外层重建整个 persistent directory batch 的恢复方式，改为只恢复失败 lane。验收需要证明健康 lane 的数据 session/epoch 持续有效，只有失败 lane 的未提交 lease 被回收并重试，已完成 range 不重复，控制重连计数不因单 data lane 故障增加；再用深圳/上海双向单 socket 故障注入确认最终 SHA。物理 NIC 与不同物理路径配置继续暂缓，待代码侧 lane-local recovery 完成后在服务器上验证。
