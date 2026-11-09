# CPNetFlux 性能路线图

更新日期：2026-11-09。本页只维护当前路线；配置、完整样本、失败和历史结论见链接的结果记录。

## 当前状态

- V3 三阶段工程已收口；大文件性能门未过。[Kernel Service17](tasks/2026-11-09-v3-kernel-service-17-result.md)的无预算候选暂停仍有效。[Byte Work18](tasks/2026-11-09-v3-byte-work-18-result.md)的大帧/CRC整任务预算未证实；[Mapped Collapse19](tasks/2026-11-09-v3-mapped-collapse-19-result.md)和[Source Read20](tasks/2026-11-09-v3-source-read-20-result.md)当前候选不集成。S0完成，[S1 Summary Path22](tasks/2026-11-09-v3-summary-path-22-result.md)显式摘要缓存受测小文件过门；[S1B Gate23](tasks/2026-11-09-v3-summary-default-gate-23-result.md)资源门阻塞，默认仍original、cached显式启用。[S2归因24](tasks/2026-11-09-v3-small-file-fixed-work-24-result.md)、[S2B局部序列化25](tasks/2026-11-09-v3-tree-serialization-25-result.md)之后，[S2C Runtime26](tasks/2026-11-09-v3-tree-serialization-runtime-26-result.md)已接入显式候选并完成五对完整任务：中位1.18330→1.21285秒，慢2.50%，性能NO-GO。原序列化继续默认；候选仅留显式复现。RDMA、线程/队列扩张和微批实现继续暂停。
- 最近的大文件同轮完整任务 baseline/profile 中位数为 **27.291/27.296 Gbps**（三对），仅验证采样开销，没有新大文件吞吐收益；输入 `025c38a9`，运行时未改。内容、V3 终态和资源门通过，sample02 接收接口 `rx_missed_errors` 增加 94，网络计数门未清洁。
- 上述成绩限定为 R770-2 → R770-1、单个 8 GiB 驻留 tmpfs 源及全新 tmpfs 目标、4 路 TCP、queued 发送/inline 实验 mmap 接收、CRC32C/verified_chunks、TLS off、commit_sync=none；不是默认、物理磁盘或持久化性能。
- [Observation Audit16](tasks/2026-11-09-v3-observation-audit-16-result.md) 的 detail-off 中位数为 27.858 Gbps，计时合同与 scope17 不同，不能跨轮计算收益。scope13 之前的约 28–29 Gbps 数据还须结合[本地 V3 executor 修复](tasks/2026-11-09-v3-receiver-critical-13-result.md)解释。
- 小文件已有[显式 pending-window 优化](tasks/2026-11-08-v3-protocol-microbatch-03.md)；它保留逐文件协议，不等于完成成员聚合或自适应微批。原始样本与历史决策通过[结果导航](README.md)查找。

## 推荐配置

以下区分代码默认和受限实验配置，不作为生产部署建议。完整参数及所有权规则见[三阶段指南](V3_TRANSFER_STAGES.md)、[映射写边界](tasks/2026-11-08-v3-mapped-download-08-result.md)和 scope16/17 复现合同。

| 用途 | 选择 | 使用边界 |
|---|---|---|
| 默认行为对照 | sender/receiver 均 queued、pwrite；read0/write0、depth2/batch1；保留默认 full 校验策略 | 不用实验成绩代表默认性能，不改全局默认 |
| 复现当前大文件基线 | sender queued；receiver 显式 inline + mmap；4 connections、4 路活跃 range；POSIX direct、read0/write0、depth2/batch1 | 仅隔离的单任务下载 CLI；遵守 mmap 文件/range 限制；每路窗口 ≤64 MiB、总映射 ≤256 MiB、payload 预算 64 MiB |
| 上述基线的校验与观测 | CRC32C/verified_chunks、checkpoint16、无预分配/压缩；TLS off、commit_sync=none；range/payload detail 及新增 CPU profile 关闭 | 保留基础 phase；源驻留/SHA 在计时外；scope17 另有共同 /proc 采集器和 gate 计时，不混用口径 |
| 密集小文件的既有对照 | 默认窗口2；auto仅在选择时已有清单且≥10K/平均≤64KiB时选8 | S0发现全新下载选择时清单为空，auto实际为2；显式8对照收益混合且不足3%，不改默认，不把历史window1收益当本轮成果 |
| S1小文件实验摘要 | 任务级 `CPNETFLUX_SUMMARY_PATH=cached`；默认仍original；保留原有内容读取与CRC | 仅改变filesystem_read摘要路径；verified_chunks复用时effective=not_used。三组受测小文件过门；S1B源准备出现主机换出，默认晋级与大文件回归暂缓 |
| S2C序列化复现 | 默认 `CPNETFLUX_TREE_MANIFEST_SERIALIZER=original`；buffered仅显式复现 | 4KiB×10K完整任务未过门，不推荐用于提速；逐字节格式、CRC、恢复与保存策略不变 |

