# 当前执行（2026-10-20；R2026-10-16-V3-UNIFIED-MULTILINK）

本节优先于下方历史路线和旧任务快照。深圳 /root/projects/CPNetFlux 为唯一权威开发目录；当前总指挥单人串行直接开发、测试和提交，不再新建开发 worktree，不使用子代理；上海只作传输对端。2026-10-20 已保全主目录旧改动并将当前代码接回根目录，详见 V3-LOGICAL-10LINK-01 任务记录。

- V3-ONLY-PEER-SYNC-01：已完成。提交 `10d05bcdf972faec15d3a19bae6230cf27905099` 已推送；深圳与上海服务运行同 SHA 的 V3 server，完整 CTest 393/393 通过。深圳→上海生产服务上传、上海→深圳下载均通过且树哈希一致；上海客户端→深圳隔离 V3 服务也经 SSH 隧道通过。上海直连深圳 TCP/2121 仍超时，详见任务记录和 evidence.json。
- V3-ONLY-LEGACY-CODE-AUDIT-01：已完成。移除三个无调用点的旧 tree runners 和仅供其使用的逐文件/pipeline/scheduler 逻辑，共净删 2,052 行；保留 V3 executor 和当前 V3 manifest/checkpoint。提交 a41ee88050eab021a9d286aa514351a29bdb573f 已推送并由 origin 回读一致；两端 clean build 二进制一致，CTest 393/393，深圳↔上海实际上传/下载均通过。详见任务记录和 evidence.json。
- 版本门禁：深圳 commit、两端构建状态、二进制 SHA-256 和服务启动命令必须记录；任一不一致时停止互传，不把旧服务或旧逻辑当作 V3 验收。

