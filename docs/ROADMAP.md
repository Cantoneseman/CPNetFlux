# CPNetFlux 性能路线图

更新日期：2026-11-10。本页只维护当前路线；配置、完整样本、失败和历史结论见链接的结果记录。

## 当前状态

- V3 三阶段工程已收口；大文件性能门未过。[Kernel Service17](tasks/2026-11-09-v3-kernel-service-17-result.md)的无预算候选暂停仍有效。[Byte Work18](tasks/2026-11-09-v3-byte-work-18-result.md)的大帧/CRC整任务预算未证实；[Mapped Collapse19](tasks/2026-11-09-v3-mapped-collapse-19-result.md)和[Source Read20](tasks/2026-11-09-v3-source-read-20-result.md)当前候选不集成。S0完成，[S1 Summary Path22](tasks/2026-11-09-v3-summary-path-22-result.md)显式摘要缓存受测小文件过门；[S1B Gate23](tasks/2026-11-09-v3-summary-default-gate-23-result.md)资源门阻塞，默认仍original、cached显式启用。[S2C Runtime26](tasks/2026-11-09-v3-tree-serialization-runtime-26-result.md)完整任务慢2.50%，性能NO-GO；[S2D Budget27](tasks/2026-11-09-v3-tree-state-budget-27-result.md)清单直接同步预算不足10%，不开发增量清单候选。[S2E接收端28](tasks/2026-11-09-v3-per-file-budget-28-result.md)与[S2F发送端29](tasks/2026-11-09-v3-sender-credit-budget-29-result.md)生命周期归因完成，线程复用未过保守≥10%预算门。[S2G事务审计30](tasks/2026-11-10-v3-receiver-transaction-audit-30-result.md)限一轮已完成，未建立可消除机制及预算，逐文件性能归因现已暂停。原序列化继续默认，buffered仅显式复现，profile默认关闭。[Publication31](tasks/2026-11-10-v3-publication-reconcile-31-result.md)已修复可证明自有hard-link发布后的CRC32C恢复缺口，两端各130项相关测试通过；本轮无吞吐收益。下一项为[TCP Buffer Ownership32](tasks/2026-11-10-v3-tcp-buffer-ownership-32.md)的有界设计/诊断，尚未实现；RDMA、线程/队列扩张和微批实现继续暂停。
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
| S2D清单归因 | 默认 `CPNETFLUX_TREE_MANIFEST_PROFILE=0`；1仅任务级诊断 | 单独输出复制/销毁、排序/编码、写入/发布与去重等待；这些嵌套/重叠区间不能相加为收益 |
| S2E成员归因 | 默认 `CPNETFLUX_MEMBER_WORK_PROFILE=0`；1仅任务级诊断 | 本地成功whole-file接收的线程/事务/result写入汇总；上传明确not_used_sender，范围/失败/截断不得外推整任务 |
| S2F发送端归因 | 默认 `CPNETFLUX_SENDER_WORK_PROFILE=0`；1在CLI入口/服务器启动时冻结 | producer生命周期、结果读取及窗口等待分类；跨通道union不等于所有通道空闲，不能与接收端时间直接相加 |

实验必须保留 CRC、V3 WorkExecutor/租约、checkpoint、恢复和原子发布。TCP 明确绑定 `192.168.100.2/.1`，保留路由/接口证据；经 RDMA 网卡的 TCP 不称为 verbs。
单次源和输出各 ≤8 GiB，MemAvailable ≥12 GiB、owned swap 和主机 swap-out 增量为零；校验后清理自有 payload。服务器、构建与同步规程见[环境](coordination/ENVIRONMENT.md)。

## 未通过的门

| 门 | 当前缺口 / 通过条件 |
|---|---|
| 大文件吞吐 | 30–50 Gbps 中期门未过；70–85 Gbps、85–90 Gbps 探索及 100 Gbps 均未验收，1 Tbps 多端口另立范围 |
| 新候选保留 | 先有可消除机制和保守关键路径收益预算，再做固定提交、同配置的完整任务 A/B；至少五对、中位收益 ≥10%、CV ≤5%，完整性和资源无回退；局部服务率/CPU 降幅不替代此门 |
| 网络与 RDMA 稳定性 | scope17 的接口 +94 尚未关联到具体连接；历史 RDMA CQ/flush/peer-mapping 问题仍开。内容正确不等于链路稳定 |
| 发布恢复边界 | Publication31已修复默认非覆盖、CRC32C、完整检查点且目标/temp同一自有inode的link后恢复；先保存committed再成功返回，保存/清理失败可重试。两端各130项通过，CLI双向恢复及再次恢复均0重传；这是分阶段状态重建，非SIGKILL/掉电验收。overwrite/rename、无校验transferring及掉电持久化不在新增保证内 |
| 适用范围 | 当前路径的同提交单路、反向、TLS-on、磁盘持久化、混合负载及长时间稳定性尚未完成验收 |
| 小文件研究 | S0/S1显式路径缓存受限验收完成；S1B共享默认/大文件回归仍受资源门阻塞；S2C整任务性能NO-GO，S2D清单直接同步预算不足10%；S2E/F两端归因与S2G事务审计完成，逐文件性能归因暂停；≥20%微批墙钟预算仍未证实 |

