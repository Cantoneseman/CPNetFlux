# CPNetFlux 性能路线图

更新日期：2026-11-09。本页只维护当前路线；配置、完整样本、失败和历史结论见链接的结果记录。

## 当前状态

- V3 三阶段工程已收口；大文件性能门未过。[Kernel Service17 结果](tasks/2026-11-09-v3-kernel-service-17-result.md)的无预算候选暂停仍有效；按[后续开发计划](tasks/2026-11-09-v3-large-file-next-plan.md)，先做一轮有界机制/收益预算核对，条件筛选 TCP 大负载帧及 CRC 拼接算子，随后分别研究接收页面管理和数据搬运，再进入小文件固定开销基线。[Byte Work18结果](tasks/2026-11-09-v3-byte-work-18-result.md)已完成局部诊断，大帧/CRC整任务预算未证实；发现per-mapping collapse大页可行性，下一项转入真实窗口诊断，完整任务尚无新收益；RDMA 优化、线程/队列扩张继续暂停。
- 最新同轮完整任务 baseline/profile 中位数为 **27.291/27.296 Gbps**（三对），仅验证采样开销，没有新吞吐收益；输入 `025c38a9`，运行时未改。内容、V3 终态和资源门通过，sample02 接收接口 `rx_missed_errors` 增加 94，网络计数门未清洁。
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
| 密集小文件的既有对照 | 显式 `--data-pending-window auto`；≥10K 文件且平均 ≤64 KiB 时选 8，否则 2 | 默认仍 2；旧优化按原提交和样本解释，新基线需固定当前提交重测 |

实验必须保留 CRC、V3 WorkExecutor/租约、checkpoint、恢复和原子发布。TCP 明确绑定 `192.168.100.2/.1`，保留路由/接口证据；经 RDMA 网卡的 TCP 不称为 verbs。
单次源和输出各 ≤8 GiB，MemAvailable ≥12 GiB、owned swap 和主机 swap-out 增量为零；校验后清理自有 payload。服务器、构建与同步规程见[环境](coordination/ENVIRONMENT.md)。

## 未通过的门

| 门 | 当前缺口 / 通过条件 |
|---|---|
| 大文件吞吐 | 30–50 Gbps 中期门未过；70–85 Gbps、85–90 Gbps 探索及 100 Gbps 均未验收，1 Tbps 多端口另立范围 |
| 新候选保留 | 先有可消除机制和保守关键路径收益预算，再做固定提交、同配置的完整任务 A/B；至少五对、中位收益 ≥10%、CV ≤5%，完整性和资源无回退；局部服务率/CPU 降幅不替代此门 |
| 网络与 RDMA 稳定性 | scope17 的接口 +94 尚未关联到具体连接；历史 RDMA CQ/flush/peer-mapping 问题仍开。内容正确不等于链路稳定 |
| 适用范围 | 当前路径的同提交单路、反向、TLS-on、磁盘持久化、混合负载及长时间稳定性尚未完成验收 |
| 小文件研究 | 新提交下的 tuned native、固定成员微批、adaptive 比较未完成；T_ready/p95/恢复和大文件回退必须分别验收 |

已关闭的 sender-inline、population、direct-receive 候选不重开；结果保留在[对应记录](README.md)。研究历史不能外推为生产 readiness，见[研究边界](RESEARCH_BASELINE.md)。

## 下一项工作

**LF4：真实MappedFileWindow的大页准备/写入诊断，待开发。** [Byte Work18结果](tasks/2026-11-09-v3-byte-work-18-result.md)已完成局部诊断与16MiB可行性探针。详细依赖见[计划](tasks/2026-11-09-v3-large-file-next-plan.md)及[路线决定](DECISIONS/2026-11-09-v3-large-file-candidate-screening.md)。

1. 先验证实际有界窗口：R1 probe已得到ShmemPmdMapped=16MiB、minor faults 4096→8，尚非吞吐结果。准备/映射/seed/collapse/写入/关闭全部计时；保护已有verified内容、租约/attempt和范围边界。映射仍≤64MiB/lane、总≤256MiB，不改共享THP/mount。
2. 先做≤1GiB有限局部筛选，再决定V3文件集成。正式保留仍需同提交双端、五对新8GiB任务、中位吞吐≥10%、CV≤5%、至少4/5配对改善及完整性/恢复/资源/网络门；失败关闭该页策略，不重开旧population。
3. 大帧/CRC暂缓运行时开发，未判为已淘汰。Byte Work18中1GiB写调用17408→1024，冷写fault仍262144、冷写CPU基本不变；CRC原语有成本但整任务预算未证实。以后有新关键路径预算才恢复LF1/LF2/LF3，独立有收益再测组合，不能相加。scope17接口+94仍未关联，网络门继续开放。
4. LF5页面阶段后再研究数据搬运，区分逻辑复制边界、CRC扫描和实测DRAM流量。TCP完成通知、buffer pin/回收或RDMA注册池/CQ所有权有独立设计门；RDMA仍暂停，不先修改系统网络。
5. S0小文件独立研究保留：4/16/64/256KiB×1K/10K可行子集，总量≤1GiB；tuned native包含pending-window auto。固定成本主导才设计成员微批；固定批及adaptive分别要求完整T_ready中位下降≥20%、p95/恢复不恶化、大文件回退≤5%。不替代大文件未过门结论。