- V3-LANE-MULTI-FAULT-BUDGET-01：已完成。两数据 lane 同时故障的 12 个跨机场景和 8 项回归全部通过；N=0/1/2 分别表现为 0 次恢复并明确失败、1 次恢复后明确失败、2 次恢复后成功。详情见 docs/tasks/2026-10-19-v3-lane-multi-fault-budget-01.md。
- V3-LANE-MIXED-FAULT-BUDGET-01：已完成。数据连接与另一 lane 控制连接同时故障的深圳/上海服务端 × 上传/下载 × N=0/1/2 共 12 个 case 全部通过；没有发现数据恢复和控制重连重复消费总预算。实现提交 6182067f2dc88ccf7ddae49bc27b9ff13d0ed53f，已推送并回读 origin 同 SHA；验收记录见 docs/tasks/2026-10-19-v3-lane-mixed-fault-budget-01.md。
- V3-LANE-FAULT-STORM-BUDGET-01：已完成。连续注入 3 次 data-lane 故障，深圳/上海服务端 × 上传/下载 × N=0/1/2/3 共 16 个 case 全部通过；N=0/1/2 恢复次数严格为 0/1/2 并清楚失败，N=3 恢复 3 次并成功，树哈希一致。实现提交 `19a449868e1650b75214a9db59c692b71ee7f272` 已推送并回读一致；详情见 `docs/tasks/2026-10-19-v3-lane-fault-storm-budget-01.md`。
- V3-LANE-MIXED-FAULT-STORM-BUDGET-01：已完成。按“数据断开 → 控制断开 → 再次数据断开”顺序注入，深圳/上海服务端 × 上传/下载 × N=0/1/2/3 共 16 个 case 全部通过；预算严格为 0/1/2/3，N=3 树哈希一致，batch ID 保持且无全局 control reconnect。最终提交 `3d14b5c27531ade6f9ae9982b28a5dfd4b502f69` 已推送并回读一致；详情见 `docs/tasks/2026-10-19-v3-lane-mixed-fault-storm-budget-01.md`。
- V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01：已完成。运行协商改为显式 XCPNETFLUX V3 / CPNetFlux V3，服务端拒绝旧 V2 greeting、客户端校验 V3 回执，不再存在这条协商降级歧义；两端同 commit 构建且 CTest 各 393/393。深圳客户端连上海正式服务、上海客户端经 SSH 隧道连深圳回环 V3 服务，双向上传/下载及 SHA-256/树哈希均通过。代码提交 5a2b2e2808f7b5614de6f5988d440a9cb406365a 已推送并回读一致；任务结果见 docs/tasks/2026-10-19-v3-only-residual-runtime-audit-01.md 和 evidence.json。
- V3-ONLY-NO-PREDEPLOYMENT-COMPATIBILITY-01：已完成。项目尚未正式部署，本轮删除 transfer manifest V1 兼容读取和恢复分支，并把内部 schema 常量与 V1/V2 运行版本命名分开；深圳、上海同一提交构建且 CTest 各 393/393，正式服务已运行新 server，深圳→上海生产服务和上海→深圳回环 V3 服务双向互传均通过、无 fallback、源/目的 SHA-256 一致。提交 e545e4b1bd328e6615d5b0bdd0bca1fedd0092db 已推送；任务记录和证据见 docs/tasks/2026-10-19-v3-only-no-predeployment-compatibility-01.md 与 evidence.json。
- 历史 checkpoint 互操作计划已取消：没有已部署用户或生产断点需要迁移，不再为 V1/V2 历史输入保留开发和跨机验收门。
- 非目标：本轮及下一计划都不验证物理 NIC、local bind 或不同物理链路；按用户要求待软件故障恢复开发完成后再配置服务器验证。
- 历史环境边界（上一 V3-only 阶段）：当时深圳与上海正式服务均 active，运行 SHA 与该阶段构建一致。本轮 Slice E 仅验收隔离临时服务，没有将正式服务切换到本轮构建。上海客户端反向验收使用深圳回环匿名临时服务和 SSH 隧道；临时服务、隧道、任务 payload 和上海正式服务下的任务测试目录均已清理，摘要与 manifest archive 保留。上海主动直连深圳 TCP/2121 仍超时，不能把隧道测试说成直连生产端口通过。
- V3-RELEASE-STABILITY-01 Slice A-E 已完成，父任务继续进行中。Slice E 修复目标冲突被误报为断网/重试耗尽及 TLS 多通道终态丢失；最终提交 63e5fcc6f59db60c09b2ccb64d6bb063761ced2d 已推送并回读一致。两端同源码构建、关键二进制 SHA 一致、完整 CTest 各 399/399（退出 0），深圳客户端与上海临时服务的上传/下载冲突及完成后重复 resume 共 4/4 通过，旧目标未改、重复 resume 数据发送量为 0。详见任务与 evidence JSON。
- V3-LOGICAL-10LINK-01：已完成。提交 bb983f5e91711d55b312a841e27b2ab721cd291d 已推送并回读一致；实际通道上限统一为 10，修复有界队列暂时没有本通道任务就提前结束、后续文件无人处理的问题。两端同源码构建且关键二进制 SHA 一致，完整 CTest 各 406/406；本机专门矩阵 14/14（含十通道 TLS/断线恢复），深圳客户端与上海临时服务真实 1/2/4/8/10 上传/下载矩阵 10/10，哈希和分片覆盖均通过。开发已回深圳项目根目录，临时进程/端口/payload 已清理。详见 docs/tasks/2026-10-20-v3-logical-10link-01.md 和 evidence.json。
- 当前软件阶段剩余 2 大项：先 V3-EDGE-STREAM-01 补齐有界读盘/校验/发送流水线和主机压力观测（已有实现/缺口见 2026-10-20-v3-edge-stream-inventory.md），再 V3-CANDIDATE-CLOSEOUT-01 固定稳定性矩阵、安装包/隔离安装、文档冻结。剩余估算 3–5 个工作日，若发现重大缺陷须重估；物理 NIC/不同物理路径继续后置。
- V3-COMPOSITION-STABILITY-01：已完成。扩展四步连续混合故障 `data#2 -> control#3 -> data#4 -> control#5`，覆盖深圳/上海服务端、上传/下载、重试预算 N=0..4 共 20/20 场景；两端同提交构建、关键二进制 SHA-256 一致，组合 CTest 1/1 通过。实现提交 `a596970c636b32a05789bb90f663150928e6f897` 已推送；任务记录和 evidence.json 见 `docs/tasks/2026-10-20-v3-composition-stability-01.md`。
- V3-RELEASE-STABILITY-01：Slice A、B、C、D、E 已完成，父任务继续进行中（最新 Slice E 结果与后续计划以上方条目为准）。Slice A 验收客户端中断续传；Slice B 新增服务进程重启后的 V3 上传/下载 resume 门禁；Slice C 验收数据静默超时后保留检查点并由 V3 续传。提交 3c4ec5b 与校验修复 e56c631 均已推送并回读一致；同一 e56c631 源码在深圳/上海构建，关键二进制 SHA 一致，新 CTest 两端 1/1、完整 CTest 两端 395/395。512 MiB 深圳→上海上传/下载各在服务端停止后从持久 checkpoint 恢复，V3 persistent-tree、无 fallback，源/目标文件及目录 SHA 一致。正式服务保持 active，临时进程与 payload 已清理。Slice C 另有 512 MiB 云间用例因固定 5 秒总截止时间失败；64 MiB 双向用例通过。Slice D 已验证部分传输后的损坏 checkpoint 安全失败（397/397 双端 CTest、双向跨云）；Slice E 已验收目标冲突和重复 resume（399/399 双端 CTest、4 个跨云用例）。任务与证据见 docs/tasks/2026-10-20-v3-release-stability-01.md 和 evidence JSON。