已关闭的 sender-inline、population、direct-receive 候选不重开；结果保留在[对应记录](README.md)。研究历史不能外推为生产 readiness，见[研究边界](RESEARCH_BASELINE.md)。

## 下一项工作

**下一项：[TCP Buffer Ownership32](tasks/2026-11-10-v3-tcp-buffer-ownership-32.md)，先完成发送缓冲所有权设计和主机能力检查，可行后只做一个独立零拷贝发送诊断；尚未实现，不直接接入V3运行时。** [Publication31](tasks/2026-11-10-v3-publication-reconcile-31-result.md)正确性前置任务已完成，不算吞吐收益。逐文件性能归因继续暂停，S1B默认晋级暂缓；Audit30未建立每文件3次保存的冗余机制及≥10%整任务预算，不删保存或改ACK。正常新建CRC32C/verified_chunks完成没有额外verifyPublished全文回读，不能以省去不存在的扫描设计候选。

当前普通sendmsg返回后可回收vector；零拷贝诊断须有界持有header/CRC/payload直到实际完成通知，覆盖短写、取消、合并/乱序/回绕通知与复制回退。loopback只测正确性；实际复制避免和性能须走双机直连网卡并保留计数。先小探针，再至多五对1GiB诊断；局部墙钟≥15%、CV≤5%以及保守≥10%整任务预算均成立后，才另立运行时集成任务。CPU下降或理论少一遍复制均不替代该门；失败就收口，不重跑参数求过门。内存池建议每路≤4MiB、合计≤16MiB，上限64MiB，保持两端12GiB可用内存和零换出门，RDMA仍暂停。

1. 真实MappedFileWindow已确认1GiB大页和fault大幅下降，但五对ordinary CV=5.124%超门，准备/写入/关闭CPU基本不变。诊断whole墙钟下降26.04%；两对归因补测显示大部分差来自close/unlink，自有实验文件销毁不代表真实传输T_ready。候选不接入V3，不重测挑CV，不再调种子/窗口。
2. LF5当前候选已结束：1GiB/4路五对，同CRC/切分、map/PTE/CRC/close全计时；CPU下降8.205%但墙钟仅3.188%，两臂CV<2%。省一遍显式复制同时增加约1.64万minor faults，不足以支持运行时集成；不重跑调参，不把诊断服务率外推完整任务。TCP零拷贝/原生RDMA更大改动仍有独立设计门，不宣称全部大文件方向已穷尽。
3. 大帧/CRC暂缓运行时开发，未判为已淘汰。Byte Work18中1GiB写调用17408→1024，冷写fault仍262144、冷写CPU基本不变；CRC原语有成本但整任务预算未证实。以后有新关键路径预算才恢复LF1/LF2/LF3，独立有收益再测组合，不能相加。scope17接口+94仍未关联，网络门继续开放。
4. 源映射借用必须覆盖queued消费者实际使用期，取消/部分写/源变更和预算先过门；区分逻辑复制边界、CRC扫描和实测DRAM流量。TCP完成通知、buffer pin/回收或RDMA注册池/CQ所有权有独立设计门；RDMA仍暂停，不先修改系统网络。
5. S0完成33次；其原时间保留为采集器循环口径，S1发现退出后ss耗时可能混入，不跨口径计算收益。全新下载auto仍为2，显式8未过门。详细计时开销约18%，基线保持off。S1显式缓存受测小文件过门，S1B默认切换受资源门阻塞；S2C完整任务未过门，S2D清单同步预算不足，S2E/F与S2G归因审计完成，该性能分支暂停。固定批和adaptive仍各自要求T_ready中位下降≥20%、p95/恢复不恶化、大文件回退≤5%，不替代大文件未过门结论。
