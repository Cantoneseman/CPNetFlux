# 总指挥任务板

更新：2026-09-24。路线版本：`R2026-09-24.16`（云端权威工作区与 GitHub 备份迁移门禁；其后继续旧数据取证→底层/架构优化→深圳—上海 100 Mbps 跨域复验）。

当前目的：先从历史实验数据寻找有证据支撑的性能问题，再针对 C++ 传输热路径和目录数据面做底层/架构级优化，最后通过深圳—上海 100 Mbps 跨域固定提交实验与真实 GridFTP 对照。历史汇总差距只是线索，不能预设 syscall 或 manifest 是主要瓶颈。广州—深圳 100G 暂不作为当前验收环境。

协作原则变更：项目操作云端优先，迁移验收后深圳为唯一权威 CPNetFlux Git 工作区、上海只作传输对端；每个可审查阶段推送到经核实的 GitHub remote。当前迁移/GitHub 备份链路尚未验收。细则见 `docs/DECISIONS/2026-09-24-cloud-first-development-and-github-backup.md`。

当前执行门：先完成 `CLOUD-GITHUB-01`。迁移和 GitHub 备份链路验收前，暂停新的源码实现、构建与性能实验；保留已产生的 worktree、证据和服务状态，不因本路线调整而覆盖或清理。性能方向本身未变，后续工作在云端迁移完成后恢复。

