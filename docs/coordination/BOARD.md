# 当前执行（2026-10-17；R2026-10-16-V3-UNIFIED-MULTILINK）

本节为当前生效状态。下方 2026-10-14 内容与未提交改动保留作历史记录；其中旧测试数量、资源阻塞和旧路线不作为本轮结论。

- V3-WORK-UNIFY-01：done。统一 WorkItem/WorkLease/WorkerContext/ConnectionEpoch 契约已接入单文件与动态目录共用的 persistent sender；状态、owner、epoch、deadline、drain、断线回收和整文件 attempt 重派通过行为测试。
- 输入提交：cbebce51e08bcdd986179e1f633885792f9bf762；本轮在独立 codex/V3-WORK-UNIFY-02 worktree 实施，结果与残余边界见 docs/tasks/2026-10-17-v3-work-unify-02-result.md。
- 当前项：V3-WORK-UNIFY-02（实施中）。persistent upload/download 的 global scheduler、共享 dispatch window、receiver incoming lease、fixed/adaptive 实际模式和 lease/worker/epoch/retry 事件已接通；完整共同 executor 抽取、自动 reconnect/retry 与 manifest 对账仍待下一轮。尚未完成十链路、跨域性能或生产稳定性验收。
- 实现提交 db469b9d109e8f5624b1d5977b4c9cee3a66313f 已推送，远端 refs/heads/codex/V3-WORK-UNIFY-02 已回读同 SHA。
- 开发位置：深圳根目录单写入者；GridFTP proxy 在实际对比实验前刷新，本阶段未使用真实 GridFTP。

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
