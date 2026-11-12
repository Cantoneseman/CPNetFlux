# CPNetFlux 性能路线图

进行中：[Task58 有界8GiB正式M2](tasks/2026-11-12-v3-private-m2-58.md)。补齐固定双profile、必要准备计时/资源上限、同数据与失败停止门；先测试和固定双端版本再运行唯一五对组合矩阵。Task57单次准入不升级为正式成绩。

最新交付：[Task57 / 原生必要准备与交付C冻结](tasks/2026-11-12-v3-private-setup-57-result.md)：五对准备门通过（旧中位53.40ms→native 7.62ms），最终两端相关CTest各261/261、kernel pacing smoke各连续10次通过；唯一1GiB完整目标任务**40.865Gbps**，必要准备计入，内容/资源/网络/页型/清理均通过。仅单次可行性准入，正式8GiB M2与稳定性仍开放；[冻结清单](tasks/2026-11-12-v3-private-setup-57-freeze.json)。默认关闭，不追加本轮样本或参数扫描。

下一步：先完成有界8GiB私有后端/driver支持、资源核算与正式M2合同，再按既定五对交错/≥40Gbps/CV≤5%/至少4对改善和全部硬门验收。当前1280MiB挂载与1GiB驱动不能直接运行8GiB；本轮不自动启动新矩阵。原生RDMA及DEFERRED项继续暂停。

前次交付：[Task56 / 内核TCP pacing与交付C冻结](tasks/2026-11-12-v3-kernel-tcp-pacing-56-result.md)：默认关闭机制已实现，R2相关259项通过、R1失败修复后定向5项通过，两端候选恢复各2项通过。唯一1GiB完整目标任务35.388Gbps，网络/资源/内容/页型/清理通过，但未达40Gbps准入。四lane ceiling回读不等于实际整形；一次0错误不关闭OOB因果。停止追加本候选和8GiB矩阵，M2仍开放；[冻结清单](tasks/2026-11-12-v3-kernel-tcp-pacing-56-freeze.json)。

历史交付：[Task55 / 私有tmpfs完整V3准入与交付C冻结](tasks/2026-11-12-v3-private-tmpfs-55-result.md)：默认关闭的有界后端与两端各18项相关回归通过。唯一1GiB样本计入必要目标准备后35.710Gbps，OOB/missed各+261，网络门失败；原始客户端窗口45.922Gbps不计为合格性能成绩。资源/内容/实际页型/清理通过，未运行新8GiB矩阵，M2仍开放；停止本候选的追加payload与扫描。冻结见[清单](tasks/2026-11-12-v3-private-tmpfs-55-freeze.json)。

前序机制：[首次大页54](tasks/2026-11-12-v3-private-tmpfs-54-result.md)仅局部五对通过；
[ID水位53](tasks/2026-11-12-v3-record-id-watermark-53-result.md)已交付。当前下一步以Task57结果与路线图为准，不沿用旧文档“待做1GiB”的历史下一步。

交付C历史快照已核对Task51/52版本：[冻结清单](tasks/2026-11-12-performance-delivery-c-freeze.json)、[交付结果与边界](tasks/2026-11-12-performance-delivery-c.md)。工程交付完成，M2/网络稳定性开放；历史Task50冻结保留原版本，不作为新代码性能成绩。

更新：2026-11-12。当前决定：[按性能里程碑推进](DECISIONS/2026-11-10-v3-performance-milestones.md)。
最近交付：[发送节奏50与本阶段收尾结果](tasks/2026-11-11-v3-sender-pacing-50-result.md)：ENGINEERING CLOSED / M2 OPEN；已验收大文件成绩见[M1-44](tasks/2026-11-11-v3-large-file-m1-44-result.md)。
历史结果保留原提交、负载与结论；本页规定下一步，不产生新的性能成绩。