| ID | 工作 | 责任角色 | 状态 | 前置条件/验收 |
|---|---|---|---|---|
| CLOUD-GITHUB-01 | 建立深圳权威工作区与 GitHub 分阶段备份链路 | 00 总指挥 + 05 运维 | ready | 核对本地全部 tracked/untracked/dirty 文件并分类、逐文件/hash 留存；只读核实深圳候选路径归属/空间/进程及 GitHub 仓库身份/凭据机制；在经批准的独立 CPNetFlux 路径迁移并逐文件对账；用任务分支完成非敏感文档/清单 commit、push 和远端 SHA 回读。不得覆盖历史 GridFlux 树或触碰 CPSS；未验收前不启动源码开发/构建/实验，不宣称已备份 |
| SETUP-01 | 新 Codex 项目与六个聊天 | 当前设置任务 | done | 已续接既有 04 聊天执行 PERF-QA-01；同一会话交付回执、CLI 终态为 0，产物读回验收。独立 dispatch-check 文件未执行 |
| ENV-01 | 两端资源重验与上海只读空间盘点方案 | 05 运维 | done，构建/实验仍 blocked | 17:23 新鲜快照：深圳 68.72 GiB、上海 33.42 GiB 可用；本轮批准释放 0 GiB；活动归属、峰值预算和 data TLS 仍未知 |
| SPEC-01 | 旧目录单路径 profiling 契约 | 01 架构 + 04 质量审查 | superseded | 被 PERF-ROUTE-01 取代；保留旧证据，不再按旧范围派发 |
| VERIFY-01 | 固定构建与阶段 0 验收复核 | 04 质量 + 05 运维 | blocked | 实验资源恢复、记录明确 commit/hash、解决测试 token 注入；不以 skip 当 pass |
| IMPL-01 | 旧目录单路径 instrumentation | 03 实现 | superseded | 被单文件+目录联合性能路线取代；新实现须等路线、QA、输入与矩阵口径冻结 |
| EXP-01 | 旧目录/调度矩阵 | 02 实验 + 05 运维 | superseded | 被 PERF-MATRIX-01 的 100 Mbps 短矩阵计划取代；实际执行仍须资源与构建门禁 |
| PERF-ROUTE-01 | 100 Mbps runner/校验路线 | 01 架构 | superseded | 由 `R2026-09-23.3` 接替；历史计时/校验约束保留为实验门，不阻塞证据驱动的局部实现 |
| HIST-FORENSIC-01 | 旧实验数据分层取证 | 02 实验 | done（完整正文恢复；部分数字按恢复限制留空） | 24 行 runner-wall 与 13 行目录 client-process 表已读回；dense GridFTP case-wall 个别精确值缺失并留空，不重算 payload |
| HIST-QA-01 | HIST-FORENSIC-01 独立复核 | 04 质量 | done（计数/代表格 PASS，整体比较 PARTIAL） | 复算 dense 上传 fp1/fp8 并发响应；安全配置和旧 evidence 不等价 |
| HIST-FORENSIC-RESTORE-01 | 恢复完整历史取证报告 | 02 实验 | done | 完整报告 24/13 表行门禁通过；恢复不等于重新扫描原始数据 |
| HIST-QA-RESTORE-01 | 恢复独立质量审查全文 | 04 质量 | done | 完整审查表/计数已恢复；只恢复既有复核，不重扫原始数据 |
| DIR-PERF-SOURCE-01 | 目录串行路径源码级选题 | 03 实现 | done（静态映射） | worker 每文件建立数据连接并等待完成；manifest 全量写在锁内，但二者因果成本均未 profile |
| LOWLEVEL-DESIGN-01 | 单文件与目录底层优化选型 | 03 实现 | review（完整报告已交，待 04） | 对比跨文件 session、单 worker lookahead、明文 vectored write、manifest；明确首选与协议恢复语义 |
| LOWLEVEL-DESIGN-QA-01 | 优化架构选型独立审查 | 04 质量 | done（PARTIAL，不放行代码/实验） | sendmsg 等本机 Linux profile；目录 lookahead 等阶段计时与关键路径证据 |
| LOWLEVEL-SINGLE-PROFILE-01 | 单文件 plain TCP syscall 基线 | 00 总指挥复核（02 原 CLI blocked，00 独立补跑） | done | 固定 `3b0820d`、Release、三项 file smoke 3/3；1/8 connections 各 3 次逐 peer trace，4,096 DATA frames、0 short/unresolved、目标 hash 全等；批准 SENDV-IMPL |
| ENV-PREFLIGHT-02 | 深圳/上海跨域实验只读准入复核 | 05 运维 | done，实验准入 blocked | 结果已回收；峰值预算、PID/批次归属、认证/data TLS 仍未闭合；不构建、不启动传输 |
| ROUTE-QA-02 | 100 Mbps 证据驱动路线质量审查 | 04 质量 | done，部分通过；实现/实验未获准 | 路线方向可继续；历史因果、0.90 样本规则和跨域匹配门仍待冻结；未改代码、不 SSH |
| PERF-MATRIX-01 | 100 Mbps 匹配对照短矩阵设计 | 02 实验 | deferred | 旧设计不可直接执行；取证与 QA 后重冻，不直接开跑旧 186-case 计划 |
| G100-SENDV-01 | 无旧证据支持的预选 sendv 微优化 | 03 实现 | superseded | 用户新总目标要求先读数据定位；sendmsg 候选保留但不预选实施 |
| G100-TREE-IO-02 | 目录 manifest 写放大优化 | 03 实现 | deferred | 待历史证据、源码映射和测量支持，并审查崩溃恢复语义后派发 |
| PERF-QA-01 | 100 Mbps 矩阵设计独立质量审查 | 04 质量 | done，结论不通过 | 04 已独立核算并审查实际 runner；报告明确未批准实验，见 `receipts/04-quality.md` PERF-QA-01 v1 |
| PERF-PLAN-QA-01 | 100 Mbps 总计划独立质量审查 | 04 质量 | done，计划不可直接执行 | 04 已追加独立审查；方向、校验隔离、A-G 主链和 100G 边界可保留，但样本分母、硬退出门、effective verify、资源/归档与 baseline 仍需冻结；未批准实现/实验 |
| PERF-IMPL-PLAN-01 | 单文件与目录优化实现拆解 | 03 实现 | done | 回执已交；只读候选、代码/测试边界和依赖完整；文档门禁与 `git diff --check` 通过，无源码/test 差异 |
| PERF-TOOL-PLAN-01 | Runner 改造文件级规格 | 02 实验 | done | 02 已追加规格回执；显式 case、计时/hash/evidence/cleanup 改造点完整，01 契约未决项标为依赖；未改代码、不实验 |
| PERF-TOOL-QA-01 | Runner 改造规格独立质量审查 | 04 质量 | done，不允许整体实现 | 04 已追加独立审查；manifest/三 arm/hash/timer/evidence 方向通过，但 confirmation 数量、批次停止、wire eligibility、attempt/retry、compression off effective 和 run-level ledger 仍阻塞；未批准 runner 实现/实验 |
| PERF-TOOL-PLAN-02 | 按质量审查修订 runner 规格 | 02 实验 | done | 02 已在既有聊天追加 v2，按 B1–B8 修订并通过文档门禁；未改代码、不运行；待 PERF-TOOL-QA-02 独立复审 |
| PERF-TOOL-QA-02 | Runner v2 规格独立复审 | 04 质量 | done，有限修订后再审 | B1–B4 pass；B5–B8 partial；compression effective、GridFTP auth/流、ledger durability、timer 语义和故障注入仍阻塞；未授权代码、测试、构建或实验 |
| PERF-TOOL-PLAN-03 | 按 QA-02 收窄工具规格修订 | 02 实验 | done，待 QA-03 | v3 已交付；B5–B8 工具侧规则收窄完成，保留 01/03/05 依赖；待 04 复审 |
| PERF-IMPL-CONTRACT-02 | 源码级 effective telemetry 字段映射 | 03 实现 | done，待 QA-03 参考复审 | v1 已交付；只读字段映射完成，未实现 telemetry；待 04 复审 |
| PERF-TOOL-QA-03 | B5–B8 与字段映射有限复审 | 04 质量 | done，继续阻塞 | B5–B8 均 partial；不授权正式实现/构建/实验，需 01/05/04 后续证据 |
| PERF-ENV-SPACE-PLAN-01 | 上海空间恢复只读证据清单 | 05 运维 | done，环境仍 blocked | 上海约 32 GiB 可用但 PID 768474 `gridflux-gridft` 归属未明；备份 SHA 未核实；确认可释放量 0，不得构建/实验 |
| PERF-QA-VECTOR-01 | B8 规格向量纯函数复核 | 04 质量 | done，B8 仍 partial | 5 个 SHA-256/脱敏/篡改敏感性向量通过；项目 validator、故障注入、timer/ledger 恢复和 durability 仍未运行 |
| PERF-QA-CONTRACT-04 | 适配映射与环境证据联合复审 | 04 质量 | done，B5/B6/B7 blocked | 04 已确认无真实 adapter 输出、case 级服务端流证据和 payload durability/cleanup gate；不授权代码或实验 |
| PERF-IMPL-ADAPTER-01 | 规范化 telemetry adapter 与覆盖清单 | 03 实现 | done，待正式实现 | 映射清单已交；缺真实 adapter 输出、participant/worker roster、失败覆盖，B5 仍 unknown/ineligible |
| PERF-ENV-EVIDENCE-02 | 服务归属、持久化与资源证据复核 | 05 运维 | done，环境仍 blocked | 上海约 33.43 GiB；历史 PID 已退出、payload hash/durability 未闭合，可释放量 0 |
| LOWLEVEL-IMPL-01 | 单文件与目录第一轮底层实现 | 03 实现 + 04 审查 | superseded | 拆为 `LOWLEVEL-SENDV-IMPL-01`（单文件，已满足 profile gate）与目录阶段观测；云端实验单独 blocked |
| LOWLEVEL-SENDV-IMPL-01 | plain DATA header+payload vectored write | 03 实现 + 04 审查 | review | 03 独立 worktree 已实现；00 的 WSL build 111/111、窄单测 6/6、文件 smoke 3/3、目录 smoke 8/8 通过；loopback A/B 每 DATA frame 写调用 2→1、CPU/GiB 降低，8-stream wall 未改善；待 04 独立代码/证据审查，不宣称跨域收益 |
| LOWLEVEL-SENDV-QA-01 | plain DATA vectored write 独立质量复核 | 04 质量 | review | 04 已交付 PARTIAL：静态字节/错误路径无明显问题，但无法独立读取 ext4 原始 trace，8-stream wall 中位数略退；暂不合入、不宣称跨域收益 |
| LOWLEVEL-SENDV-QA-RECHECK-02 | sendv 原型与 loopback 原始证据独立复核 | 04 质量 | done，PARTIAL | 04 独立核对 172 项证据与代码；DATA peer 发送调用约减半、CPU/GiB 中位数下降，但 8 流 wall 中位数增加约 6.5%，不合入、不宣称跨域收益；见 `2026-09-24-lowlevel-sendv-qa-recheck-02-result.md` |
| LOWLEVEL-TREE-STAGE-PROFILE-01 | 目录阶段关键路径观测 | 00/02 实验 + 04 审查 | review | 02 角色因 CLI WSL E_ACCESSDENIED 保持 BLOCKED；00 已补跑固定基线 upload/download，6+6 cases 正确完成，但 file elapsed/阶段边界缺失；依据见 `lowlevel-tree-stage-profile-00-supplement.md`，下一步需 telemetry instrumentation，不派 lookahead |
| LOWLEVEL-CLOUD-PREFLIGHT-01 | 深圳—上海验证环境只读准入复核 | 05 运维 | done，实验仍 blocked | 05 已完成只读 SSH：深圳 68.72 GiB、上海 33.42 GiB；GridFTP 服务必须保留；认证/data TLS、服务归属和 build/archive/evidence 预算未闭合，未构建或传输 |
| LOWLEVEL-TREE-TELEMETRY-PLAN-01 | 目录阶段 telemetry 契约与插点设计 | 03 实现 + 04 质量 | superseded | 03 v1 只读契约已审查；因旧 profiling 不等式、manifest 边界、崩溃生命周期、stream/nullability 和旧消费者兼容缺口失效 |
| LOWLEVEL-TREE-TELEMETRY-QA-01 | telemetry 契约独立质量复审 | 04 质量 | done，PARTIAL | 04 已指出上述契约缺口；未授权实现，结论转入 PLAN-02/QA-02 |
| LOWLEVEL-TREE-TELEMETRY-PLAN-02 | 按 QA-01 修订目录 telemetry 契约 | 03 实现 | done，QA PARTIAL | 文档已交付；不能进入实现，见 QA-02 结果 |
| LOWLEVEL-TREE-TELEMETRY-QA-02 | telemetry v2 独立质量复审 | 04 质量 | done，PARTIAL | finalize/226 边界矛盾、下载 mtime 顺序、stream_identity 字段和架构映射未闭合；不授权实现 |
| LOWLEVEL-TREE-TELEMETRY-ARCH-REVIEW-01 | telemetry 最终架构映射裁定 | 01 架构 | done，需 00 收口 | 结果已交；同一 attempt finalize/wait 相接，建议新增 download_mtime_finalize；schema/ID/logger 已列明；无实现/测试 |
| LOWLEVEL-TREE-TELEMETRY-PLAN-03 | 按架构裁定修订 telemetry 映射 | 03 实现 | superseded | 执行结果 BLOCKED；固定源码揭示两类 skip 会漏记已发生控制/数据活动；保留证据，不作为实现输入 |
| LOWLEVEL-TREE-TELEMETRY-ARCH-REVIEW-02 | resume 生命周期裁定复核 | 01 架构 | done，00 已裁定 | 固定源码复核交付；00 已冻结 decision `R2026-09-24.8` 两项 attempt-0 向量及独立结果状态轴 |
| LOWLEVEL-TREE-TELEMETRY-ARBITRATION-03 | no-range/retry/idle 生命周期裁定 | 01 架构 + 00 | done，条件收口 | ARBITRATION-03 已交付；固定源码复核通过；仅解除规格歧义，不授权实现 |
| LOWLEVEL-TREE-TELEMETRY-PLAN-04 | 依据架构裁定修订 telemetry 实现计划 | 03/00 | superseded，待 v2 | QA-04 发现 no-range、retry control、idle 尾部冲突；保留 v1 和阻塞证据，待 03 按 ARBITRATION-03 重写 |
| LOWLEVEL-TREE-TELEMETRY-PLAN-05 | 按 ARBITRATION-03 重写 telemetry 实现计划 | 03 实现 | ready | 必须收窄 paired N/A、same control retry、idle 三类 fixture；待 04 独立复审 |
| LOWLEVEL-TREE-TELEMETRY-QA-04 | PLAN-04 独立质量复审 | 04 质量 | done，BLOCKED | 结果已交；当前实现闸门保持 BLOCKED，需稳定 v2 后重审 |
| LOWLEVEL-TREE-TELEMETRY-PLAN-06 | 补齐 telemetry 规格验收向量与性能门 | 03 实现 | done，待 QA-06 | 结果已交；6 组/44 行 fixture lint、schema 矩阵、logger 注入契约和 10 对 on/off 门已写入；动态项未运行 |
| LOWLEVEL-TREE-TELEMETRY-QA-05 | PLAN-05 独立质量复审 | 04 质量 | done，BLOCKED | 结果已交；缺完整 schema/golden/故障注入/性能门，待 PLAN-06 后重审 |
| LOWLEVEL-TREE-TELEMETRY-PLAN-07 | 修正 span/stream fixture 两个最小规格错误 | 03 实现 | done，待 QA-07 | 结果交付且静态 fixture lint 通过；仅文档规格修订，无运行态验收 |
| LOWLEVEL-TREE-TELEMETRY-QA-06 | PLAN-06 最终独立质量复审 | 04 质量 | done，BLOCKED | 该轮发现的 span_id 下界和 R-SAME-CONTROL fixture 错误已由 PLAN-07 修订；由 QA-07 复审，不回写旧 QA 结论 |
| LOWLEVEL-TREE-TELEMETRY-QA-07 | PLAN-07 两项更正独立复审 | 04 质量 | done，PASS for implementation design gate | span_id=0 拒绝边界、R-SAME 上传 stream_id=0、6 组 fixture 共 44 行静态复核通过；动态 parser/logger/传输/on-off/开销门仍未运行 |
| LOWLEVEL-TREE-TELEMETRY-IMPL-01 | 目录阶段 telemetry 窄实现 | 03 实现；04 独立审查 | review | 固定输入 `3b0820d`、隔离 worktree 的实现已交；00 在 WSL2/Linux Release 构建 112/112、定向 CTest 27/27、全单元 CTest 183/183（io_uring 可选项 skipped），fresh on/off 上传/下载 hash 一致。no-range/resume 与 10 对开销门仍未完成；Linux 证据包在 `D:\Project\CPNetFlux-evidence\LOWLEVEL-TREE-TELEMETRY-IMPL-01-local-20260924\linux-validation`，等待独立质量复核 |
| LOWLEVEL-TREE-TELEMETRY-QA-08 | telemetry 实现与 Linux 证据独立审查 | 04 质量 | done，PARTIAL | 04 已独立复核候选 worktree、bundle 与日志；允许受限 fresh POSIX profile；no-range/resume、10-pair overhead、wire/frame/manifest 等价和 CRLF 修正可追溯性仍是硬门，不批准合入或性能结论；见 `2026-09-24-lowlevel-tree-telemetry-qa-08-result.md` |
| LOWLEVEL-TREE-DENSE-PROFILE-LOCAL-01 | 128×1 MiB 目录阶段本地画像 | 00 总指挥 | done，12/12 PASS | WSL2 隔离服务，upload/download × fp1/fp8 × 3；所有退出码与 tree hash 通过。结果显示 fp8 未改善本机 wall，高并发下 control/data/first-payload/interfile 阶段变长；见 `2026-09-24-lowlevel-tree-dense-profile-local-01-result.md` 与外部证据目录；仅形成 lookahead/preconnect 候选，不外推 WAN |
| LOWLEVEL-TREE-LOOKAHEAD-DESIGN-01 | 有界目录 lookahead/preconnect 契约 | 01 架构 + 03 实现 + 04 质量 | done（revision-02 设计已交付并经 QA） | 设计结果：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`；实现验收另行进行 |
| LOWLEVEL-TREE-LOOKAHEAD-QA-REVISION-08 | 旧源码上的 handoff 动态独立验收 | 04 质量 | superseded | 原结果 BLOCKED，因源码 hash 与 revision-08 不匹配；由 QA-REVISION-09 对 revision-10 可追溯证据重新复核 |
| LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-09 | revision-08 深圳固定构建 | 05 运维 | superseded | 源码归档漏掉 CMakeLists 差异，构建结果无效；旧 run root 和进程保留，不覆盖/清理 |
| LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10 | 完整导出并验收深圳固定构建 | 05 运维 | done（内容级证据已由 QA 核对） | 规范化源码+io_uring ON 的 CTest 224/224；精确源码 OFF 有 3 个 CRLF shell 失败；这些构建记录不证明 lookahead 已启用或性能改善 |
| LOWLEVEL-TREE-LOOKAHEAD-QA-REVISION-09 | revision-10 固定构建证据和有效启用门独立复核 | 04 质量 | done，PARTIAL；性能实验 BLOCKED | 证据与源码内容可核对；精确/off 与规范化/io_uring 配置分别报告；`reliableCandidateMemory=false` 使 depth=1 仍回退到 0。见 QA-REVISION-09 结果 |
| LOWLEVEL-TREE-LOOKAHEAD-RESOURCE-CONTRACT-01 | 收口候选线程、TLS 与内存上界 | 01 架构 | in_progress | 按 QA-REVISION-09 实证判定现有 1/2 MiB 预算能否可靠覆盖候选运行时；若不可证明，保持 fail-closed。未通过 04 独立审查前不得开启性能实验 |
| MCE-TOPOLOGY-02-R2 | 修正手动跨域计划端点角色 | 02 实验；00 验收 | done（仅计划器） | Windows 手动入口、深圳 runner/client、上海 peer；方向只交换 source/destination；`-Execute` 仍关闭，未 SSH/传输 |

状态含义：`ready` 可被派发但未启动；`in_progress` 需真实执行证据；`review` 待验收；`done` 有验收链接；`blocked` 有依赖；`superseded` 规格已失效。不得仅因 prompt 写好就把角色或任务标记完成。

## 当前阻塞与限制

- 设置记录：六个聊天和资料已建立；本轮已通过公开 CLI 续接既有 02/04/05 聊天并回收各自回执，04 真实完成 PERF-QA-01。单独的 dispatch-check.md smoke 尚未运行，但角色派单往返已有真实证据。
- 架构派单阻塞：01 的 PERF-ROUTE-01 曾报 `already has an active writer`。本轮未抢占或启动第二个控制器；01 尚无本路线新交付。
- TELEMETRY QA-02 为 PARTIAL；PLAN-03 的 BLOCKED 证据保留并已 supersede。01 ARCH-REVIEW-02、ARBITRATION-03 已交付；00 于 R2026-09-24.9 冻结 attempt-0 语义、paired N/A、same-control retry 和 idle 终态规则。PLAN-04/QA-04 v1 已明确 BLOCKED；待 03 交付 PLAN-05，04 对稳定 v2 独立复审前不派实现。
- 05 于 2026-09-23 17:23 +08:00 只读复核：深圳 `/`、`/tmp` 同盘可用 68.72 GiB，上海 33.42 GiB。深圳 root `python3` PID 3886978 批次归属未知；两端 GridFTP 服务需保留；认证与数据通道 TLS 条件、运行根权限、峰值 payload/build/archive/evidence 预算均未核实。虽然剩余磁盘高于 10 GiB 保留线，因预算与活动归属未闭合，构建和实验仍 blocked。
- 全套 CTest 尚无全绿证据，代表性重测未完成。05 运维复核两端已有 liburing 开发包，但真实 io_uring 构建/运行能力未验收；旧实验缺库结论只适用于当时构建。
- 用户最新目标见 `docs/DECISIONS/2026-09-23-100m-evidence-driven-optimization.md`。旧路线 R2026-09-23.2 和预选 SENDV-01 已 supersede；先读取旧实验、环境并独立审查，再由证据选择底层/架构实现。
- HIST-QA-01、DIR-PERF-SOURCE-01、LOWLEVEL-DESIGN-01 与 LOWLEVEL-DESIGN-QA-01 的外层 CLI 均以退出码 0 终态；02/04 的完整历史报告、03 设计报告和 04 设计审查已读回。00 已独立完成固定提交的本地 profile，SENDV-IMPL 已具备派发条件；目录 lookahead 仍需阶段观测。云端无构建、传输或清理。

## 文件写入与整合

当前派单进展：03 已完成 LOWLEVEL-SENDV-IMPL-01 独立 worktree 实现；00 的本地 Linux 验证显示 DATA frame 写调用数和 CPU/GiB 改善，但 8-stream wall 中位数略退；04 独立审查 PARTIAL，sendv 暂不合入。05 已完成深圳/上海只读准入复核，但云端仍 blocked。目录固定基线显示 fp1/f4 有重复的并发差异，现有日志缺文件阶段耗时。PLAN-03 因 skip lifecycle 与固定源码冲突已 supersede；ARCH-REVIEW-02、ARBITRATION-03 已回收，00 已将决策更新到 R2026-09-24.9。PLAN-07 已修订，QA-07 独立复核 PASS for implementation design gate，且 03 已启动固定提交隔离 worktree 中的 telemetry 窄实现。该实现只是为后续低层优化补充阶段证据，不是性能优化；目录 lookahead 和跨域实验仍未获准，云端仍 blocked。

旧 QA 对 runner 的 B5–B8 结论继续限制旧矩阵可执行性；它不替代新路线的历史取证、底层实现回归或正式实验验收。

总指挥独占维护本文件、ROSTER 和对用户的状态汇总；每个角色独占 `receipts/NN-role.md`。任务具体修改范围在任务单登记，重叠时先排队。

实现、测试修改使用 `codex/<task-id>` 分支及独立 worktree，禁止多个活跃角色在同一目录切换分支或操作共享 Git index。初始化只写回执，不做 git add/commit。总指挥完成审查后统一提交资料；后续代码整合按任务安排，不使用 `git add .` 混入别人的工作。

## 当前窄任务：R2026-10-09.1

DIR-ASYNC-CONTROL-03 / v2：in_progress。用户明确将当前工作收窄为目录控制连接池+有界pending。输入 d362ea2360bf0cbfe3626a9c93734368a74ec0b6，隔离分支 codex/DIR-ASYNC-CONTROL-03。旧迁移暂停门对本任务的限制和旧单槽设计已 superseded；不宣称迁移验收完成。

00实现C++与提交；02既有聊天编写30-case短实验脚本；04既有聊天编写并执行独立loopback。三个角色独占文件，不并发操作index。01/03/05本轮不新增派单。构建/测试后提交push，短实验留给用户手动运行；性能未验收，不扩大矩阵。任务契约见 ../tasks/2026-10-08-dir-async-control-03.md。