实验必须保留 CRC、V3 WorkExecutor/租约、checkpoint、恢复和原子发布。TCP 明确绑定 `192.168.100.2/.1`，保留路由/接口证据；经 RDMA 网卡的 TCP 不称为 verbs。
单次源和输出各 ≤8 GiB，MemAvailable ≥12 GiB、owned swap 和主机 swap-out 增量为零；校验后清理自有 payload。服务器、构建与同步规程见[环境](coordination/ENVIRONMENT.md)。

## 未通过的门

| 门 | 当前缺口 / 通过条件 |
|---|---|
| 大文件吞吐 | 30–50 Gbps 中期门未过；70–85 Gbps、85–90 Gbps 探索及 100 Gbps 均未验收，1 Tbps 多端口另立范围 |
| 新候选保留 | 先有可消除机制和保守关键路径收益预算，再做固定提交、同配置的完整任务 A/B；至少五对、中位收益 ≥10%、CV ≤5%，完整性和资源无回退；局部服务率/CPU 降幅不替代此门 |
| 网络与 RDMA 稳定性 | scope17 的接口 +94 尚未关联到具体连接；历史 RDMA CQ/flush/peer-mapping 问题仍开。内容正确不等于链路稳定 |
| 适用范围 | 当前路径的同提交单路、反向、TLS-on、磁盘持久化、混合负载及长时间稳定性尚未完成验收 |
| 小文件研究 | S0/S1显式路径缓存受限验收完成；S1B共享默认/大文件回归仍受资源门阻塞；S2/S2B完成，S2C整任务性能NO-GO；下一候选须先证明关键路径预算，≥20%微批墙钟预算仍未证实 |

已关闭的 sender-inline、population、direct-receive 候选不重开；结果保留在[对应记录](README.md)。研究历史不能外推为生产 readiness，见[研究边界](RESEARCH_BASELINE.md)。

## 下一项工作

**下一项是结构性工作量预算复核；S2C候选关闭，S1B默认晋级暂缓。** [Runtime26](tasks/2026-11-09-v3-tree-serialization-runtime-26-result.md)五对全部更慢，CV约1.2%；清单快照中位220→304、序列化字节54.4→85.9MB、累加锁等待0.122→0.198秒，保存策略仍是32事件/100ms。结果与“单次保存更快、写入期间自然合并减少、工作量放大”的机制相符，但尚未证明各因素的因果份额；这些区间与传输重叠，不能直接当成可消除墙钟。停止该候选性能重测，不做其默认晋级、负对照和大文件矩阵，不调阈值/延迟。下轮先证明全量快照复制、排序、发布中可削减工作的保守关键路径预算；有预算才设计增量表示，并独立覆盖崩溃恢复和最终屏障。增量清单目前只是方向，未实现或批准修改格式；预算不足则关闭此方向，复核其他逐文件固定成本，再判断是否值得开发微批。

1. 真实MappedFileWindow已确认1GiB大页和fault大幅下降，但五对ordinary CV=5.124%超门，准备/写入/关闭CPU基本不变。诊断whole墙钟下降26.04%；两对归因补测显示大部分差来自close/unlink，自有实验文件销毁不代表真实传输T_ready。候选不接入V3，不重测挑CV，不再调种子/窗口。
2. LF5当前候选已结束：1GiB/4路五对，同CRC/切分、map/PTE/CRC/close全计时；CPU下降8.205%但墙钟仅3.188%，两臂CV<2%。省一遍显式复制同时增加约1.64万minor faults，不足以支持运行时集成；不重跑调参，不把诊断服务率外推完整任务。TCP零拷贝/原生RDMA更大改动仍有独立设计门，不宣称全部大文件方向已穷尽。
3. 大帧/CRC暂缓运行时开发，未判为已淘汰。Byte Work18中1GiB写调用17408→1024，冷写fault仍262144、冷写CPU基本不变；CRC原语有成本但整任务预算未证实。以后有新关键路径预算才恢复LF1/LF2/LF3，独立有收益再测组合，不能相加。scope17接口+94仍未关联，网络门继续开放。
4. 源映射借用必须覆盖queued消费者实际使用期，取消/部分写/源变更和预算先过门；区分逻辑复制边界、CRC扫描和实测DRAM流量。TCP完成通知、buffer pin/回收或RDMA注册池/CQ所有权有独立设计门；RDMA仍暂停，不先修改系统网络。
5. S0完成33次；其原时间保留为采集器循环口径，S1发现退出后ss耗时可能混入，不跨口径计算收益。全新下载auto仍为2，显式8未过门。详细计时开销约18%，基线保持off。S1显式缓存受测小文件过门，S1B默认切换受资源门阻塞；S2/S2B完成、S2C完整任务未过门。固定批和adaptive仍各自要求T_ready中位下降≥20%、p95/恢复不恶化、大文件回退≤5%，不替代大文件未过门结论。
