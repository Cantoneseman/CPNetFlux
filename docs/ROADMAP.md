# CPNetFlux 性能路线图

更新：2026-11-10。当前决定：[按性能里程碑推进](DECISIONS/2026-11-10-v3-performance-milestones.md)。
最近交付：[里程碑38 queued mmap接收候选结果](tasks/2026-11-10-v3-receiver-pipeline-38-result.md)。
历史结果保留原提交、负载与结论；本页规定下一步，不产生新的性能成绩。

## 当前主线

**在固定完整性语义和测试条件下，让完整V3大文件任务跨过30Gbps，并验证向40–50Gbps扩展的机制。**
完整V3 TCP 256KiB DATA与有界接收buffer已实现并完成1GiB同提交对照，见[Milestone35结果](tasks/2026-11-10-v3-large-file-milestone-35-result.md)。[定向网络归因36](tasks/2026-11-10-v3-receive-network-36-result.md)未复现缺缓冲事件，根因仍OPEN；[1MiB DATA候选37](tasks/2026-11-10-v3-data-frame-1m-37-result.md)和[queued mmap接收候选38](tasks/2026-11-10-v3-receiver-pipeline-38-result.md)均NO-GO并撤回。38的1GiB完整任务吞吐35.758→30.877Gbps，五对全部退化，接收CPU采样中位0.85→1.25秒/GiB。8GiB正式门仍BLOCKED_ENV，不重复同前提盲测。
下一项：评估在range slot保持写入占用期间，把现有CRC32C chunk combine移出共享`RangeFileCoordinator::Entry`锁；先用正确性测试证明失败重试、checkpoint与恢复状态不变，再决定是否实施。不得增加接收线程、队列深度或网卡系统调优。
R770-1单人串行、同一时刻一个写入者；R770-2只接收固定提交。

## 已验证成果与当前性能边界

| 类别 | 已有事实 | 仍未完成 |
|---|---|---|
| 三阶段/可靠性工程 | V3三阶段工程收口；[Publication31](tasks/2026-11-10-v3-publication-reconcile-31-result.md)修复可证明自有hard-link发布后的CRC32C恢复窗口 | 不是所有发布方式/掉电恢复保证，不代表性能达标 |
| 大文件 | [Milestone35](tasks/2026-11-10-v3-large-file-milestone-35-result.md)1GiB：大帧27.331→35.821Gbps；独立buffer对照34.111→36.952Gbps；38在固定提交上inline 35.758、queued 30.877Gbps，queued五对全退化并撤回；旧scope17仍为27.291Gbps/8GiB | 8GiB M1仍阻塞，网络错误开放；40–50、70–85、100Gbps未验收，不能跨负载/提交算累计收益 |
| 小文件 | [S1摘要缓存22](tasks/2026-11-09-v3-summary-path-22-result.md)三组受测任务T_ready下降44.7%/54.5%/41.5%，显式cached可复现 | 默认original；S1B默认晋级仍受环境门阻塞；不是payload加速或自适应微批 |
| 机制资产 | [TCP所有权32](tasks/2026-11-10-v3-tcp-buffer-ownership-32-result.md)诊断发送CPU下降19.84%，完成通知/有界池通过对应测试 | 墙钟无可信改善，不集成默认；可作未来有新依据的组合资产 |

大文件参考条件：R770-2→R770-1、单个8GiB驻留tmpfs源/全新tmpfs目标、4路TCP、
queued发送/inline实验mmap接收、CRC32C/verified_chunks、checkpoint16、TLS/压缩off、
commit_sync=none。这是受限实验配置，不是默认、磁盘持久化、原生RDMA或生产成绩。
scope16/17及其他负载/计时合同不能跨轮直接算收益；最新提交需重新固定对照。

## 当前待办及依赖

| 顺序/状态 | 工作 | 交付与进入条件 |
|---|---|---|
| DONE EXPLICIT | 完整V3大负载DATA原型 | 256KiB逻辑DATA+4B CRC，能力/控制限额/预算/恢复通过；保持65532B默认 |
| DONE 1GiB | 同提交完整任务A/B | 两机制分别五对交错完整任务，全SHA/checkpoint通过；1GiB结果不能替代8GiB M1 |
| DONE EXPLICIT | 一个下一核心机制 | 按ss背压证据实现1MiB请求的接收buffer，实际2MiB/socket、4 lanes上限；显式、默认off |
| DONE / NO-GO | 1MiB DATA帧扩展 | [37结果](tasks/2026-11-10-v3-data-frame-1m-37-result.md)：五对1GiB吞吐未改善，候选CV 9.21%，与接收接口miss计数增加相伴；运行时选项已撤回，不再做帧大小扫描 |
| DONE / NO-GO | queued mmap接收交接 | [38结果](tasks/2026-11-10-v3-receiver-pipeline-38-result.md)：固定depth2队列，五对1GiB全退化，吞吐-13.65%、采样接收CPU+47%；候选已撤回，不再扫描队列深度或唤醒策略 |
| NEXT SCOPING | Entry锁内CRC combine | 先证明单range slot pin可保证校验状态独占，再以失败测试覆盖写入错误、重试、checkpoint和恢复；若无安全的锁外计算方案或成本依据不足，则冻结接收端软件候选并回到8GiB环境门诊断 |
| BLOCKED_ENV | 8GiB源驻留与换出 | [Recheck34](tasks/2026-11-10-v3-summary-default-recheck-34-result.md)在payload前失败；先解释受载进程/cgroup和条件变化，不凭空闲快照重试；不阻塞较小合规任务开发 |
| DEFERRED | S1B摘要缓存默认晋级 | 保留显式收益；等待对应环境/大文件回归门，不抢占大文件主线 |
| DEFERRED | 小文件固定/自适应成员微批 | 独立完成时间目标；利用S0/S1及S2证据，后续设计成员恢复/事务，不包装已有pending-window为新微批 |
| OPEN / BOUNDED DIAGNOSTIC DONE | 网络稳定性与原生RDMA | 36定位到mlx5接收RQ缺缓冲层，未复现；37的1MiB候选五对中总 `rx_missed_errors` +2,169、256KiB对照+36，接口范围因果仍需谨慎解释。停止加大DATA帧，不盲补采集或调整NIC；RDMA历史CQ/flush/peer-mapping门开放，运行时优化仍暂停 |

