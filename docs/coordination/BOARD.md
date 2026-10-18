# 当前执行（2026-10-18；R2026-10-16-V3-UNIFIED-MULTILINK）

本节为当前生效状态。下方历史路线保留供追溯；历史测试数、资源阻塞和已被替代的下一步不覆盖本轮结论。

- V3-FLOWCUT-01：done。GlobalScheduler 已驱动 N 条逻辑 LinkSet lane；persistent global + LINKS>1 可用，队列 lane 选择与 drain/disconnect/recover/epoch 同步通过。实现提交 edf92bf4081347ea9ad60f196d741499f99b222e 已推送并回读；详情见 docs/tasks/2026-10-18-v3-flowcut-01.md。
- V3-LANE-FAILOVER-01：done。DynamicFileQueue 的 lease/lane 计数、断线接管和 epoch fencing 已有行为测试；提交 c571de0150037a43346cfe12050a1db783ed10f6 已推送到 origin 分支 codex/V3-LANE-FAILOVER-01。
- V3-LANE-LOCAL-RECOVERY-01：done。本轮将 persistent tree 的单 data TransportError 限定在失败 lane：按 channel fail/re-attach，保留健康控制/数据会话，retry attempt 重建 range slot 并复用 checkpoint；全量单元 341/341，定向 CTest 10/10，深圳→上海与上海→深圳各有正常/单 data socket reset 互传，四次均 2/2 且 SHA-256 一致。故障场景均 `lane_local_recovery_count=1`、`data_connect_count=3`、`control_reconnect_count=0`。详情见 docs/tasks/2026-10-18-v3-lane-local-recovery-01.md。
- 完整 CTest 为 379/382；3 个失败均为当前 2 CPU/长连接 30 秒环境型超时（reconnect、stability TLS、pipeline sidecar），已记录原始错误，不据此宣称全量 CTest 全绿。
- V3-LANE-RECOVERY-COVERAGE-01：done。loopback harness 已覆盖 2 lane/多 range、upload/download、TLS+control pipeline；三场景均证明目标 lane 单次恢复、健康 lane 保持、控制连接不重连、树摘要一致。全量 CTest 382/384，剩余 reconnect 与稳定性 TLS 两项仍是 30 秒环境型失败。
- 下一阶段：V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01。验证下载方向的控制连接断开也能只恢复故障 lane；物理 NIC/local bind/不同物理链路验证继续后置。
- 本轮深圳仍是唯一权威 Git 工作区，上海只作传输对端；临时服务/端口已停止并释放，既有服务未改动，证据负载保留在 /tmp，不进入 Git。
- 03 实现与 04 独立复核专用派单此前因 429/active-writer 锁不可用，本轮未冒称已收到角色回执或独立审查结论。

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
