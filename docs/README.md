# CPNetFlux 文档导航

最新局部准入：[私有tmpfs首次大页54](tasks/2026-11-12-v3-private-tmpfs-54-result.md)：真实新目标窗口五对，worker CPU/墙钟和中位约降低49.8%/50.7%，5/5改善且CV约1.1%；无全局设置变更。下一阶段为默认关闭、有界私有目标后端的完整V3≤1GiB验证；尚无新跨机性能成绩，M2及网络稳定性仍开放。

最新修复：[范围记录ID水位53](tasks/2026-11-12-v3-record-id-watermark-53-result.md)已消除分配ID的二次扫描；两端各251项相关回归通过。局部收益已量化，无新跨机性能样本，M2/网络稳定性仍开放。下一步核验私有有界tmpfs首次大folio分配机制，不修改全局THP或共享挂载。

交付C历史快照已核对Task51/52版本：[冻结清单](tasks/2026-11-12-performance-delivery-c-freeze.json)、[交付结果与边界](tasks/2026-11-12-performance-delivery-c.md)。工程交付完成，M2/网络稳定性开放；历史Task50冻结保留原版本，不作为新代码性能成绩。


近期交付：[TCP exact-read 52](tasks/2026-11-12-v3-tcp-exact-read-52-result.md)：双端254项相关回归通过，读取机制确认；唯一1GiB触发OOB/missed各+52停止门，未产生新合格性能成绩，M2仍开放。

**当前性能主线只维护在[路线图](ROADMAP.md)。** 本页负责导航，详细数据、失败与验收命令留在各任务/结果中；历史结论按其输入提交和环境解释。

## 当前入口

| 要查什么 | 文档 |
|---|---|
| 项目、服务器与开发入口 | [项目 README](../README.md)、[开发规则](../AGENTS.md)、[R770 环境](coordination/ENVIRONMENT.md) |
| 当前状态、配置、未过门与下一项 | [性能路线图](ROADMAP.md) |
| 本阶段结束状态 | [收尾合同](tasks/2026-11-11-performance-stage-closeout-handoff.md)、[Task50收尾结果](tasks/2026-11-11-v3-sender-pacing-50-result.md)：ENGINEERING CLOSED / M2 OPEN；唯一1GiB速率未达40Gbps准入，未运行新8GiB矩阵 |
| 交付B：成绩与M2判定 | [离线复核与NO-GO结果](tasks/2026-11-11-v3-sender-pacing-50-result.md)：历史M1十个完整任务复算通过；A未达40Gbps准入，正式M2未运行、未测项不计通过 |
| 交付C：最终冻结与边界 | [配置/版本冻结清单](tasks/2026-11-11-v3-sender-pacing-50-freeze.json)、[C销项结果](tasks/2026-11-11-v3-sender-pacing-50-result.md)：同运行时回归证据、现场清理与RDMA预算评估完成；阶段ENGINEERING CLOSED / M2 OPEN |
| 暂存研究 idea（本轮不开发） | [统一工作单元与预算感知策略记忆](ideas/2026-11-11-unified-work-unit-and-budgeted-policy-memory.md)：DEFERRED，需用户明确重启 |
| 当前批准主线与串行交接 | [里程碑35计划](tasks/2026-11-10-v3-large-file-milestone-35.md)、[实现/1GiB结果](tasks/2026-11-10-v3-large-file-milestone-35-result.md)、[M1-41计划](tasks/2026-11-11-v3-large-file-m1-41.md)、[M1-41结果：资源门中止、无吞吐样本](tasks/2026-11-11-v3-large-file-m1-41-result.md)、[M1-44计划](tasks/2026-11-11-v3-large-file-m1-44.md)、[M1-44结果：8GiB正式门通过](tasks/2026-11-11-v3-large-file-m1-44-result.md)、[M1-45接收端profile诊断](tasks/2026-11-11-v3-receiver-profile-diagnostic-45.md)、[Task46合同与结果](tasks/2026-11-11-v3-profile-overhead-counter-correlation-46.md)、[Task46中止结果](tasks/2026-11-11-v3-profile-overhead-counter-correlation-46-result.md)、[资源失败证据补全42结果](tasks/2026-11-11-v3-resource-gate-failure-evidence-42-result.md)、[host pswpout诊断43结果](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)、[分层准入与路线决定](DECISIONS/2026-11-10-v3-performance-milestones.md) |
| 近期诊断结果 | [无payload host pswpout归因43](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)：两端均未复现，M1-41单页事件仍未定位 |
| 最新执行结果 | [Task50计划](tasks/2026-11-11-v3-sender-pacing-50.md)、[Task50结果](tasks/2026-11-11-v3-sender-pacing-50-result.md)：有界应用发送pacer实际生效，唯一1GiB为28.221Gbps，内容/资源/网络窗口通过；未达正式矩阵准入，默认off、仅保留诊断能力 |
| 执行方式和缓冲所有权 | [三阶段指南](V3_TRANSFER_STAGES.md)、[映射下载边界](tasks/2026-11-08-v3-mapped-download-08-result.md) |
| 接收队列计数归因 | [Task47合同](tasks/2026-11-11-v3-rx-page-pool-counter-analysis-47.md)、[Task47结果](tasks/2026-11-11-v3-rx-page-pool-counter-analysis-47-result.md)、[Task48计划](tasks/2026-11-11-v3-rx-oob-driver-source-audit-48.md)、[Task48结果](tasks/2026-11-11-v3-rx-oob-driver-source-audit-48-result.md) |
| 当前开发 | [Task52结果](tasks/2026-11-12-v3-tcp-exact-read-52-result.md)：exact-read默认off、网络门FAIL，不追加矩阵；下一机制须有实际搬运/页管理工作量依据。原生RDMA和DEFERRED工作仍暂停 |
| 设计、协议与工程规范 | [设计](DESIGN.md)、[工程规范](ENGINEERING.md)、[可观测性](OBSERVABILITY.md)、[目录传输](DIRECTORY_TRANSFER.md) |
| 安全、发布与证据边界 | [安全](SECURITY.md)、[发布限制](release/ALPHA_LIMITATIONS.md)、[历史研究基线](RESEARCH_BASELINE.md) |

