# CPNetFlux 文档导航

最新开发：[阶段78结果](tasks/2026-11-13-v3-directory-semantics-78-result.md)已补齐V3普通/空目录独立元数据、schema2完成事实、安全路径创建，并修复断连恢复的旧占用/重新派发竞争。双端同提交/同配置/四SHA，各完整回归825通过、3项原生RDMA环境跳过、0失败；最终无trace早期控制断连64/64通过。工程正确性阶段闭合，下一步是新版本受控GridFTP复测与稳定性验收；A/B/C和上机仍OPEN/NOT_READY，没有新吞吐成绩。 [阶段79](tasks/2026-11-13-field-trial-scenarios-79-result.md)因实验输入缺失在payload前回滚，正式组均NOT_RUN；当前先闭合[80启动输入](tasks/2026-11-13-field-trial-inputs-80.md)，原始失败保留。

历史实测：[阶段75](tasks/2026-11-12-field-trial-comparison-75-result.md)1GiB五对中位CP31.075/Grid12.352Gbps；[阶段77](tasks/2026-11-13-field-trial-continuation-77-result.md)dense各3次0.399/0.945秒、tiny各3次3.504/2.561秒，mixed文件全对但空目录丢失。上述为旧运行时成绩，不能搬到新二进制。临时RX4096已恢复1024，阶段77现场已清理；新窗口需重新准入，不启动吞吐参数扫描。

历史开发补项：[阶段68：工作租约迁移恢复修复](tasks/2026-11-12-whole-file-retry-68-result.md)。双端各156/156相关回归通过，原512B故障场景6/6复核；[阶段67：默认关闭端口计划](tasks/2026-11-12-tcp-source-plan-67-result.md)完成一次1MiB实际四队列映射。该阶段未新增性能成绩；其后1GiB验收已由阶段69执行并失败，下一步以页首为准。当前验收结论以页首为准。

历史诊断：[阶段66：RX队列聚集隔离对照](tasks/2026-11-12-rss-queue-control-66-result.md)。同速四流分散队列时OOB/missed为0；集中单队列触发各+310并停止。没有新增CP性能成绩，下一步验证软件TCP连接布局及恢复/端口复用；正式比较仍NO-GO、稳定性未开始、上机NOT_READY。

前序观测交付：[阶段65：连接/接收队列观测补全](tasks/2026-11-12-rss-flow-evidence-65-result.md)。12条低速probe的RSS预测全部验证；V3实际端口及最后CPU/NAPI已实现，双端各5项回归、一次0字节跨机功能核验通过。没有新增吞吐成绩，OOB因果尚未关闭；比较、稳定性和上机状态仍以下列阶段64失败边界为准。

当前交付复核：[阶段64正式比较中止](tasks/2026-11-12-field-trial-comparison-64-result.md)：固定版本仅1/5完整有效配对，第2对CP在R1触发OOB/missed各+1275；比较NO-GO、稳定性未开始、上机NOT_READY。代码与既有工程闭环保持，不发布速度倍数或稳定40G。

开发闭环已验收：[阶段60结果](tasks/2026-11-12-field-trial-closeout-60-result.md)。双端相关278项通过；后续比较/稳定性状态以阶段64为准。

当前执行：[阶段60：保持性能、开发闭环→GridFTP复测→稳定性→上机候选](tasks/2026-11-12-field-trial-closeout-60.md)。M2及历史交付不改，未完成实际实验前不标稳定性通过。

历史Task58交付C：[Task58后最终集成与边界](tasks/2026-11-12-performance-delivery-c-task58.md)、[当前冻结清单](tasks/2026-11-12-performance-delivery-c-task58-freeze.json)：代码/配置/回归/现场与备份收口；ENGINEERING CLOSED / M2 OPEN，正式资源门仍未通过。

最新交付：[Task58 / 有界8GiB与正式M2资源停止](tasks/2026-11-12-v3-private-m2-58-result.md)：固定双profile/五对矩阵和8GiB能力已实现；唯一尝试在第1个control源write时因R2 host pswpout +1页停止，未启动网络传输，完整样本0。源leaf swap/回收为0、user-1002祖先swap +4096B，具体对象未识别。已补MemFree≥9.25GiB的分配前拒绝；最终双端相关8/8通过，原输入双端262/262及候选恢复各2/2通过。M2 OPEN，当前资源未准入，不重跑；[冻结](tasks/2026-11-12-v3-private-m2-58-freeze.json)。

前次交付：[Task57 / 原生必要准备与交付C冻结](tasks/2026-11-12-v3-private-setup-57-result.md)：五对准备门通过（旧中位53.40ms→native 7.62ms），最终两端相关CTest各261/261、kernel pacing smoke各连续10次通过；唯一1GiB完整目标任务**40.865Gbps**，必要准备计入，内容/资源/网络/页型/清理均通过。仅单次可行性准入，正式8GiB M2与稳定性仍开放；[冻结清单](tasks/2026-11-12-v3-private-setup-57-freeze.json)。默认关闭，不追加本轮样本或参数扫描。

历史交付：[Task56 / 内核TCP pacing与交付C冻结](tasks/2026-11-12-v3-kernel-tcp-pacing-56-result.md)：默认关闭机制已实现，R2相关259项通过、R1失败修复后定向5项通过，两端候选恢复各2项通过。唯一1GiB完整目标任务35.388Gbps，网络/资源/内容/页型/清理通过，但未达40Gbps准入。四lane ceiling回读不等于实际整形；一次0错误不关闭OOB因果。停止追加本候选和8GiB矩阵，M2仍开放；[冻结清单](tasks/2026-11-12-v3-kernel-tcp-pacing-56-freeze.json)。

