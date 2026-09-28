# CPNetFlux 100 Mbps 性能路线（已被底层优化路线 supersede）

> 状态：`R2026-09-23.1` 已由 `R2026-09-23.2` 取代，不再作为当前代码开发路线。保留本文和相关审查作为历史证据；旧 100 Mbps 矩阵、runner/telemetry 门禁将来如需使用，必须另行修订任务单。当前路线见 [`docs/DECISIONS/2026-09-23-lower-level-dataplane-route.md`](DECISIONS/2026-09-23-lower-level-dataplane-route.md)。

- 状态：执行计划 v1；目标已获用户确认，04 已完成两轮独立审查；B5–B8 和 A 阶段契约仍未冻结，当前不得直接开跑。
- 路线版本：`R2026-09-23.1`。
- 输入基线：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；最近实现仍为 `3b0820d`，其后是文档变化。每个后续代码任务必须重新记录完整输入 commit 和工作树状态。
- 目标：在现有约 100 Mbps 跨域服务器上同时提升单文件和多文件目录传输，尽量接近匹配配置的真实 GridFTP；未来 100G 必须另在高速环境测试。

## 目标与验收口径

比较对象必须是同源数据、同方向、可匹配的文件/流并行度、可说明的认证和数据通道安全级别。真实 GridFTP 通过 `globus-url-copy`；名称含 GridFTP 的 CPNetFlux framed 私有 runner 不算对照。无法匹配的格点标记 `unmatched_config` 并排除，不用参数名相同代替实际行为相同。

主要指标为明确 transfer timer 内的 logical goodput（十进制 Mbps）、每个配置的配对中位数和波动、传输成功率。准备数据、独立 SHA-256/tree hash、日志回收和清理不能混进传输分母。保留 runner wall、native transfer wall、外部完整性校验三种边界，只有定义并对齐的计时才能用于系统间比值。

近期工作验收目标建议为：单文件和目录的每个有效配置格，CPNetFlux 中位 goodput 达到匹配真实 GridFTP 中位数的至少 90%，筛选阶段 `range/median` 不高于 20%，且没有传输或完整性失败。该数值是路线提案，待 01 架构与 04 质量在路线决策中确认；无效或不匹配的格点不计入达标分母，也不得隐藏。

校验分两种工作负载。`checksum=none` 是吞吐诊断档；应用内部不做 chunk checksum，独立 SHA-256 在计时外用于测试数据验收。`crc32c + verified_chunks` 是保留分块校验的档，只有 chunk 全覆盖、manifest flush/commit 条件满足并记录 effective policy 时才能省去末尾全文件重读。禁止把请求 `full`、实际未执行的 no-op 或 fallback 混称为相同校验策略。两档数据分别报告，不拼成一个 headline。

任何 100 Mbps 结果只适用于记录过的两台主机、链路、认证、数据安全、构建和存储条件；不证明 100G NIC/CPU/NUMA/存储能力，不称 production readiness。

## 阶段与负责人

| 阶段 | 负责人 | 交付与退出条件 | 状态 |
| --- | --- | --- | --- |
| A. 联合路线和测量契约 | 01；04 独立审查 | 决定 timer 边界、单文件/目录的指标、none/CRC effective verify、auth/data TLS、GSI 双向并流、目标比值和失败分类；写版本化 decision | blocked：01 既有聊天曾返回 active-writer，尚未交付 |
| B. Runner 改造规格 | 02；04 审查 | 精确 case manifest、配对随机顺序、统一 hash 类型、transfer timer 与外部证据/cleanup 门禁；只给文件级改造规格，标出需 A 冻结项 | pending A；现有矩阵设计算术已复核但 QA 不通过可执行性 |
| C1. 单文件观测 | 03；04 review | 按 A 契约补 upload/download 的 native transfer wall、配置/effective verify 和必要阶段事件；不改协议、默认值、校验/resume 或 IO 策略 | pending A/B 规格 |
| C2. 目录观测 | 03；04 review | 在 C1 与 A 之后补 tree upload/download 的逐文件 control/data/complete/finalize 观测及 run wall；并发阶段不可冒充 wall，普通和并行 scheduler 路径都覆盖 | pending C1、A |
| D. 固定构建和回归 | 05 运维构建，04 独立验收 | 固定提交归档、源码和二进制 SHA-256、明确 CMake 参数、token 受控注入、CTest 与真实 transfer/resume/hash smoke；skip/blocked 分开 | blocked：上海约 32 GiB 但活动 PID 归属、预算和备份证据未闭合 |
| E. 链路 pilot 与筛选 | 02 设计/分析，05 运维，04 QA gate | 先跑小型双向 pilot 验证实际配置和证据，再冻结并运行短矩阵；同环境一次一个批次，先保存/回收证据再清理自有 payload | blocked：D 未通过且上海空间未恢复 |
| F. 证据驱动优化 | 03 实现，04 review，02/05 验证 | 每次只针对测量支持的一个瓶颈提出窄改动；独立 worktree、明确文件白名单、配对回归和 100 Mbps 矩阵复测；未达目标则保留结论并调整方案 | pending E |
| G. 100G 复验 | 00 决策，01 方案，02/04/05 执行 | 取得高速环境后重新建立 link/build/resource baseline 和测试矩阵；100 Mbps 的优化排序不可直接外推 | 单独未来阶段 |

