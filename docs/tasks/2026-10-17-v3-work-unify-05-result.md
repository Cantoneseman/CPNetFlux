# V3-WORK-UNIFY-05：兼容编排收口与取消边界结果

状态：done
执行日期：2026-10-17（证据目录）；提交前复核：2026-10-18
输入提交：3525a1638a653864c9abdb9cbb30badd603247cb
工作区：/root/projects/CPNetFlux-worktrees/v3-work-unify-05
分支：codex/V3-WORK-UNIFY-05

## 本次开发解决的实际问题

此前只有 persistent dynamic 路径完整使用共享 WorkExecutor/queue owner；static、per-file、global 和 pipeline 兼容编排仍有各自认领、取消和终态处理。这样会造成三个实际风险：一个文件被拒绝后错误地终止整批目录、终态失败仍占用 channel credit/lease、取消或 join 时新任务继续被认领。静态通道还可能因全局扫描游标在交错认领下破坏同一通道的文件顺序。

本阶段把这些兼容路径接入共享 WorkExecutor 与 DynamicFileQueue，增加终态失败释放、stopClaims 和 static channel affinity；保留逐文件失败后继续其它文件的现有行为，并把外部传输故障、取消和 deadline 收敛到停止新认领再清理活动 lease 的顺序。静态队列改为有界元数据容量和按通道游标，不再要求按文件数扩张 ready ring。

## 实现范围

- DynamicFileQueue 支持 stopClaims、terminal failTerminal、静态通道 affinity 和有界静态游标。
- WorkExecutor 允许兼容路径在 Claimed/InFlight/CommitPending 的 IO 已静默状态下释放失败 lease/credit。
- persistent tree upload/download、global scheduler 和 pipeline 调度器共用 queue/executor owner；非 dynamic 路径维持逐文件结果语义。
- 取消路径先停止新认领，再等待 worker/connection 收尾；旧 lease/epoch 不得修改当前任务。
- 增加 queue/executor 的终态失败、静态保序、incoming channel 校验和 stop-claims 行为测试。

## 验收证据

证据根目录：/tmp/cpnetflux-runs/V3-WORK-UNIFY-05-20261017

- 22 个队列、执行器、恢复定向单元测试通过。
- run_cpnetflux_tree_data_reuse_smoke.py 通过：128×1 MiB upload/download、4 通道静态调度、单文件冲突后其余文件继续。
- run_v3_unified_executor_smoke.py 通过。
- Release + TLS + io_uring 配置全量 CTest：362/362 passed，0 failed；原始日志为 ctest-bounded-final.log（CTest 尾部报告 100% tests passed，总耗时 68.14s）。
- 最终版云端二进制在两端 SHA-256：
  - cpnetflux-gridftp-server: ad08ea0c5d67dc4d5a0d8cdbf892fe10fc9474a0515ad5df54f3c1cb0e1d3f1d
  - cpnetflux-tree-upload-client: f86c38d377177358b2fe47385eca2c57c9c66ec91e329e1216dc08dc9f7cda5c
  - cpnetflux-tree-download-client: c51a70949e20e19f725d03a0e87cf638c72fadc06cef26acc104f5da6e60af41
- 深圳 120.25.121.51 → 上海 47.116.174.181：persistent_tree、static、4 条数据连接，2/2 文件，67,112,064 bytes；逐文件 SHA-256 与接收端一致。
- 上海 → 深圳：同样 persistent_tree、static、4 条数据连接，2/2 文件，67,112,064 bytes；客户端 hash_match=true。
- 最终版故障注入：静态上传在 1 次 reset 后按预期 result=fail，1 completed/1 failed，1,641,600 bytes，故障日志仅有 1 条 reset；进程、控制端口和数据端口均已回收。该结果证明失败收敛与证据记录，不宣称 static 自动重连。

## 已知边界

本阶段没有改变 static 的自动重连语义，也没有启用十物理链路、RDMA、生产默认值或跨域性能结论。下一阶段只先实现 LinkSet 模型与能力协商，不直接进行十链路性能实验。

## 验收命令与交接

- 构建：cmake --build /tmp/cpnetflux-runs/V3-WORK-UNIFY-05-20261017/build -j2 --target cpnetflux_unit_tests cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux-gridftp-server
- 单测：/tmp/cpnetflux-runs/V3-WORK-UNIFY-05-20261017/build/cpnetflux_unit_tests --gtest_filter="WorkExecutorTest.*:WorkRecoveryTest.*:DynamicFileQueueTest.*"
- smoke：python3 tools/test/run_cpnetflux_tree_data_reuse_smoke.py --build-dir ...；python3 tools/test/run_v3_unified_executor_smoke.py --build-dir ...
- 全量：CPNETFLUX_TEST_TOKEN=V3_W05_TEST_TOKEN ctest --output-on-failure -j2
- 云端结果与端口回收记录位于证据目录及 2026-10-18-v3-work-unify-05-cloud-summary.json。

下一阶段任务单：docs/tasks/2026-10-18-v3-linkset-01.md。