历史交付：[Task55 / 私有tmpfs完整V3准入与交付C冻结](tasks/2026-11-12-v3-private-tmpfs-55-result.md)：默认关闭的有界后端与两端各18项相关回归通过。唯一1GiB样本计入必要目标准备后35.710Gbps，OOB/missed各+261，网络门失败；原始客户端窗口45.922Gbps不计为合格性能成绩。资源/内容/实际页型/清理通过，未运行新8GiB矩阵，M2仍开放；停止本候选的追加payload与扫描。冻结见[清单](tasks/2026-11-12-v3-private-tmpfs-55-freeze.json)。

前序机制：[首次大页54](tasks/2026-11-12-v3-private-tmpfs-54-result.md)仅局部五对通过；
[ID水位53](tasks/2026-11-12-v3-record-id-watermark-53-result.md)已交付。当前下一步以阶段64结果与路线图页首为准，不沿用旧文档“待做1GiB”的历史下一步。

交付C历史快照已核对Task51/52版本：[冻结清单](tasks/2026-11-12-performance-delivery-c-freeze.json)、[交付结果与边界](tasks/2026-11-12-performance-delivery-c.md)。工程交付完成，M2/网络稳定性开放；历史Task50冻结保留原版本，不作为新代码性能成绩。


近期交付：[TCP exact-read 52](tasks/2026-11-12-v3-tcp-exact-read-52-result.md)：双端254项相关回归通过，读取机制确认；唯一1GiB触发OOB/missed各+52停止门，未产生新合格性能成绩，M2仍开放。

**当前性能主线只维护在[路线图](ROADMAP.md)。** 本页负责导航，详细数据、失败与验收命令留在各任务/结果中；历史结论按其输入提交和环境解释。

## 当前入口

| 要查什么 | 文档 |
|---|---|
| 项目、服务器与开发入口 | [项目 README](../README.md)、[开发规则](../AGENTS.md)、[R770 环境](coordination/ENVIRONMENT.md) |
| 当前状态、配置、未过门与下一项 | [性能路线图](ROADMAP.md) |
| 历史Task58工程交付状态 | [Task58交付C](tasks/2026-11-12-performance-delivery-c-task58.md)：ENGINEERING CLOSED / M2 OPEN，Task58源准备资源停止；[Task50历史收尾合同](tasks/2026-11-11-performance-stage-closeout-handoff.md)保留原边界 |
| 交付B：成绩与M2判定 | [Task58正式尝试](tasks/2026-11-12-v3-private-m2-58-result.md)：源准备资源停止，0完整任务；[Task50历史NO-GO](tasks/2026-11-11-v3-sender-pacing-50-result.md)不作当前准入结论 |
| 历史Task50交付C：冻结与边界 | [配置/版本冻结清单](tasks/2026-11-11-v3-sender-pacing-50-freeze.json)、[C销项结果](tasks/2026-11-11-v3-sender-pacing-50-result.md)：同运行时回归证据、现场清理与RDMA预算评估完成；阶段ENGINEERING CLOSED / M2 OPEN |
| 暂存研究 idea（本轮不开发） | [统一工作单元与预算感知策略记忆](ideas/2026-11-11-unified-work-unit-and-budgeted-policy-memory.md)：DEFERRED，需用户明确重启 |
| 当前批准主线与串行交接 | [里程碑35计划](tasks/2026-11-10-v3-large-file-milestone-35.md)、[实现/1GiB结果](tasks/2026-11-10-v3-large-file-milestone-35-result.md)、[M1-41计划](tasks/2026-11-11-v3-large-file-m1-41.md)、[M1-41结果：资源门中止、无吞吐样本](tasks/2026-11-11-v3-large-file-m1-41-result.md)、[M1-44计划](tasks/2026-11-11-v3-large-file-m1-44.md)、[M1-44结果：8GiB正式门通过](tasks/2026-11-11-v3-large-file-m1-44-result.md)、[M1-45接收端profile诊断](tasks/2026-11-11-v3-receiver-profile-diagnostic-45.md)、[Task46合同与结果](tasks/2026-11-11-v3-profile-overhead-counter-correlation-46.md)、[Task46中止结果](tasks/2026-11-11-v3-profile-overhead-counter-correlation-46-result.md)、[资源失败证据补全42结果](tasks/2026-11-11-v3-resource-gate-failure-evidence-42-result.md)、[host pswpout诊断43结果](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)、[分层准入与路线决定](DECISIONS/2026-11-10-v3-performance-milestones.md) |
| 近期诊断结果 | [无payload host pswpout归因43](tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)：两端均未复现，M1-41单页事件仍未定位 |
| 最新执行结果 | [阶段64结果](tasks/2026-11-12-field-trial-comparison-64-result.md)、[冻结](tasks/2026-11-12-field-trial-comparison-64-freeze.json)：1/5有效配对后网络门停止，稳定性未开始、上机NOT_READY |
| 执行方式和缓冲所有权 | [三阶段指南](V3_TRANSFER_STAGES.md)、[映射下载边界](tasks/2026-11-08-v3-mapped-download-08-result.md) |
| 接收队列计数归因 | [Task47合同](tasks/2026-11-11-v3-rx-page-pool-counter-analysis-47.md)、[Task47结果](tasks/2026-11-11-v3-rx-page-pool-counter-analysis-47-result.md)、[Task48计划](tasks/2026-11-11-v3-rx-oob-driver-source-audit-48.md)、[Task48结果](tasks/2026-11-11-v3-rx-oob-driver-source-audit-48-result.md) |
| 当前开发 | [阶段64](tasks/2026-11-12-field-trial-comparison-64-result.md)：正式比较因OOB停止；先解决可验证的接收服务/流分布问题，再重开比较与稳定性 |
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
