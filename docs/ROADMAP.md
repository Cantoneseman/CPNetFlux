# CPNetFlux 性能路线图

更新日期：2026-11-05
最近修复与实测源码：`78e1e4ab7a2e08eb0fd27f3958fd8536ac75b301`；原 48 样本基线仍按其输入提交解释。
当前开发分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`

## 当前目标

在 R770-1/R770-2 单端口 100GbE/RoCE 上，建立真实文件端到端性能证据，再按剖析结果优化。单端口路线不等于 10 端口 1Tbps 聚合验收。

原基线已采集 48 个样本，42 通过、6 个 64 GiB 下载失败，详见 [基线结果](tasks/2026-11-04-r770-rdma-e2e-baseline-01-result.md)。[V3 缺失块握手修复](tasks/2026-11-04-v3-resume-mask-01-result.md) 和 `fd10e16` 的 CQ 等待修复没有自动使完整文件门通过。

11/5 的[协议尾部诊断](tasks/2026-11-05-r770-rdma-tail-trace-01-result.md)定位了确定故障路径：64 GiB 文件发布且外部 SHA-256 正确，range 8 的 FILE_RESULT 两端均成功读写，但服务端在等待 172.614 秒后做 V3 lease finalize 返回 InvalidArgument。payload I/O 会刷新默认 30 秒租约，CommitPending 没有独立预算，而整文件校验耗时约 149.912 秒；成功结果到达时租约已过期，该通道没有发 DirectoryEnd，客户端继续等待。该诊断提交未修复行为，后续修复与验收见下文。

11/5 后续的 [V3 result 生命周期修复](tasks/2026-11-05-v3-result-lifetime-01-result.md)已通过 R770-1 全量 503/503、R770-2 相关 76/76 CTest，以及 1/4 lane 各三次原生 RDMA 64 GiB 下载完整任务与独立 SHA-256。两端同提交、同配置，五个二进制 SHA-256 一致；每次任务均无恢复重试，全部数据/控制终态完成。独立 result/commit 预算为 5 分钟且受任务截止约束，payload 空闲租约仍为 30 秒，过期和 owner/epoch/attempt 隔离保留。最长实测结果等待 174.730 秒已正常结束。另有 31 秒实际 TCP 等待、原生 RDMA shutdown/control-EOF 和连续 10 次终态丢失恢复证据。

当前下载 task rate 中位数为单通道 1.831384 Gbps（CV 0.254366%）、四通道 1.536711 Gbps（CV 0.348727%），均采用客户端接收端 full 校验和 commit_sync=none；外部 SHA 在任务计时之外。四通道约慢 16.1%，不能把本轮正确性修复当作吞吐提升。全文件复读校验约 149-158 秒，四通道后半组 range 还有约 80 秒事务锁等待；这些累计线程计时不可相加。RNR/out_of_buffer/transport retry/ACK timeout 增量为零，但 CQ error/flush 增量尚未归因，生产稳定性门未通过。原 48 样本的 42 PASS / 6 FAIL 保持历史结果，不改写成 48/48。

当前下一步是显式暴露并报告实际接收端的 final-verify、manifest/checkpoint、commit-sync 策略，保持默认值；再以相同持久化策略比较 full 与 verified_chunks，各至少三次、保留外部 SHA 与损坏/恢复/发布测试，并明确两者写后复读故障覆盖不同。随后分别优化 manifest 序列化、checkpoint 频率与共享锁，逐项对照。批量 checkpoint 16 仅为 range 诊断变量，不能升为默认或视作非 range 路径也已批量化。RQ/CQ 资源、三阶段流水线及微批按这些结果继续推进。

接收策略实现中的截短回归发现：旧报告所说的 checksum=none 强制完整复读并非实际行为，session 会直接跳过该复读，且原发布路径缺少实际长度检查。本轮补上所有校验策略发布前的长度门，并明确摘要中的 coverage_only/no-readback 语义，见 [校验语义决策](DECISIONS/2026-11-05-v3-receiver-verification-semantics.md)。既有 CRC32C 测量不受这一描述纠正影响；无 checksum 模式不进入完整性合格的性能对照。

完整范围、计时口径、阶段验收与性能目标见 [单链路 100G 性能提升计划](tasks/2026-11-04-performance-roadmap.md)。本轮路线决策见 [2026-11-04 性能决策](DECISIONS/2026-11-04-performance-route.md) 和 [result 生命周期决策](DECISIONS/2026-11-05-v3-result-lifetime.md)。下表区分历史链路/bench 证据与最新文件测量。

## 当前证据

| 类型 | 已测证据 | 不能推出 |
|---|---|---|
| 原生链路 | `ib_send_bw` 约 92.59 Gbps | CPNetFlux 文件传输达到 92.59 或 100 Gbps |
| CPNetFlux RDMA direct bench | 单连接长传输约 30.35 Gbps；4×64 GiB 组端到端约 82.97 Gbps | 文件系统、manifest 和完整任务时间有相同速率 |
| V3 真实文件，源码 `78e1e4ab` | 64 GiB 下载 1/4 lane 各三次全部通过；task rate 中位数 1.831384 / 1.536711 Gbps | 100 Gbps 文件吞吐、持久化 fsync 验收、原 48 样本全量重跑或生产 readiness |
| 硬件拓扑 | 每台 R770 一张 100GbE/RoCE 端口 | 10×100Gbps 多链路或 1Tbps 扩展 |

稳态 payload rate、含建连/提交的 task rate、每 lane 吞吐分别记录，不合并计时口径。测试需固定提交、双端同配置构建，逐文件校验并保存 route、端口计数、CPU/RSS 和原始日志。

## 串行执行阶段

| 阶段 | 主要工作 | 决策门 |
|---|---|---|
| 0. E2E 基线 | 对照原生带宽、CPNetFlux direct bench、V3 真实文件；1/4 连接、4/64 GiB、至少 3 次重复；核实同一大文件是否真正并行 range | 完整性通过，稳定与 task rate 分开，失败可解释；完成后再定优化项 |
| 1. 瓶颈归因 | 分解建连、注册/清零、RQ/CQ、帧复制、checksum、调度、写入/writeback、manifest/final verify | 主要耗时可被重复样本定位，不凭单一计数器推因果 |
| 2. RDMA 资源与完成队列 | 测接收池预算、RQ/SQ 深度、补投递、CQ 批处理及完成等待；维持槽位所有权和有界反压 | 找到低内存且长传输稳定的窗口；若收益不成立，保留现状 |
| 3. 三阶段流水线 | 按 profile 解耦读/checksum、网络收发、接收校验/写入/提交；比较有界暂存和现有 bounded pipeline | 端到端收益、RSS 预算、恢复与尾包语义均通过 |
| 4. 小文件自适应微批 | 对比优化后的逐文件基线、固定微批、自适应微批；测 4–256 KiB 与 1K–100K 文件规模 | 改善完整 T_ready 和 p95，成员 checksum、原子提交、独立恢复通过 |
| 5. 条件式服务率调度 | 只有观察到 worker 异质性、队列偏斜或拖尾时，比较 SR-Dispatch/BAP | 多次重复收益成立且无公平性、饥饿和恢复回归；否则不实现 |
| 6. 单链路稳定验收 | 冻结代码/配置/数据；双向 large/dense/mixed 重复测与超接收池长传输 | SHA 全过、CV ≤5%、无静默降级和未解释链路错误 |

每次仅改变一个主要因素。流水线队列有界；注册池、网络在途窗口和应用暂存分别记账。扩容缓冲本身不视作吞吐优化。微批需保留逻辑成员索引和成员级校验/恢复。RDMA WRITE、系统 MTU/NUMA 修改及 1Tbps 多端口设计均需独立决策。

## 分阶段目标

目标值用于计划与阶段门，不是当前成绩、工期承诺或保证值。真实文件需同时报告 payload rate 和从任务启动至校验提交完成的 task rate。

| 阶段目标 | 真实文件目标 |
|---|---:|
| 第一工程里程碑 | task rate 10–20 Gbps，且至少为同负载 post-RNR-fix 基线的 2 倍 |
| 中期大文件 | 单文件 4-lane range 并发 task rate 30–50 Gbps |
| 单端口冲刺 | 稳定 task rate 70–85 Gbps |
| 极限探索 | 85–90 Gbps，仅在测量与稳定性支持时 |
| 不承诺 | 应用有效吞吐 100 Gbps；或单端口实验证明 1 Tbps |

## 研究分支边界

开题中的三阶段流水线、SR-Dispatch/BAP、adaptive microbundle 是候选方案。批处理、文件聚合和流水线已有相关研究与工程实现；先完成 profiling 和 tuned-native/fixed 对照，再评估研究增量。小文件 T_ready 优先于只看 Gbps。CPSS(DCC) 独立管理，压缩不进入当前首轮传输热路径对照。

早期 S0-S8、Phase 和迁移路线已清理出当前工作树；相关实验结果若仍在 `docs/experiments`，按其输入提交和测量环境解释，不能自动继承为当前代码的 readiness 证据。