本阶段已按[收尾与串行交接](tasks/2026-11-11-performance-stage-closeout-handoff.md)结束：
发送候选实现与回归通过，唯一1GiB可行性任务28.221Gbps，未达40Gbps正式矩阵准入；未运行新8GiB矩阵。
工程交付、冻结配置与边界评估完成，M2和长期稳定性仍开放；停止追加同类诊断或参数扫描。
2026-11-12已交付[B的离线成绩复核与NO-GO判定](tasks/2026-11-11-v3-sender-pacing-50-result.md)：A未达速率准入，新正式M2为0样本；历史M1原始hash与十个完整任务复算通过，未将未测项计为pass。
2026-11-12交付C已完成：[最终冻结清单](tasks/2026-11-11-v3-sender-pacing-50-freeze.json)固定默认/实验/诊断配置、代码与二进制；回归证据及两端现场清理复核完成，阶段工程报告项收口，M2/稳定性未达标。
用户新提出的[统一工作单元/预算采样/策略记忆](ideas/2026-11-11-unified-work-unit-and-budgeted-policy-memory.md)已存档为DEFERRED，本轮不开发；阶段结束后也需用户明确重启。

## 新目标执行（2026-11-12）

用户已要求在上一阶段收尾后继续修复实验暴露的问题并推进开发。[等待成本51](tasks/2026-11-12-v3-pacing-wait-51.md)先做一次无payload定向定位，确认机制后只改一个有界任务局部候选；不重开参数扫描、不放宽M2/资源门。下文Task50/C的结束状态是上一阶段历史交付，不表示停止本次新目标。原生RDMA及DEFERRED新idea仍暂停。

任务51结果：[有界timerfd等待修复](tasks/2026-11-12-v3-pacing-wait-51-result.md)已完成并固定52acbeb9；
两端相关CTest各241通过，唯一1GiB可行性36.478Gbps，完整性/资源/网络/清理通过。
未达40Gbps准入，新8GiB正式样本0，不能与Task50或历史M1算跨提交收益。
本候选停止追加rate/lane/wait扫描；下一设计检查接收端页管理/搬运中是否还有具体可减工作，
与已有聚合写及失败population/direct-receive/collapse路径区分；原生RDMA及DEFERRED工作仍暂停。

Task52已完成[默认关闭exact-read实现与结果](tasks/2026-11-12-v3-tcp-exact-read-52-result.md)：
两端相关254/254通过；唯一1GiB的4096个大负载均一次recv完成。
新driver的channel/lane索引错误已用实际合同测试修复；原始记录不改、只离线复核。
窄窗OOB +52、刷新尾窗missed +52，网络门FAIL；36.011Gbps仅原始耗时，不是合格性能成绩。
未达40且网络门失败，不追加8GiB或扫描，exact-read保持默认off，M2/网络稳定性OPEN。
下一实现前须在TCP接收/CRC/新目标页管理的数据所有权与搬运中识别具体可减工作，
区别于已关闭候选；不增加通用profiler轮次。原生RDMA及DEFERRED工作继续暂停。

Task55已完成默认关闭私有tmpfs实验后端与本轮交付C冻结，见[结果](tasks/2026-11-12-v3-private-tmpfs-55-result.md)。
新目标大页实际生效，但唯一1GiB完整目标任务35.710Gbps、OOB/missed各+261；
资源/内容/清理通过，网络门FAIL。没有新的合格吞吐成绩或8GiB M2样本。
本候选停止追加试验；下一机制必须解释可减少的接收服务工作与OOB证据边界，
不放宽门、不扫描参数，也不自动重启RDMA/DEFERRED项。

Task56已完成[实现、完整任务与交付C冻结](tasks/2026-11-12-v3-kernel-tcp-pacing-56-result.md)。
per-socket内核TCP pacing四lane回读通过，无应用逐帧timer；唯一1GiB完整目标35.388Gbps，
观察窗OOB/missed为0，但没有稳定性/因果证据，未达40Gbps。本候选停止追加payload与扫描。
该历史下一步已由Task57执行：实际必要准备工作被减少，单次完整任务40.865Gbps准入；
不能跨提交算收益，也不能将该样本等同正式M2。后续以页首Task57结果为准。

## 当前主线

