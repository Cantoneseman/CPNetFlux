# 当前执行（2026-10-18；R2026-10-16-V3-UNIFIED-MULTILINK）

本节为当前生效状态。下方历史路线保留供追溯；历史测试数、资源阻塞和已被替代的下一步不覆盖本轮结论。

- V3-FLOWCUT-01：done。GlobalScheduler 已驱动 N 条逻辑 LinkSet lane；persistent global + LINKS>1 可用，队列 lane 选择与 drain/disconnect/recover/epoch 同步通过。337 单元（336 通过、1 项 io_uring 条件跳过）、378/378 CTest，深圳↔上海双向各 80 MiB 哈希一致。实现提交 edf92bf4081347ea9ad60f196d741499f99b222e 已推送并回读；详情见 docs/tasks/2026-10-18-v3-flowcut-01.md。
- V3-LANE-FAILOVER-01：done。修复 DynamicFileQueue 活跃 lease 未记入 LinkSet inflightBytes，补齐 commit/fail/reject/retry/recovery/cancel 的对称释放；低负载 lane 选择、断线后健康 lane 接管、旧 lease 拒绝和恢复 epoch 已有行为测试。
- 本阶段实现提交 c571de0150037a43346cfe12050a1db783ed10f6，分支 codex/V3-LANE-FAILOVER-01 已推送到 origin；refs/heads/codex/V3-LANE-FAILOVER-01 回读同 SHA。结果见 docs/tasks/2026-10-18-v3-lane-failover-01.md。
- 最终全量构建成功；340 单元测试 339 通过、1 项 io_uring 条件跳过；CTest 381/381、0 失败、1 项条件跳过。token-auth/event-log 在 CTest 进程内注入临时随机测试值。日志位于 /tmp/cpnetflux-runs/V3-LANE-FAILOVER-01-{build-final,unit-final,ctest-final}.log。
- 深圳→上海及上海→深圳各传输 256 MiB，单 data TCP session 故障注入后均 result=pass、2/2 lane、range completion=[2,2]、SHA-256 一致。两方向分别为 a8c1906883db8da9d1f1378873e230cf647157d9c8bfd7ce2837ad5d1e1e5836 与 ff81afe80419ec7442cda8ece2d35eb64c1844a25a81cc9105508ed36c56810a。但各方向 control_reconnect_count=2、data_connect_count=4，表明当前恢复会重建整批控制/数据会话；健康 lane 连续性尚未证明。
- 下一项 V3-LANE-LOCAL-RECOVERY-01（planned，输入提交 c571de0150037a43346cfe12050a1db783ed10f6）：单 lane TransportError 不再 cancel/rebuild 整个 batch，只回收失败 lane 的未提交 lease 并恢复该 lane，保持健康 lane 的 data session 与 epoch；详见 docs/tasks/2026-10-18-v3-lane-local-recovery-01.md。
- 物理 NIC、local bind、不同物理路径的配置与验证按用户要求暂缓；lane-local recovery 完成后再在服务器上配置验证。深圳为唯一权威开发端，上海仅作传输对端。测试服务/端口已停止并核实空闲，证据负载和日志保留在 /tmp，不纳入 Git。

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
