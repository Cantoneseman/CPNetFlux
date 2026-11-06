# V3 有界无额外复制聚合写入：实现与 TCP 对照结果

日期：2026-11-07。输入 `6cbc5aefa1b147914ebdc269c39c2c763b0b4877`。权威根为 R770-1 `/home/Su/projects/CPNetFlux`；单人串行开发，R770-2 仅同步固定提交、构建和测试。

## 交付与处置

实现最多两个已经到达、连续 offset 的 payload 借用式 `pwritev` 写入，以及批量出队/有界回收；没有新增应用 payload 副本或等待凑批。**同提交 TCP 对照未证实净收益，拒绝默认启用。** `CPNETFLUX_RECEIVER_WRITE_BATCH` 默认 `1`；`2` 仅为诊断候选。保持 AvailableCapacity 唤醒、depth2、preallocate off；没有更改原有校验/持久化默认值。

实现提交 `3e373b7ba26e19eb90aefcf401e2204ee8ba254b`；回收修正与本次全部正式实验提交 `57502f309cbcde56e2e25f1cd49bf2b0da046b8d`。均已推送 GitHub 并回读分支 SHA；最终文档提交/备份回执见外部证据根 HANDOFF/backup JSON。性能对照没有使用第一版有回收问题的提交。

## 实现边界

- `FileWriteSlice` 最多两个借用 span；局部 iovec 调整处理跨 slice 短写、EINTR/EAGAIN、零写和错误；检查空指针、offset/长度溢出。无 payload 合并复制。
- writer 仅从队列取当前可用项；POSIX direct、整文件、连续偏移才允许合写。range、buffered/io_uring、max-chunks 保留逐帧 V3；非法模式在创建 checkpoint 前拒绝。
- 实际批次写入成功后才逐帧记录 chunk/校验进度。部分写后失败不认证该批次；已发出的每帧租约 IO 在错误/中断时仍逐一 drain。
- depth2 队列上限 131072 字节；本轮无压缩路径最多五个 payload 所有者，即约 327680 字节容量预算。两帧消费可在等待时留下四个空闲 owner，加一个生产者；回收既有 owner，不扩大在途队列。发送端预算未改；有压缩兼容测试不等于对 codec 暂存作相同总内存预算保证。
- timing 导出 effective batch limit、两帧 wrapper 尝试数/帧/请求字节。它们不是“已验证进度”或经独立探针观测的内核 syscall 次数。没有实现全局独立三阶段 worker 池。

## 发现并修复的回归

第一版两端 full/related gate 各有两项失败：旧空闲回收槽只适合单帧消费者，特定调度下丢弃可复用 buffer，增加分配；另一个测试把 writer 调用数写死为帧数。新增确定性四空闲 owner 测试先观察到仅保留 48/64 字节，再修复两帧回收边界。另更新模式相关分配预算和 writer 调用会计，未放宽 payload 所有权上限。保留初始失败日志，不作为最终通过成绩。

## 正确性与门禁

- storage/queue 初始 RED 8 项中 7 失败、1 已有非法输入拒绝通过；receiver 新测试 RED 2/2 失败，随后实现 GREEN。
- 存储故障注入执行真实 pwritev，验证输入指针/数据不变、短写跨 slice、重试、部分失败、零写、无效输入。队列覆盖关闭排空、取消唤醒、并发顺序、可变长度及所有权预算。
- 两帧快速相关测试 30 轮；实际 framed sender/receiver 测试 batch2 30 轮、batch1 10 轮；均通过。修正后 R1 全量 CTest **568/568，254.69 秒**；R2 相关 **219/219，59.22 秒**，测试环境 batch2；最后默认 batch1 相关单测 **123/123，36.11 秒**。无跳过。环境 token 临时生成且不写仓库/日志；现有 RDMA 兼容门使用显式 bind，没有优化 RDMA。
- 实际 TCP 故障/恢复：第一次 pwritev 在 offset 982980、请求 65596 字节的跨边界批次中仅写 1024 字节，再返回 EIO。失败时输出未发布、verified chunk 为 0（该批次本应结束第一个 1MiB chunk）；同一个失败目录随后 V3 resume 成功，完整 SHA 一致。注入库仅用于该诊断进程，未链接生产程序，不进入性能组。

## 同提交 TCP 交叉对照

双端 `57502f30`、相同 CMake 配置/版本和八个二进制 SHA-256 一致；每个样本重新核对 tracked clean。R2→R1，显式 bind `192.168.100.2`/`192.168.100.1`，接口 `eno17095np0`，控制面和数据面均明确为 TCP/TLS off，native QP 初始化事件为零。路由/接口计数留存；这不是原生 verbs 试验。

同一 1GiB tmpfs 源/每轮新目标，单 lane、无压缩、CRC32C、verified_chunks、every16 manifest、commit_sync=none、direct write、depth2。有效 chunk1MiB，共17408个 DATA frame。顺序 AB/BA/AB/BA，共四对；外部 SHA 和预热在 task 之外，程序建连/源读/校验/写入/提交在 task 内。各指标独立取中位数，文件阶段是 phase wall 的派生速率，不能与完整任务混称。

