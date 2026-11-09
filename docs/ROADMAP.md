# CPNetFlux 性能路线图

更新日期：2026-11-10。本页只维护当前路线；配置、完整样本、失败和历史结论见链接的结果记录。

## 当前状态

- V3 三阶段工程已收口；大文件性能门未过。[Receiver Service Budget33](tasks/2026-11-10-v3-receiver-service-budget-33-result.md)已复核scope17/32证据和当前热路径：chunk CRC确在共享锁内，但现有栈无法区分锁内/会话CRC成本，未建立可消除机制及保守≥10%整任务预算；当前大文件接收端软件候选分支暂停，不自动追加profiler或并发扫描。[TCP Buffer Ownership32](tasks/2026-11-10-v3-tcp-buffer-ownership-32-result.md)发送CPU下降19.84%、服务墙钟增加1.81%，已停止，不接入V3。[S1B Recheck34](tasks/2026-11-10-v3-summary-default-recheck-34-result.md)两端空闲资源窗口通过，但唯一8GiB准备后首次驻留检查失败、主机发生换出，零性能样本；已修复准备/SHA/驻留资源记录与基线跨阶段缺口，不重试。本轮无运行时提速；RDMA、线程/队列扩张和微批实现继续暂停。
- 小文件S0/S1显式摘要缓存受限通过，S1B默认晋级仍受资源/驻留门阻塞；scope34写入阶段记录为零换出，写入记录到服务启动之间却增加84863页主机换出，初次性能样本未启动。默认original、cached显式选择。[S2C Runtime26](tasks/2026-11-09-v3-tree-serialization-runtime-26-result.md)整任务慢2.50%，S2D清单同步、S2E/F两端生命周期及[S2G事务审计30](tasks/2026-11-10-v3-receiver-transaction-audit-30-result.md)均未建立可消除机制及保守≥10%预算，逐文件性能归因暂停。原序列化默认、profile关闭。[Publication31](tasks/2026-11-10-v3-publication-reconcile-31-result.md)已修复可证明自有hard-link发布后的CRC32C恢复缺口；scope32含其回归，两端各140项相关测试通过。
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
| S1小文件实验摘要 | 任务级 `CPNETFLUX_SUMMARY_PATH=cached`；默认仍original；保留原有内容读取与CRC | 仅改变filesystem_read摘要路径；verified_chunks复用时effective=not_used。三组受测小文件过门；S1B34空闲窗口通过仍未满足受载驻留/零换出门，默认晋级与大文件回归暂缓 |
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

**下一独立待办：解释S1B受载准备期间的回收/换出与资源归属。** [S1B Recheck34](tasks/2026-11-10-v3-summary-default-recheck-34-result.md)唯一尝试在首个客户端启动前结束。空闲窗口、高MemAvailable和进程VmSwap0不足以保证源驻留及主机零换出；当前SSH/上层cgroup读数也不是原准备进程的因果时间线。先基于已保存证据明确剩余缺口，任何新诊断需独立限定范围并覆盖同一准备进程/cgroup的写入、SHA、驻留生命周期；当前范围不允许再次准备8GiB，不凭另一个空闲窗口重试，不改系统策略或降低门槛。

scope34两端各241样本/60秒窗口通过，最低可用约28.54/28.00GiB；8GiB写入记录仍为零换出，随后到服务启动之前R2 pswpout2207568→2292431。原失败驻留页数未保存，不能用84863页主机换出来反推源缺失页数。已修复SHA/驻留后检查、原始换出基线延续、失败报告落盘和中止时80阶段断言掩盖问题；两端各19项相关测试通过，12项驱动单测通过，修复后未重跑准备。自有源/目标和隔离服务已清理，默认仍original、cached显式选择；S1B仍HOLD，无新Gbps成绩。

[Receiver Service Budget33](tasks/2026-11-10-v3-receiver-service-budget-33-result.md)的接收端候选暂停仍有效；每GiB17408帧、34816次两层CRC拼接不能代替可消除的关键路径预算。重新开展大文件运行时开发须有新机制及保守≥10%完整任务收益预算，不自动扩大并发。S1B默认验收不能替代大文件100G目标。

[TCP Ownership32](tasks/2026-11-10-v3-tcp-buffer-ownership-32-result.md)有界池和完成通知正确性已验证，物理网卡没有报告COPIED回退，仍未获得墙钟收益；该候选停止，不重跑调参、不接入运行时。两端测试及12次互传完整性通过，内存池合计8.5MiB、最大单源/目标1GiB、最小可用内存约27.04GiB，采样owned swap及host swap-out增量均0；新接口错误增量0不消除scope17历史+94。逐文件归因仍暂停，S1B默认晋级未通过。不删保存或改ACK；正常verified_chunks完成没有额外verifyPublished全文回读，不能省去不存在的扫描。

1. 真实MappedFileWindow已确认1GiB大页和fault大幅下降，但五对ordinary CV=5.124%超门，准备/写入/关闭CPU基本不变。诊断whole墙钟下降26.04%；两对归因补测显示大部分差来自close/unlink，自有实验文件销毁不代表真实传输T_ready。候选不接入V3，不重测挑CV，不再调种子/窗口。
2. LF5当前候选已结束：1GiB/4路五对，同CRC/切分、map/PTE/CRC/close全计时；CPU下降8.205%但墙钟仅3.188%，两臂CV<2%。省一遍显式复制同时增加约1.64万minor faults，不足以支持运行时集成；不重跑调参，不把诊断服务率外推完整任务。后续TCP零拷贝32也未过墙钟门；原生RDMA保持独立设计门和暂停范围，不宣称全部大文件方向已穷尽。
3. 大帧/CRC暂缓运行时开发，未判为已淘汰。Byte Work18中1GiB写调用17408→1024，冷写fault仍262144、冷写CPU基本不变；CRC原语有成本但整任务预算未证实。以后有新关键路径预算才恢复LF1/LF2/LF3，独立有收益再测组合，不能相加。scope17接口+94仍未关联，网络门继续开放。
4. 源映射借用必须覆盖queued消费者实际使用期，取消/部分写/源变更和预算先过门；区分逻辑复制边界、CRC扫描和实测DRAM流量。TCP32的进程终止保留策略不能代替生产异步取消设计；RDMA注册池/CQ所有权仍有独立设计门，RDMA保持暂停，不先修改系统网络。
5. S0完成33次；其原时间保留为采集器循环口径，S1发现退出后ss耗时可能混入，不跨口径计算收益。全新下载auto仍为2，显式8未过门。详细计时开销约18%，基线保持off。S1显式缓存受测小文件过门，S1B默认切换受资源门阻塞；S2C完整任务未过门，S2D清单同步预算不足，S2E/F与S2G归因审计完成，该性能分支暂停。固定批和adaptive仍各自要求T_ready中位下降≥20%、p95/恢复不恶化、大文件回退≤5%，不替代大文件未过门结论。