## 当前比较和候选扫描

02 的候选矩阵包含 single 256 MiB（每文件连接 1/8）、dense 128 MiB（file parallelism 1/4/8、每文件连接 1）、mixed 256 MiB（(1,1)/(2,2)/(4,2)），双方向、真实 GridFTP 对照、同 seed。初稿最多 186 个 transfer cases、38.25 GiB logical payload，100 Mbps 理想线速下限约 54.76 分钟。这只是矩阵设计和下限估算，不是预计 wall 或已批准运行计划；04 已发现现有 runner 无法精确生成该矩阵，timer、校验档、GSI 反向多流、hash 与清理证据仍需改造/冻结。

单文件优先扫描连接数 1/8；之后才在固定连接数下单独扫描 buffer 或 chunk，避免同时改两个参数。目录先比较 dense 的 file parallelism，再看 mixed 的组合并发；控制复用保持任务明确指定的 worker，不改默认值。以上都是待测假设，不能凭历史 33–49% 目录差距或旧 dirty run 断言根因。

## 运行门禁与安全边界

05 的 2026-09-23 只读复核显示深圳约 48.88 GiB 可用且有外部写入迹象；上海 `/` 与 `/tmp` 同盘约 32 GiB 可用，但发现新近 PID 768474 `gridflux-gridft` 监听 25252，归属未明。只有每个相关挂载点满足至少 `10 GiB 保留 + 峰值 payload + build/archive/evidence 预算`，且进程/端口/批次归属复核通过，才可启动构建或实验。ENV-01 当前批准清理量仍为 0 GiB；未经逐路径备份核验和授权不得删上海历史数据。

云端 `/root/projects/GridFlux-Beta` 两处是 dirty 历史树，禁止覆盖或清理；`/root/projects/CPSS(DCC)` 绝不进入或修改。SSH 使用已登记 alias 和严格 host-key 检查，不把凭据放日志。云端只能运行本地已提交的明确 commit 归档，不在云端历史目录开发。

代码与测试改动分别由正式任务指定 `codex/<task-id>` 独立 worktree。文档任务各写自己的回执；00 维护本计划、BOARD/ROSTER 和阶段验收。任何任务都不得把 blocked、skipped 或缺少证据写成通过。普通可逆的工具和测试工作在上述边界内继续；清理未知数据、改校验/协议语义和改变研究方向必须先有具体审查决定。

## 计划审查后的硬门

04 的 `PERF-PLAN-QA-01 v1` 已确认方向可保留，但本文件不是直接执行授权。A 阶段必须先冻结：90% 比值与 `range/median` 的有效样本分母、阶段和 checksum arm；单文件与目录的 `worker/scheduler/compression/POSIX/fresh` baseline；timer 起止事件、auth/data-channel 等价性、GSI 反向并流处理；`none`/CRC32C 的 effective verify 与 fallback 语义。B 阶段必须把这些字段落入显式 manifest/result validator，并由 04 验收。

pilot 和固定构建在启动前必须分别有硬退出条件：固定 commit、源码归档和两端 binary SHA-256，CTest/transfer/hash smoke 的 pass、blocked、skipped 分列，实际 GridFTP 流数与安全级别证据齐全；每个受影响挂载点满足 `10 GiB + 峰值 payload + build/archive/evidence 预算`。证据 index 持久化、回读和 SHA-256 复核完成后才能清理；上海虽约 32 GiB 可用，但活动 PID 归属、峰值预算和备份 SHA 仍未闭合且没有清理授权，所以 D/E 阶段保持 blocked。

只有 A/B 完成、01 writer 释放并交付版本化 decision、05 重新确认空间与批次归属、04 通过 runner/schema 规格后，才可向 03 下发单文件/目录观测代码任务。任何 100 Mbps 结果仍只适用于记录的当前主机和链路，不能提前外推 100G。