## 历史执行快照

V3-FLOWCUT、V3-LANE-FAILOVER、V3-LANE-LOCAL-RECOVERY 与更早路线记录见下文历史部分；其旧“下一阶段”文字已被上方当前任务替代。最近完整状态以各自 `docs/tasks/` 记录为准。
# 当前执行（2026-10-14）

当前主线：V3-COMPOSITION-01 组合稳定性回归 -> V3-WORKER-STATE-01 统一 scheduler/pipeline/持久连接 worker 生命周期 -> V3-RELEASE-STABILITY-01 稳定候选验收。V3-WORKER-STATE-01A 状态报告修复已通过；下一步继续实现共同 worker/lease 生命周期，不据此宣称所有策略组合或生产 readiness 已验证。

# 总指挥任务板

更新：2026-10-14。路线版本：R2026-10-14.1（服务器一键初始化->安全节点包->桌面真实认证->V3 统一事务与连接生命周期->稳定候选）。

当前目的：安装后服务器可用短命令配置认证并生成可安全导入的节点包；桌面端真实检查节点并传输文件/目录。核心层统一事务身份、工作分配与连接生命周期；只对经组合回归验证的能力承诺稳定支持。

用户最新授权：深圳 /root/projects/CPNetFlux 是唯一权威工作区，根目录单人串行开发；上海只作对端，Windows 只作连接/查看/回收。按深圳仓库当前 AGENTS 串行在根目录开发；每阶段只提交明确文件并推送已核实的 GitHub remote。

当前提交：1817d8385211df55c1457bedb181fe5f85a6250c，origin/main SHA 一致。V3-WORKER-STATE-01A 的 requested/actual/fallback 状态修复及 299/299 CTest 通过；V3 整体仍未达到稳定版门槛。

路线依据：docs/DECISIONS/2026-10-14-v3-stable-composition.md。V3-UNIFIED-01 的 V1 退役和事务引擎交付继续有效，但不再作为 scheduler、pipeline 和持久 worker 已组合的证明。

| 当前任务 | 状态 | 验收/下一步 |
|---|---|---|
| V3-COMPOSITION-01 | done | 持久 data TLS 身份、下载恢复源元数据预检、manifest 身份复用和真实 payload 统计修复；CRC32C + dynamic range + TLS + committed resume/source-change rejection 通过；298/298 CTest，1 io_uring skip；代码提交 688ce60，结果见 docs/tasks/2026-10-14-v3-composition-01-result.md |
| V3-WORKER-STATE-01A | done | JSON 如实记录 pipeline 自动降级的 requested/actual/fallback_reason；默认兼容路径与显式拒绝通过回归，299/299 CTest；代码提交 1817d83，结果见 docs/tasks/2026-10-14-v3-worker-state-01a-result.md |
| V3-WORKER-STATE-01 | in_progress | 继续统一 scheduler/pipeline/持久 worker 的 dispatch lease、取消、超时、断线回收、连接生命周期与能力协商；01A 仅完成可观测性子阶段 |
| V3-COMPOSITION-02 | done | 持久 V3 上传自动压缩与逐帧/逐文件决策观测；TLS/CRC32C/range/resume 混合数据回归，298/298 CTest，内部 DEB；代码提交 66277c7，结果见 docs/tasks/2026-10-14-v3-composition-02-result.md |
| V3-RELEASE-STABILITY-01 | planned | 支持能力组合的多点故障矩阵、完整 CTest、候选安装包；无真实跨机实验不宣称跨域稳定 |
| DIR-V2-TRANSFER-ENGINE-01 | done（一期；完整架构未完成） | 显式 --data-session-reuse tree，单 worker/流；固定提交证据见 docs/tasks/2026-10-09-data-session-reuse-result.md |