**M1已在8GiB完整V3 TCP任务中跨过30Gbps；Task57完成必要准备优化与单次1GiB完整目标40.865Gbps准入。正式40–50Gbps M2仍未验收，默认配置没有晋级。**
完整V3 TCP 256KiB DATA与有界接收buffer已完成1GiB同提交对照，见[Milestone35结果](tasks/2026-11-10-v3-large-file-milestone-35-result.md)。[定向网络归因36](tasks/2026-11-10-v3-receive-network-36-result.md)和[诊断40](tasks/2026-11-11-v3-rx-queue-irq-correlation-40-result.md)的固定路径采样均未复现缺缓冲事件；[1MiB DATA候选37](tasks/2026-11-10-v3-data-frame-1m-37-result.md)和[queued mmap接收候选38](tasks/2026-11-10-v3-receiver-pipeline-38-result.md)均NO-GO。Diagnostic39在R770-2一次8GiB源准备中验证全页驻留、零pswpout、零进程/叶cgroup swap。M1-41驱动已实现，但唯一矩阵在首个control任务的资源门中止，没有完整传输样本；详见[M1-41结果](tasks/2026-11-11-v3-large-file-m1-41-result.md)。新一轮[M1-44](tasks/2026-11-11-v3-large-file-m1-44-result.md)完成五对8GiB交错对照，组合配置中位34.159Gbps，通过正式M1门。
诊断40的固定256KiB DATA/1MiB接收buffer采样未复现RX缺缓冲事件；M1-41也未形成可比较吞吐，不能据此推断网络或候选收益。[任务42](tasks/2026-11-11-v3-resource-gate-failure-evidence-42-result.md)已补全失败swap状态与客户端receipt；[任务43结果](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)在两端120秒空载观察中未复现host变化，M1-41单页事件仍未归因。[M1-44结果](tasks/2026-11-11-v3-large-file-m1-44-result.md)在10个完整任务中均满足资源、内容、接口计数与清理门；两端host pswpout增量为0。当前只报告受限TCP/tmpfs实验成绩。Entry锁内CRC combine不再开发：实测成本约为采样接收CPU的0.013%。不得增加接收线程、队列深度或网卡系统调优。
M1-45 profile诊断记录6/10个frame矩阵样本完整，最后一个宽窗口 rx_missed_errors +405；后续审查确认旧窗口跨传输和本地校验，无法归因。Task46首个off样本的窄窗口观测到 rx_out_of_buffer +237后按停止门中止，profile对照无结论。Task47复算发现rx13/rx16 page-pool empty/slow +8/+10，不能归因接口OOB；Task48核对精确mlx5源码，确认rx_out_of_buffer由shared-device Q counter聚合且没有RX队列ID，现有debugfs/tracepoint也未提供逐RQ归属，详见[Task48结果](tasks/2026-11-11-v3-rx-oob-driver-source-audit-48-result.md)。新GridFTP加密诊断也在1GiB明文样本记录R2 missed/OOB +104，4GiB组因host swap中止，不能归为CPNetFlux mmap独有问题。[Task49结果](tasks/2026-11-11-v3-rx-lane-diagnostic-49-result.md)：driver窗口/停止门已修复；首个1GiB单路pwrite、无详细profiler样本记录R1 OOB/missed各+171，四路按门未运行，完整性/资源/清理通过，无有效性能结果。说明降到一路仍未避免现象，不提供流/RQ因果。[Task50结果](tasks/2026-11-11-v3-sender-pacing-50-result.md)：显式每lane12Gbps应用DATA准入pacer实际生效；唯一1GiB四路样本28.221Gbps，内容、资源及三个网络观察窗口通过，但速率未达40Gbps准入，未运行新8GiB正式矩阵。它不是同提交收益对照，不能与历史M1直接计算退化；一次零事件也不能关闭OOB因果。候选默认off、不推荐用于性能配置，本阶段停止实验；M2及稳定性仍开放。
R770-1单人串行、同一时刻一个写入者；R770-2只接收固定提交。

## 已验证成果与当前性能边界