## 详细结果导航

| 主题 | 任务 / 结果 |
|---|---|
| 最近大文件候选评估 | [M1-41结果](tasks/2026-11-11-v3-large-file-m1-41-result.md)：首个control任务因host swap资源门中止，零完整样本；[任务42](tasks/2026-11-11-v3-resource-gate-failure-evidence-42-result.md)已补齐失败资源证据；[任务43结果](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)未复现host事件；[M1-44结果](tasks/2026-11-11-v3-large-file-m1-44-result.md)以34.159Gbps中位速率通过门槛；[M1-45/Task46](tasks/2026-11-11-v3-profile-overhead-counter-correlation-46-result.md)记录rx_out_of_buffer +237后中止；[Task47/48](tasks/2026-11-11-v3-rx-oob-driver-source-audit-48-result.md)确认该计数没有逐队列归属；M1-41仍不重跑。网络诊断40和CRC combine/8 GiB源驻留边界见[40](tasks/2026-11-11-v3-rx-queue-irq-correlation-40-result.md)、[39](tasks/2026-11-10-v3-source-residency-ownership-39-result.md) |
| 1MiB帧候选 | [DATA帧37结果](tasks/2026-11-10-v3-data-frame-1m-37-result.md)：五对1GiB吞吐未改善，接收miss计数增加，候选撤回；不再扫描帧尺寸 |
| 网络事件定向归因 | [诊断40结果](tasks/2026-11-11-v3-rx-queue-irq-correlation-40-result.md)：当前固定路径未复现；[诊断36](tasks/2026-11-10-v3-receive-network-36-result.md)给出硬件RQ缺缓冲层级，但历史触发机制和流级因果仍OPEN |
| 最新大文件基线与归因 | [内核服务17](tasks/2026-11-09-v3-kernel-service-17-result.md)、[观测开销16](tasks/2026-11-09-v3-observation-audit-16-result.md)、[本地 V3 executor 修复13](tasks/2026-11-09-v3-receiver-critical-13-result.md) |
| 写入路径与集成 | [lane/storage06](tasks/2026-11-08-v3-lane-storage-06.md)、[写后端07](tasks/2026-11-08-v3-write-backend-07.md)、[映射下载08](tasks/2026-11-08-v3-mapped-download-08-result.md)、[映射成本14](tasks/2026-11-09-v3-mapped-cost-14-result.md) |
| 已关闭候选与负结果 | [流水线交接](tasks/2026-11-07-v3-path-isolation-01-result.md)、[population10](tasks/2026-11-08-v3-mapped-populate-10-result.md)、[发送inline12](tasks/2026-11-08-v3-sender-service-12-result.md)、[直接接收15](tasks/2026-11-09-v3-direct-receive-15-result.md) |
| 小文件 | [逐文件基线01](tasks/2026-11-08-v3-small-file-microbatch-01.md)、[清单候选02](tasks/2026-11-08-v3-small-file-microbatch-02.md)、[pending-window03](tasks/2026-11-08-v3-protocol-microbatch-03.md) |
| 最近的小文件优化与待办 | [S0基线21](tasks/2026-11-09-v3-small-file-baseline-21-result.md)、[S1摘要缓存22](tasks/2026-11-09-v3-summary-path-22-result.md)、[S1B默认门23](tasks/2026-11-09-v3-summary-default-gate-23-result.md)、[S2归因24](tasks/2026-11-09-v3-small-file-fixed-work-24-result.md)、[S2B序列化25结果](tasks/2026-11-09-v3-tree-serialization-25-result.md)、[S2C运行时26结果](tasks/2026-11-09-v3-tree-serialization-runtime-26-result.md)（NO-GO）、[S2D清单预算27结果](tasks/2026-11-09-v3-tree-state-budget-27-result.md)（直接同步预算不足）、[S2E逐文件预算28结果](tasks/2026-11-09-v3-per-file-budget-28-result.md)、[S2F发送端窗口预算29结果](tasks/2026-11-09-v3-sender-credit-budget-29-result.md)、[S2G事务审计30结果](tasks/2026-11-10-v3-receiver-transaction-audit-30-result.md)（逐文件性能归因暂停） |
| 最新正确性修复与大文件诊断 | [Publication31结果](tasks/2026-11-10-v3-publication-reconcile-31-result.md)（有界发布恢复修复）、[TCP Buffer Ownership32结果](tasks/2026-11-10-v3-tcp-buffer-ownership-32-result.md)（五对诊断CPU下降19.84%，墙钟未过门，不集成）、[Receiver Service Budget33结果](tasks/2026-11-10-v3-receiver-service-budget-33-result.md)（既有证据/源码审计完成，锁内CRC关键路径预算仍未知，当前接收端候选分支暂停） |
| S1B资源与大文件源驻留 | [Recheck34](tasks/2026-11-10-v3-summary-default-recheck-34-result.md)记录原8 GiB驻留失败；[Diagnostic39](tasks/2026-11-10-v3-source-residency-ownership-39-result.md)在同进程/cgroup完成一次源准备，2,097,152页全部驻留、host pswpout和进程swap增量为0。没有性能样本，不自动重试或晋级S1B默认 |
| 历史三阶段 / RDMA | [三阶段工程收口](tasks/2026-11-07-v3-stage-decoupling-01-result.md)、[RDMA 基线](tasks/2026-11-04-r770-rdma-e2e-baseline-01-result.md)、[result 生命周期](tasks/2026-11-05-v3-result-lifetime-01-result.md)、[冷建连](tasks/2026-11-06-v3-rdma-startup-01-result.md) |
| 其余记录与宏观设计 | [全部任务](tasks/)、[历史实验](experiments/)、[原分阶段计划](tasks/2026-11-04-performance-roadmap.md)（执行优先级以当前路线图为准） |