## 历史阻塞快照（不代替上方当前状态）

- 设置阻塞：六个聊天和资料已建立；总指挥部分轮次缺少 shell/文件工具。00→04 最小派单测试未执行，不能称为全自动协作已验收。等待总指挥会话实际恢复工具后执行现有 SETUP-CHECK；不重复创建角色。
- 上海 `/` 和 `/tmp` 可用空间 0，不能启动新性能实验或大构建。
- 全套 CTest 尚无全绿证据，代表性重测未完成。05 运维复核两端已有 liburing 开发包，但真实 io_uring 构建/运行能力未验收；旧实验缺库结论只适用于当时构建。
- 此处的先后顺序是接手建议；初始化聊天只阅读、写自己的回执，不自动执行整张任务板。

## 文件写入与整合

总指挥独占维护本文件、ROSTER 和对用户的状态汇总；每个角色独占 `receipts/NN-role.md`。任务具体修改范围在任务单登记，重叠时先排队。

旧多人 worktree 要求已由用户最新串行模式取代：直接在深圳根目录开发，禁止其他角色同时写入/index 操作。使用 `codex/<task-id>` 分支，仅提交逐项审查文件，GitHub push 与 SHA 回读后才称为已备份，不使用 `git add .`。
## 2026-10-18 V3-MULTILINK-RANGE-01 当前交付

- 状态：done。输入提交 49eb3eea84c4455d4c407f07fe5ec1070e764825；独立 worktree `/root/projects/CPNetFlux-worktrees/v3-multilink-range-01`，分支 `codex/V3-MULTILINK-RANGE-01`。
- 实际解决：LINKS/LINK_CAPS 不再只停留在控制面；persistent tree 至少建立请求数量的数据会话，worker 获得稳定 lane ID，range/整文件 claim 按 lane 亲和分配，并在 lane 不可接受时走既有回收/重试接管；摘要输出 observed data connections 和逐 lane range 完成数。
- 验收：334 个单元测试 333 通过、1 个 io_uring 条件跳过；CTest 375/375，0 失败；深圳↔上海双向 80 MiB、2 lane、每 lane 2 个 range 完成，源/目的 SHA-256 一致。证据详见 `docs/tasks/2026-10-18-v3-multilink-range-01.md`。
- 边界：本轮只证明两个独立 TCP data session 和逻辑 lane/range 绑定，不证明不同物理网卡/交换路径、吞吐提升、全局调度多链路、故障 drain/cut 或十链路稳定性。
- 后续：V3-FLOWCUT-01 已完成 GlobalScheduler/逻辑 LinkSet 接入；V3-LANE-FAILOVER-01 将验证单 lane 故障回收与 epoch 保护。endpoint/local-bind/物理路径配置和不同物理链路验证依用户要求暂缓到代码侧故障切换完成后。

## 2026-10-18 V3-LANE-RECOVERY-COVERAGE-01 当前交付

- 状态：done。输入提交 `bd3a1b02d3dc4d16346dd8bf30d455274c295fc7`；独立 worktree `/root/projects/CPNetFlux-worktrees/v3-lane-recovery-coverage-01`，分支 `codex/V3-LANE-RECOVERY-COVERAGE-01`。
- 实际解决：把单 data socket reset 从一次性手工注入固化为可重复 fault harness；补齐 2 lane/多 range、upload/download、TLS+control pipeline 的 lane-local 恢复断言；同时修复失败 lane 重挂接时阻塞健康 lane 完成的 persistent directory batch 竞态。
- 验收：构建退出码 0；单元 342/342；新增 `cpnetflux_lane_recovery_smoke` 1/1；手工 harness upload、download、TLS+pipeline 3/3。三场景均为 `lane_local_recovery_count=1`、`control_reconnect_count=0`、`data_connect_count=3`，两个 lane 有 range 完成记录且树 SHA 一致。
- 全量回归：382/384 通过；`cpnetflux_reconnect_smoke` 与 `cpnetflux_v3_stability_matrix` 仍分别因 30 秒 transfer deadline/TLS peer closed 失败，按环境型长连接问题保留原始证据；`cpnetflux_tree_pipeline_sidecar_smoke` 已通过。
- 边界：本轮只验证软件 lane-local 恢复和 loopback 故障注入，不验证吞吐、生产 readiness、物理 NIC、local bind、源地址或不同物理链路。
- 下一阶段：`V3-LANE-RECOVERY-INTEROP-01`，先做深圳↔上海双向真实链路上的同一恢复断言，随后再单独安排物理链路验证。