## 下一步派单

1. 01 续接 PERF-ROUTE-01，完成 A 阶段 decision；当前不能确认该既有聊天 writer 已释放，单独 CLI 不得抢占。
2. 02/03 已交付窄范围回执：PLAN-03 v3 完成工具侧 B5–B8 收窄，CONTRACT-02 v1 完成源码字段映射；两者均未改代码或运行环境。
3. 04 已完成 PERF-TOOL-QA-03：B5–B8 均 partial，当前不授权 02/03 正式实现；05 已完成上海空间恢复只读证据清单，环境仍 blocked。
4. 04 已完成 PERF-QA-VECTOR-01：5 个纯函数向量通过；这不替代项目 validator、故障注入、项目测试或 01 timer 契约。
5. 03 已完成 PERF-IMPL-ADAPTER-01，05 已完成 PERF-ENV-EVIDENCE-02；04 的联合契约复审已完成，B5/B6/B7 均 blocked，仍不授权代码实现。
6. 05 保持云端无构建/实验状态；上海当前约 33.43 GiB 仍不是批准配额，可释放量为 0。
7. 下一实际门是 01 交付 timer/auth/GSI/verify 决策；同时需为 03/05 的三项 blocked 条件形成具体白名单任务，之后才由 04 复审并决定是否进入代码实现。
8. 代码与工具改动完成并经 04 审查后，由 05 做固定提交隔离构建和 CTest；资源满足后才由 02/05 申请 pilot 和 screening。目标比例、波动及 case 口径须在首个运行任务单冻结。

## 当前完成情况

- PERF-MATRIX-01：02 的设计稿已交付；04 独立核算通过，但判定当前可执行性不通过，实验未批准。
- PERF-QA-01：04 已完成审查，见 `docs/coordination/receipts/04-quality.md`。
- ENV-01：05 已完成两端只读复核；上海最新只读快照约 32 GiB 可用，但活动 PID 归属未明、备份 SHA 未核实，当前批准释放量仍为 0，见 `docs/coordination/receipts/05-operations.md`。
- PERF-IMPL-PLAN-01：03 已交付实现拆解，文档门禁和 `git diff --check` 通过，见 `docs/coordination/receipts/03-implementation.md`。
- PERF-TOOL-PLAN-01：02 已交付 runner/证据工具文件级规格；只追加回执，未运行实验或测试，见 `docs/coordination/receipts/02-experiments.md`。
- PERF-PLAN-QA-01：04 已完成总计划独立审查；方向可保留但计划不可直接执行，见 `docs/coordination/receipts/04-quality.md`。
- PERF-TOOL-QA-01：04 已完成 runner 规格独立审查；规格骨架可保留但不允许整体实现，见 `docs/coordination/receipts/04-quality.md`。
- PERF-TOOL-PLAN-02：02 已交付 v2，逐项回应 QA-01 的 B1–B8；QA-02 已判 B1–B4 pass、B5–B8 partial，需有限修订后再审。
- PERF-TOOL-QA-02：04 已完成独立复审；compression effective、GridFTP auth/流语义、ledger durability、timer 事件和故障注入仍未闭合，未批准实现或实验。
- PERF-TOOL-PLAN-03：02 已实际交付 v3；04 已复审，B5–B8 仍 partial/blocked，未授权实现或实验。
- PERF-IMPL-CONTRACT-02：03 已实际交付 v1；现由 PERF-IMPL-ADAPTER-01 补充映射，正式实现仍未授权。
- PERF-TOOL-QA-03：04 已完成复审；B5–B8 未闭合，不授权实现或实验。
- PERF-ENV-SPACE-PLAN-01：05 已完成只读空间证据任务；不删除、不改服务、不启动构建或实验，D/E 仍 blocked。
- PERF-QA-VECTOR-01：04 已完成纯函数向量复核；5 个摘要与篡改敏感性断言通过，项目级 B8 仍 partial。
- PERF-IMPL-ADAPTER-01：03 已交付规范化 telemetry 映射；participant/worker/失败覆盖缺失，B5 仍 unknown/ineligible。
- PERF-ENV-EVIDENCE-02：05 已交付服务、双机空间与备份索引复核；历史 PID 归属和上海 payload hash 未知，可释放量 0。
- PERF-QA-CONTRACT-04：04 已完成联合复审；B5/B6/B7 blocked，尚不能创建白名单正式实现任务。
- PERF-ROUTE-01：任务单已写，但因 active-writer 冲突尚未送达/完成；A 阶段仍未结束。
