# 当前执行（2026-10-18；R2026-10-16-V3-UNIFIED-MULTILINK）

本节为当前生效状态。下方 2026-10-14 内容与未提交改动保留作历史记录；其中旧测试数量、资源阻塞和旧路线不作为本轮结论。

- V3-WORK-UNIFY-01：done。统一 WorkItem/WorkLease/WorkerContext/ConnectionEpoch 契约已接入单文件与动态目录共用的 persistent sender；状态、owner、epoch、deadline、drain、断线回收和整文件 attempt 重派通过行为测试。
- 输入提交：cbebce51e08bcdd986179e1f633885792f9bf762；本轮在独立 codex/V3-WORK-UNIFY-02 worktree 实施，结果与残余边界见 docs/tasks/2026-10-17-v3-work-unify-02-result.md。
- 父项：V3-WORK-UNIFY-02（实施中）。persistent upload/download 的 global scheduler、共享 dispatch window、receiver incoming lease、fixed/adaptive 实际模式和 lease/worker/epoch/retry 事件已接通；共同 lease executor 与受控恢复已由 03 交付；实际客户端有界自动 reconnect/retry 与远端 manifest 对账已由 04 交付；全部兼容编排合并及取消/超时收口仍待 05。尚未完成十链路、跨域性能或生产稳定性验收。
- 实现提交 db469b9d109e8f5624b1d5977b4c9cee3a66313f 已推送，远端 refs/heads/codex/V3-WORK-UNIFY-02 已回读同 SHA。
- 当前子阶段 V3-WORK-UNIFY-03：done。共同 lease executor、完整 ledger 的有界受控恢复、多 range 补传发布修复；最终代码 05d2ef6 的 354/354 CTest 通过（0 skip），27 项定向回归通过。输入 739a124，独立 codex/V3-WORK-UNIFY-03 worktree；实现和修复已 push/回读，详见 docs/tasks/2026-10-17-v3-work-unify-03-result.md。
- V3-WORK-UNIFY-04：done。实际 persistent dynamic tree 显式有界重连、旧 batch 释放屏障、manifest 对账；最终代码 ff71014 的 357/357 CTest（0 skip）与深圳/上海 8/8 真实互传通过，4 次中断均自动恢复且 SHA-256 一致。代码已 push/回读；结果 docs/tasks/2026-10-17-v3-work-unify-04-result.md。
- V3-WORK-UNIFY-05：done。static/per-file/global/pipeline 兼容编排共用 owner，终态失败释放 credit，stop claims/取消/join/静态保序已收口；362/362 CTest 与最终版深圳↔上海双向 static persistent tree 互传通过；结果 docs/tasks/2026-10-17-v3-work-unify-05-result.md。
- V3-LINKSET-01：done。LinkSet、单链路兼容视图、稳定能力摘要及 XCPNETFLUX V2 可选 LINKS/LINK_CAPS 已实现；333/333 单元、374/374 CTest 通过，深圳↔上海双向显式控制面协商传输与跨机拒绝均通过，文件哈希一致；控制面结果见 docs/tasks/2026-10-18-v3-linkset-01.md。
- 当前 V3-MULTILINK-RANGE-01：done。LINKS/LINK_CAPS 已驱动 persistent data plane 的独立 TCP session、lane ID 和 range affinity；334 单元（333 pass、1 skip）、375/375 CTest 及深圳↔上海双向 80 MiB 哈希验收通过；下一步为 V3-FLOWCUT-01。
- 开发位置：深圳独立 worktree 单写入者（依本轮用户明确的 worktree 要求）；上海仅部署固定二进制作传输对端。两端本任务进程、端口与可再生 payload 已回收，原始诊断保留。

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
- 下一项：V3-FLOWCUT-01，先做 endpoint/local-bind/path identity 与 LinkSet 的 global-scheduler adapter，再做 lane drain/cut、失败 lane reclaim、epoch 证据和 1/2/4/8/10-link 双向矩阵。