| 类别 | 已有事实 | 仍未完成 |
|---|---|---|
| 三阶段/可靠性工程 | V3三阶段工程收口；[Publication31](tasks/2026-11-10-v3-publication-reconcile-31-result.md)修复可证明自有hard-link发布后的CRC32C恢复窗口 | 不是所有发布方式/掉电恢复保证，不代表性能达标 |
| 大文件 | [Milestone35](tasks/2026-11-10-v3-large-file-milestone-35-result.md)1GiB候选数据；[M1-44](tasks/2026-11-11-v3-large-file-m1-44-result.md)同提交8GiB：default 28.126、combined 34.159Gbps，两组中位速率之比提升21.45%，5/5改善 | 40–50、70–85、100Gbps仍未验收；M1组合效应未在8GiB拆成单项归因，不能跨负载/提交累加收益 |
| 小文件 | [S1摘要缓存22](tasks/2026-11-09-v3-summary-path-22-result.md)三组受测任务T_ready下降44.7%/54.5%/41.5%，显式cached可复现 | 默认original；S1B默认晋级仍受环境门阻塞；不是payload加速或自适应微批 |
| 单次准入 | [Task57](tasks/2026-11-12-v3-private-setup-57-result.md)1GiB完整目标40.865Gbps，必要准备计时、全部准入门通过 | 没有A/B、CV或正式8GiB M2；不证明稳定40Gbps、磁盘/TLS/RDMA性能 |
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
| PASS | 8GiB正式M1验收 | [M1-44结果](tasks/2026-11-11-v3-large-file-m1-44-result.md)：34.159Gbps combined中位、CV 3.24%、5/5配对改善；DATA帧中位139,264→32,768，接收端CPU采样下界7.99→6.77s。40–50Gbps目标仍开放 |
| ABORTED / NO PERFORMANCE RESULT | M1-45接收端profile诊断 | [结果](tasks/2026-11-11-v3-receiver-profile-diagnostic-45-result.md)：frame矩阵在6/10样本后因宽窗口 `rx_missed_errors` +405停止；receive-buffer矩阵未运行。只读审查确认旧窗口跨传输及本地校验，不能归因 |
| ABORTED / NO PROFILE OR PERFORMANCE RESULT | Task46 profile扰动与窄计数窗口 | [结果](tasks/2026-11-11-v3-profile-overhead-counter-correlation-46-result.md)：profile-off首个1GiB样本观察窗口记录 rx_out_of_buffer +237后停止；完整性/资源/清理通过。Task48源码核验确认该计数无逐RQ归属；按Task48合同仅进行1GiB 1-lane/4-lane无profiler复现，不重跑profile矩阵 |
| DONE / DIAGNOSTIC ONLY | Task47 page-pool队列计数复算 | [合同](tasks/2026-11-11-v3-rx-page-pool-counter-analysis-47.md)与[结果](tasks/2026-11-11-v3-rx-page-pool-counter-analysis-47-result.md)：分析器输出RX packet/page-pool/WQE/buffer错误delta；Task46快照复算为rx13/rx16 empty/slow +8/+10，buff_alloc_err/wqe_err为0。Task48精确源码确认R1的rx_out_of_buffer没有逐队列归属；下一步限于有资源门的1GiB lane-count复现，无性能结论 |
| DONE / SOURCE AUDIT ONLY | Task48 精确mlx5源码核验 | [计划](tasks/2026-11-11-v3-rx-oob-driver-source-audit-48.md)与[结果](tasks/2026-11-11-v3-rx-oob-driver-source-audit-48-result.md)：精确Ubuntu tag确认rx_out_of_buffer是聚合qcnt字段，没有RQ归属；现有只读接口未找到逐RQ OOB；下一步为有界1GiB 1-lane/4-lane复现 |
| DONE / ABORTED FIRST SAMPLE | Task49 lane诊断与停止门 | [结果](tasks/2026-11-11-v3-rx-lane-diagnostic-49-result.md)：1-lane pwrite首样本OOB/missed +171，4-lane未运行；SHA/资源/清理通过，无性能结果；停止门工程交付完成 |
| DONE / DIAGNOSTIC ONLY | Task50有界发送准入 | [结果](tasks/2026-11-11-v3-sender-pacing-50-result.md)：四lane共4096帧实际受控，机制/正确性通过；唯一1GiB为28.221Gbps，低于40Gbps准入，未运行新8GiB矩阵；默认off，不推荐性能配置 |
| DONE / NOT ADMITTED | Task51 timerfd等待修复 | [结果](tasks/2026-11-12-v3-pacing-wait-51-result.md)：相关回归两端各241；唯一1GiB36.478Gbps、网络门通过，但未达40准入、无收益对照 |
| DONE / FEASIBILITY ADMITTED | Task57 必要目标准备 | [结果](tasks/2026-11-12-v3-private-setup-57-result.md)：有界native supervisor、日志完整性修复、两端各261项及各10次smoke通过；五对准备节省中位44.38ms，唯一1GiB完整目标40.865Gbps、全门通过。只准入后续新合同，正式M2未运行 |
| DONE / NOT ADMITTED | Task56 内核TCP pacing | [结果](tasks/2026-11-12-v3-kernel-tcp-pacing-56-result.md)：四lane socket配置通过，唯一1GiB完整目标35.388Gbps，工程/资源/网络门通过；速率不足，不追加样本，默认off |
| DONE / NETWORK STOP | Task52 TCP exact-read | [结果](tasks/2026-11-12-v3-tcp-exact-read-52-result.md)：两端254项通过，4096大负载/4096recv；原始36.011Gbps因OOB/missed各+52不作合格成绩，默认off、未运行新矩阵 |
| ENGINEERING CLOSED / M2 OPEN | 本阶段集成与边界收尾 | [Task50收尾](tasks/2026-11-11-v3-sender-pacing-50-result.md)：两端相关CTest各126/126，开启候选额外4/4；冻结配置、资源/清理和版本证据完成。原生RDMA仅评估MR预算/所有权重启条件，继续暂停；不自动追加诊断或启动DEFERRED idea |
| DONE / NOT REPRODUCED | host `pswpout`归因诊断 | [任务43结果](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)：两端120秒无payload观察均稳定，未定位M1-41单页增量；M1-44已通过独立host swap-out门，但这不改变M1-41旧结果；不重跑M1-41 |
| DEFERRED | S1B摘要缓存默认晋级 | 保留显式收益；等待对应环境/大文件回归门，不抢占大文件主线 |
| DEFERRED | 小文件固定/自适应成员微批 | 独立完成时间目标；利用S0/S1及S2证据，后续设计成员恢复/事务，不包装已有pending-window为新微批 |
| DEFERRED / DESIGN ONLY | 统一工作单元与预算感知策略记忆 | [设计备忘](ideas/2026-11-11-unified-work-unit-and-budgeted-policy-memory.md)：大文件range/小文件聚合统一抽象，粒度选择与BACS-M思路扩展；当前仅存档，不实现、不替代M2 |
| DIAGNOSTIC DONE / CAUSE OPEN | 网络稳定性与原生RDMA | 诊断40当前固定路径一次未复现；37历史候选总 `rx_missed_errors` +2,169、256KiB对照+36仍无队列/流级归因。任务43的host swap事件观察未复现且不能解释M1-41单页变化；M1-44的10个TCP样本均无rx_missed_errors增量；旧网络因果仍开放，原生RDMA运行时优化继续暂停 |

