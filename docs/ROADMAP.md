# CPNetFlux 性能路线图

更新：2026-11-11。当前决定：[按性能里程碑推进](DECISIONS/2026-11-10-v3-performance-milestones.md)。
最近交付：[8 GiB大文件M1验收44结果](tasks/2026-11-11-v3-large-file-m1-44-result.md)。
历史结果保留原提交、负载与结论；本页规定下一步，不产生新的性能成绩。

## 当前主线

**M1已在8GiB完整V3 TCP任务中跨过30Gbps；下一阶段以该固定配置为基线，选择一个有阶段证据支撑的机制推进40–50Gbps。**
完整V3 TCP 256KiB DATA与有界接收buffer已完成1GiB同提交对照，见[Milestone35结果](tasks/2026-11-10-v3-large-file-milestone-35-result.md)。[定向网络归因36](tasks/2026-11-10-v3-receive-network-36-result.md)和[诊断40](tasks/2026-11-11-v3-rx-queue-irq-correlation-40-result.md)的固定路径采样均未复现缺缓冲事件；[1MiB DATA候选37](tasks/2026-11-10-v3-data-frame-1m-37-result.md)和[queued mmap接收候选38](tasks/2026-11-10-v3-receiver-pipeline-38-result.md)均NO-GO。Diagnostic39在R770-2一次8GiB源准备中验证全页驻留、零pswpout、零进程/叶cgroup swap。M1-41驱动已实现，但唯一矩阵在首个control任务的资源门中止，没有完整传输样本；详见[M1-41结果](tasks/2026-11-11-v3-large-file-m1-41-result.md)。新一轮[M1-44](tasks/2026-11-11-v3-large-file-m1-44-result.md)完成五对8GiB交错对照，组合配置中位34.159Gbps，通过正式M1门。
诊断40的固定256KiB DATA/1MiB接收buffer采样未复现RX缺缓冲事件；M1-41也未形成可比较吞吐，不能据此推断网络或候选收益。[任务42](tasks/2026-11-11-v3-resource-gate-failure-evidence-42-result.md)已补全失败swap状态与客户端receipt；[任务43结果](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)在两端120秒空载观察中未复现host变化，M1-41单页事件仍未归因。[M1-44结果](tasks/2026-11-11-v3-large-file-m1-44-result.md)在10个完整任务中均满足资源、内容、接口计数与清理门；两端host pswpout增量为0。当前只报告受限TCP/tmpfs实验成绩。Entry锁内CRC combine不再开发：实测成本约为采样接收CPU的0.013%。不得增加接收线程、队列深度或网卡系统调优。
R770-1单人串行、同一时刻一个写入者；R770-2只接收固定提交。

## 已验证成果与当前性能边界

| 类别 | 已有事实 | 仍未完成 |
|---|---|---|
| 三阶段/可靠性工程 | V3三阶段工程收口；[Publication31](tasks/2026-11-10-v3-publication-reconcile-31-result.md)修复可证明自有hard-link发布后的CRC32C恢复窗口 | 不是所有发布方式/掉电恢复保证，不代表性能达标 |
| 大文件 | [Milestone35](tasks/2026-11-10-v3-large-file-milestone-35-result.md)1GiB候选数据；[M1-44](tasks/2026-11-11-v3-large-file-m1-44-result.md)同提交8GiB：default 28.126、combined 34.159Gbps，配对中位提升21.45%，5/5改善 | 40–50、70–85、100Gbps仍未验收；M1组合效应未在8GiB拆成单项归因，不能跨负载/提交累加收益 |
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
| DONE / NO-GO | Entry锁内CRC combine | [Diagnostic39](tasks/2026-11-10-v3-source-residency-ownership-39-result.md)：256KiB combine约0.1084ms/GiB，相对约0.85 receiver CPU秒/GiB仅约0.013%；不改运行时 |
| SOURCE PREP PASS | 8GiB源驻留与换出 | [Recheck34](tasks/2026-11-10-v3-summary-default-recheck-34-result.md)的失败 count gap由[Diagnostic39](tasks/2026-11-10-v3-source-residency-ownership-39-result.md)补齐一次：同进程/cgroup下全页驻留、pswpout及swap为0；仅为源准备诊断 |
| ATTEMPT ABORTED / NO PERFORMANCE DATA | 8GiB旧M1尝试 | [M1-41](tasks/2026-11-11-v3-large-file-m1-41-result.md)：首个control任务在host swap资源门中止，零完整样本；不重跑。失败证据见[任务42](tasks/2026-11-11-v3-resource-gate-failure-evidence-42-result.md)，空载归因见[任务43](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md) |
| PASS | 8GiB正式M1验收 | [M1-44结果](tasks/2026-11-11-v3-large-file-m1-44-result.md)：34.159Gbps combined中位、CV 3.24%、5/5配对改善；DATA帧中位139,264→32,768，接收端CPU采样下界7.99→6.77s。下一步先完善profile证据保留，再用≤1GiB诊断记录逻辑copy/CRC/线程CPU并选定一个机制；profile吞吐不作成绩。40–50Gbps目标仍开放 |
| DONE / NOT REPRODUCED | host `pswpout`归因诊断 | [任务43结果](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)：两端120秒无payload观察均稳定，未定位M1-41单页增量；M1-44已通过独立host swap-out门，但这不改变M1-41旧结果；不重跑M1-41 |
| DEFERRED | S1B摘要缓存默认晋级 | 保留显式收益；等待对应环境/大文件回归门，不抢占大文件主线 |
| DEFERRED | 小文件固定/自适应成员微批 | 独立完成时间目标；利用S0/S1及S2证据，后续设计成员恢复/事务，不包装已有pending-window为新微批 |
| DIAGNOSTIC DONE / CAUSE OPEN | 网络稳定性与原生RDMA | 诊断40当前固定路径一次未复现；37历史候选总 `rx_missed_errors` +2,169、256KiB对照+36仍无队列/流级归因。任务43的host swap事件观察未复现且不能解释M1-41单页变化；M1-44的10个TCP样本均无rx_missed_errors增量；旧网络因果仍开放，原生RDMA运行时优化继续暂停 |

### 已结束候选的处置

- sender-inline、population、direct-receive、seed+collapse、source-mmap当前具体候选不原样重跑。
- [S2C序列化26](tasks/2026-11-09-v3-tree-serialization-runtime-26-result.md)完整任务慢2.50%，保留original默认。
  “保存更快→全量快照更多”的证据用于未来保存机制设计；不删必要checkpoint/ACK。
- TCP零拷贝32保留独立诊断及CPU效率证据，不自动集成、不重复调参求通过。
- 256KiB逻辑DATA路径保留；1MiB DATA尺寸扩展、queued mmap接收和当前Entry锁内CRC combine候选均NO-GO。CRC算法与校验语义不变；诊断40未复现RX错误，不再重复同类采样；M1-41仍不重跑；M1-44两端资源门通过，旧单页事件不因此视为已归因。
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