| 指标，中位数 | 单帧直接写 | 两帧候选 |
|---|---:|---:|
| 完整 task Gbps | 20.099 | 17.045 |
| 文件阶段 Gbps | 23.442 | 19.069 |
| task 秒 | 0.427609 | 0.506286 |
| 文件秒 | 0.366452 | 0.450821 |
| 接收 lane+writer CPU 秒/GiB | 0.645868 | 0.733323 |
| 接收进程 user+sys CPU 秒/GiB | 0.685 | 0.775 |
| 发送进程 CPU 秒/GiB | 0.555 | 0.640 |
| 双端进程 CPU 秒/GiB | 1.240 | 1.415 |
| writer wrapper 调用/每GiB | 17408.0 | 13179.5 |
| 其中两帧聚合 wrapper 调用 | 0.0 | 4228.5 |
| 接收 payload 实际分配次数 | 4 | 5 |

两帧候选 writer 调用减少24.29%，但 task/文件吞吐中位数下降15.20%/18.65%，接收线程 CPU 增加13.54%，双端进程 CPU 增加14.11%。四对小样本存在波动，仅否定当前负载下的默认推广，不声称所有设备上的 pwritev 都无收益。发送进程 CPU 取 client transfer 前后 `/proc/pid/stat` ticks 差，含同期线程总量/空闲时段、粒度0.01秒；接收进程 CPU 取 GNU time，thread CPU 为 phase 观测，口径分别保留。没有用测量开销消失来承诺生产收益。

| 样本 | task Gbps | 文件 Gbps | 接收线程CPU秒 | 两帧调用 | writer调用 |
|---|---:|---:|---:|---:|---:|
| direct-1-tmpfs-1g-c1-verified_chunks-d2-01 | 21.620 | 25.571 | 0.618588 | 0 | 17408 |
| direct-1-tmpfs-1g-c1-verified_chunks-d2-02 | 19.625 | 23.290 | 0.648304 | 0 | 17408 |
| direct-1-tmpfs-1g-c1-verified_chunks-d2-03 | 17.967 | 20.551 | 0.709822 | 0 | 17408 |
| direct-1-tmpfs-1g-c1-verified_chunks-d2-04 | 20.574 | 23.594 | 0.643432 | 0 | 17408 |
| gathered-2-tmpfs-1g-c1-verified_chunks-d2-01 | 15.088 | 16.923 | 0.772497 | 3816 | 13592 |
| gathered-2-tmpfs-1g-c1-verified_chunks-d2-02 | 15.888 | 18.530 | 0.742935 | 4179 | 13229 |
| gathered-2-tmpfs-1g-c1-verified_chunks-d2-03 | 20.241 | 23.941 | 0.653030 | 6983 | 10425 |
| gathered-2-tmpfs-1g-c1-verified_chunks-d2-04 | 18.203 | 19.609 | 0.723711 | 4278 | 13130 |

八个样本全部应用校验/独立 SHA/协议终态/内存门通过，无恢复重试，发送端物理读取字节均为0，payload logical_copy_bytes为0。内存文件、非持久化策略的 TCP 测量与旧 RDMA 冷池 task 结果不可直接做代码提速比；不能当磁盘吞吐、100G 能力或生产 readiness。

## 空间、证据与复现

源 SHA256 `d07872d86158e18d8c147240e1951b138c2c0d8e1a48eb2586859619946e3880`。只生成 R2 一份1GiB源、R1一份当前输出；内存门 MemAvailable>=12GiB、owned swap为0、host pswpout增量为0，RSS与tmpfs分别统计。每轮输出校验后删除；最后重核源 inode45/长度/SHA 后删除两端 `/dev/shm/cpnetflux-v3-gathered-20261107`。清理仅限本任务 fixture，没有清理其他实验或项目。

外部证据根：`/home/Su/experiments/CPNetFlux/2026-11-07-v3-gathered-write-01`。保留 `experiment.py`/`baseline_evidence.py`（TCP），`run_experiments.py`、`recovery.py`、故障注入C源码，`validate.py`、`metrics/comparison.json`/CSV、每轮 JSON/日志/内存门/route/interface快照、checkpoint小副本、验收与清理回执。大 payload/构建/凭据不进 Git。

复现时必须使用新 fixture/证据目录，先同提交构建/哈希，再 seed 后执行 `python3 experiment.py gathered-series <fixed-full-SHA>`；禁止覆盖旧 receipts。默认路径运行时无需设置 batch 环境变量；候选进程显式 `CPNETFLUX_RECEIVER_WRITE_BATCH=2`。不提供跨版本互传或运行时 V3 降级。

## 下一步（尚未执行）

1. 继续 TCP 公共软件路径：固定小内存源和策略，先用现有 timing 做1/2/4 lane 有界伸缩对照，区分源读/CRC、接收网络线程、writer、range事务锁与 task setup，记录双端进程CPU/预算；phase profiling on/off需单独核对。
2. 根据锁/CPU证据确定下一项实现。如果不同 range 的写入仍被共享事务锁串行化，先设计并验证不重叠 offset 写入与短 checkpoint 临界区，保留 V3 lease/断连排空/成员级校验；不能直接增加线程和缓冲区宣称已解耦。
3. 仅在阶段服务率与净 task 收益证明后推进独立的有界读/网络/写调度；队列预算按总字节统一记账，重测失败恢复与完整性。仍不改 RDMA 参数或系统网络。

全局三阶段 worker 池未实现；CQ response/flush 与旧 peer-mapping 继续开放；100G长期稳定性门未通过。