## 决策与历史规则

- 2026-11-10起执行[性能里程碑决定](DECISIONS/2026-11-10-v3-performance-milestones.md)。历史候选结果保留原判定；scope39的Entry锁内CRC combine为NO-GO，源准备通过未替代8GiB完整任务门。诊断40未复现RX miss；M1-41因资源门中止、无性能数据，[任务42](tasks/2026-11-11-v3-resource-gate-failure-evidence-42-result.md)已完成失败证据补全，[任务43](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)未复现host事件；[M1-44结果](tasks/2026-11-11-v3-large-file-m1-44-result.md)已通过8GiB完整任务门槛；M1-41仍不重跑。

- 当前开发环境遵循 [R770 主开发决定](DECISIONS/2026-10-30-r770-primary.md)；[性能路线决定](DECISIONS/2026-11-04-performance-route.md)和[流水线优先决定](DECISIONS/2026-11-07-v3-pipeline-priority.md)按当前路线图理解。
- 深圳/上海主开发、旧协作角色、强制 worktree、旧路径和 SSH 命令均为历史材料，不覆盖当前开发规则。
- 路线图只更新当前结论、推荐配置、开放门和下一项；逐次测量、失败修正、测试清单、证据路径与历史下一步写回对应结果，避免继续追加实验时间线。