### 已结束候选的处置

- sender-inline、population、direct-receive、seed+collapse、source-mmap当前具体候选不原样重跑。
- [S2C序列化26](tasks/2026-11-09-v3-tree-serialization-runtime-26-result.md)完整任务慢2.50%，保留original默认。
  “保存更快→全量快照更多”的证据用于未来保存机制设计；不删必要checkpoint/ACK。
- TCP零拷贝32保留独立诊断及CPU效率证据，不自动集成、不重复调参求通过。
- 256KiB逻辑DATA路径保留；1MiB DATA尺寸扩展和queued mmap接收均已NO-GO。CRC算法与校验语义保持不变，下一候选只评估combine状态更新与Entry锁的交互。
- scope33“不再自动追加通用profiler”的止损原则保留；其“先证明≥10%才可开发”的前置限制失效。
- 不修改这些历史任务的实验数值与原判定；旧“下一项”按本页和里程碑35执行。

## 分层验收与效率规则

| 层级 | 通过条件 |
|---|---|
| 原型准入 | 具体机制、有成本证据、限定接口/预算与对照。无需提前证明10%或局部15%收益 |
| 正确性/机制 | CRC、租约、恢复、发布、边界/故障及资源通过，目标工作量确实变化；小样本只说明该负载 |
| 优化保留 | 同配置收益超过测量噪声，综合吞吐、CPU/GiB、内存、复杂度与回退；不设所有候选统一10%门 |
| 正式M1 | 同提交8GiB完整任务至少五对交错对照，候选task_rate中位≥30Gbps且有可信配对改善；CV≤5%、至少4/5配对改善，内容/资源/恢复通过 |
| 后续M2/稳定性 | 验证40–50Gbps及扩展机制；网络异常、方向/TLS/磁盘/持久化/长时分别验收，不用M1替代生产readiness |

每个机制默认一轮原型、一轮有原因的修正；噪声不清报告不确定，不挑样本或无界重复。
简单稳定3%–5%可有保留价值，CPU收益可独立记录；都不能自动宣称吞吐达标或默认晋级。
新观测先说明要区分的解释与下一行动，最多一轮定向采集。复用驱动/测试/清理，不为每次检查建立新阶段。
每轮按完整任务成绩、可用能力、关键障碍和下一交付汇报，提交数/测试数仅作为证据。

## 配置、资源与实验纪律

- 默认行为：sender/receiver queued、pwrite、read0/write0、depth2/batch1；保留默认full校验。
- 大文件参考：4 connections/活跃range，queued sender、inline实验mmap receiver；
  64MiB/lane映射、总256MiB、总payload预算64MiB。新大帧必须按全部持有者重算，不增加总预算。
- 小文件summary默认original、cached仅显式；tree serializer默认original；详细profile默认off。
  新鲜下载auto pending-window实际仍为2，历史窗口收益不外推。
- 源/输出各≤8GiB、MemAvailable≥12GiB、owned swap及host swap-out增量为0，正式源驻留；
  原资源门保持。≤1GiB任务也必须满足门；8GiB阻塞时不能以换计数基线或预热排除成本过门。
- 同提交、同配置构建/版本/二进制SHA核对后才能互传；数据显式bind192.168.100.2/.1并记录路由/计数。
- 每个成功任务保留CRC、V3工作租约、checkpoint/恢复/原子发布及外部SHA；失败吞吐不得用planned bytes。
- 仅R770-1写源码/Git，R770-2接收固定提交；不改共享网络、THP、swap或服务；不使用子代理/worktree。

详细复现配置见[三阶段指南](V3_TRANSFER_STAGES.md)、scope17/35；服务器和构建见[环境](coordination/ENVIRONMENT.md)。
历史研究/生产边界见[研究基线](RESEARCH_BASELINE.md)，结果索引见[文档导航](README.md)。