## 2026-10-19 V3-LANE-RECOVERY-INTEROP-01 当前交付

- 状态：done；输入提交 `92bc148d2d0aeacdd0cb021abba918018af40699`；实现提交 `2b223b1bbb8ed9db2d0119d3c63c07129292d737` 已推送，GitHub ref 回读同 SHA；worktree `/root/projects/CPNetFlux-worktrees/v3-lane-recovery-interop-01`，分支 `codex/V3-LANE-RECOVERY-INTEROP-01`。
- 实际解决：健康 lane 完成后，其 done 状态不再被清理抹掉；失败 lane 重连不再误等健康 lane 重新上线。跨机 runner 固化 2-lane/多-range TCP reset 测试。
- 验收：单元 7/7、loopback 恢复 CTest 1/1；真实深圳→上海、上海→深圳均 tree hash 相等，lane-local recovery=1、control reconnect=0、data connect=3、ranges=[2,2]；跨机 CTest 1/1（36.20 秒，矩阵 `/tmp/cpnetflux-runs/V3-LANE-RECOVERY-INTEROP-01/build/cpnetflux-lane-recovery-interop-evidence/20261018T165743Z-1792342663694352372/matrix.json`）。证据详见 `docs/tasks/2026-10-19-v3-lane-recovery-interop-01.md` 和 `/tmp/cpnetflux-runs/V3-LANE-RECOVERY-INTEROP-01/bidirectional-final1/`。
- 边界：只证明 plain TCP 数据通道 reset 的软件恢复；控制通道断开、TLS/认证互操作、物理 NIC/local bind/不同物理路径、吞吐和生产 readiness 未验证。
- 下一阶段：`V3-LANE-CONTROL-RECOVERY-01`，解决 lane 控制连接断开时升级成整批重连、扰动健康 lane 的问题；按原批次身份只重建故障 lane 的控制与数据连接。物理链路验证继续后置。

## 2026-10-19 V3-LANE-CONTROL-RECOVERY-01 当前交付

- 状态：done；输入提交 `5aa0396d3b47bb2fc6d6d403b28ced62b2cd98ab`；隔离 worktree `/root/projects/CPNetFlux-worktrees/v3-lane-control-recovery-01`，分支 `codex/V3-LANE-CONTROL-RECOVERY-01`。
- 实际解决：控制和数据连接同时断开时，不再默认整批重连；先只重建故障 lane 的控制/数据会话，沿用原 batch ID、lane 编号和检查点，健康 lane 保持原完成状态。
- 提交：规划文档 `3feb706d85a28a3be87fe344efa955df58e80f3e`；实现 `c5352189ef15c6d1f65a09a281b19ef799e92c95`；均已推送到 origin，并由 `git ls-remote` 回读相同 SHA。
- 验收：相关 CTest 3/3、完整单元 342/342；深圳↔上海双向控制 RST 均得到同一树 SHA，`lane_local_recovery_count=1`、`lane_control_reconnect_count=1`、`control_reconnect_count=0`、`reconnect_attempts=0`、`control_connect_count=3`、`data_connect_count=3`、`persistent_lane_range_completed=[2,2]`。矩阵见 `/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-01/build/cpnetflux-lane-control-recovery-interop-evidence/20261018T172309Z-1792344189085800644/matrix.json`。
- 边界：已验证 plain TCP 跨云上传控制连接故障；下载控制连接故障、TLS/生产认证互操作、物理 NIC/local bind/不同物理路径、吞吐和生产 readiness 尚未由本阶段证明。
- 下一阶段：`V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01`，双向验证下载控制连接故障下的 batch/lane 续传、检查点不重复写入、健康 lane 保持和最终 SHA 一致。

## 2026-10-19 V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01 当前交付