### 已结束候选的处置

- sender-inline、population、direct-receive、seed+collapse、source-mmap当前具体候选不原样重跑。
- Task52 exact-read机制成立但网络停止门触发、无吞吐收益证据，保持默认off；不重复小负载或参数扫描。
- Task50应用发送pacer机制成立，速率准入未达；保留默认off诊断，不扫描sleep/rate/lane参数或追加小负载。
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
- sender pacing默认off；1500000000仅为plain TCP、1..4 lanes显式诊断，不向TLS/RDMA/压缩外推。
- 小文件summary默认original、cached仅显式；tree serializer默认original；详细profile默认off。
  新鲜下载auto pending-window实际仍为2，历史窗口收益不外推。
- 源/输出各≤8GiB、MemAvailable≥12GiB、owned swap及host swap-out增量为0，正式源驻留；
  原资源门保持。≤1GiB任务也必须满足门；8GiB阻塞时不能以换计数基线或预热排除成本过门。
- 同提交、同配置构建/版本/二进制SHA核对后才能互传；数据显式bind192.168.100.2/.1并记录路由/计数。
- 每个成功任务保留CRC、V3工作租约、checkpoint/恢复/原子发布及外部SHA；失败吞吐不得用planned bytes。
- 仅R770-1写源码/Git，R770-2接收固定提交；不改共享网络、THP、swap或服务；不使用子代理/worktree。

详细复现配置见[三阶段指南](V3_TRANSFER_STAGES.md)、scope17/35；服务器和构建见[环境](coordination/ENVIRONMENT.md)。
历史研究/生产边界见[研究基线](RESEARCH_BASELINE.md)，结果索引见[文档导航](README.md)。