- 状态：done；输入提交 `0f9bf8a28f9df2cfb0567bea998b5a930f9a8732`；隔离 worktree `/root/projects/CPNetFlux-worktrees/v3-lane-control-recovery-download-01`，分支 `codex/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01`。
- 实际解决：补齐下载方向的控制连接故障验收；runner 现在清理远端旧 manifest、从实际下载客户端回收结果，并以独立 CTest 双向验证，不再把旧清单或错误路径误报成传输失败。
- 提交：规划文档 `3d1977ea5b3b642c68fd6e75c5d7e81f259653cd`；实现 `e8e3985925c5f8c1f494617fc3dd1da9d3dbf741`；均已推送到 origin，并由 `git ls-remote` 回读相同 SHA。
- 验收：下载控制恢复 CTest 1/1、上传控制恢复回归 1/1、完整单元 342/342；深圳↔上海双向下载均得到树 SHA `5816ca45fdc866157f2c8c9d0097009af11a3132eee66cd2fcdd745ab696c092`，`lane_local_recovery_count=1`、`lane_control_reconnect_count=1`、`control_reconnect_count=0`、`reconnect_attempts=0`、`control_connect_count=3`、`data_connect_count=3`。矩阵见 `/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01/build/cpnetflux-lane-control-recovery-download-interop-evidence/20261018T174234Z-1792345354963629285/matrix.json`。
- 边界：本阶段未改生产状态机；TLS/生产认证互操作、物理 NIC/local bind/不同物理路径、吞吐和 production readiness 仍未证明。
- 下一阶段：`V3-LANE-RECOVERY-BUDGET-01`，统一 lane-local 与整批恢复的 `--reconnect-attempts` 总预算，先用连续故障注入建立红测，再实现有界计数和明确耗尽原因。

## 2026-10-19 V3-LANE-RECOVERY-BUDGET-01 交付结果

- 状态：done；输入提交 `6d1e11cf31cafb6bd316f1bc6754096439ec7e6b`；隔离 worktree `/root/projects/CPNetFlux-worktrees/v3-lane-recovery-budget-01`，分支 `codex/V3-LANE-RECOVERY-BUDGET-01`。
- 实际解决：lane 局部恢复和整批恢复共用 `--reconnect-attempts N` 一个预算；JSON 增加配置预算和实际消耗字段；耗尽时报可读原因，N=0 不恢复。
- 实现提交：`0cc1696dffda995b078516ffece8a7e47834cbd3` 已推送，GitHub ref 回读同 SHA。
- 验收：单元 342/342、lane recovery smoke 1/1；原有上传/下载控制故障 CTest 通过；双云注入 N=1 上传/下载及 N=2 上传时，深圳和上海各自为服务端的结果都精确消费 1/1/2 次；N=0 双向保持 0 次恢复。测试矩阵和边界见 `docs/tasks/2026-10-19-v3-lane-recovery-budget-01.md`。
- 额外发现：`cpnetflux_reconnect_smoke` 的 data-only reset 测试在 30 秒 deadline 超时；同样失败可在输入提交旧二进制中复现，已确认是任务前存在的问题，不是预算改动引入。
- 下一阶段：V3-LANE-MULTI-FAULT-BUDGET-01，验证两条 lane 并发故障时共享重试预算是否会被超额消费；物理 NIC/local bind 与真实不同物理路径继续暂缓。


## 2026-10-19 V3-LANE-DATA-FAULT-RECOVERY-01 验收完成（待独立复核）

- 任务输入提交 4af82db824d7d4a7c02accb4f63a40b62e3caa9a；实现 worktree /root/projects/CPNetFlux-worktrees/v3-lane-data-fault-recovery-01，分支 codex/V3-LANE-DATA-FAULT-RECOVERY-01。
- GitHub 阶段提交 e2598016d55bd66ebc2ec809a07dec9f4b189638 已推送；远端分支 SHA 回读一致。
- 修复 data socket 在所有 lane ready 前断开后服务端仍等满 30 秒的问题；完整性审计现在会在重试前执行，已提交文件损坏时明确失败。
- 验收：单元 344/344；三项定向 CTest 3/3；本地故障矩阵 17/17；深圳/上海作服务端 × 上传/下载双云矩阵 4/4，四项树摘要一致、每项恢复一次且不超过预算。详细命令和证据见 docs/tasks/2026-10-19-v3-lane-data-fault-recovery-01.md。
- 独立质量复核派单遇到 429；CLI 续接报告 active writer，未获得审查回执。当前只记录实现和测试结果，不声称独立审查通过。
- 物理 NIC/local bind/不同物理链路、吞吐优化和生产 readiness 均不在本任务结论内。
