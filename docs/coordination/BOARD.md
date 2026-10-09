# 总指挥任务板

更新：2026-10-09。路线版本：`R2026-10-09.2`（深圳根目录串行开发；目录持久数据连接→短跨域实测→有界并发调度）。

当前目的：先从历史实验数据寻找有证据支撑的性能问题，再针对 C++ 传输热路径和目录数据面做底层/架构级优化，最后通过深圳—上海 100 Mbps 跨域固定提交实验与真实 GridFTP 对照。历史汇总差距只是线索，不能预设 syscall 或 manifest 是主要瓶颈。广州—深圳 100G 暂不作为当前验收环境。

用户最新授权：深圳 `/root/projects/CPNetFlux` 是唯一权威工作区，根目录单人串行开发；上海只作对端，Windows 只作连接/查看/回收。迁移门与强制 worktree 已被最新 AGENTS/云端决策取代。每个阶段提交明确文件并推送 GitHub；当前由 00 单人执行，不并发修改。

当前状态：一期代码 `53a6dfbf46792e26a613059b9da5df615873f641` 已 push 到 GitHub 并回读一致。固定提交源码归档 Release 再验：222 项 CTest，221 pass/1 io_uring skip/0 fail。128×1MiB 双向 loopback 各为 1 条实际 data TCP 连接，独立 hash/manifest 通过。本版本尚未跑跨域对比，不宣称达到 GridFTP 80–90%。

| 当前任务 | 状态 | 验收/下一步 |
|---|---|---|
| DIR-V2-TRANSFER-ENGINE-01 | done（一期；完整架构未完成） | 显式 `--data-session-reuse tree`，单 worker/流；固定提交证据见 `docs/tasks/2026-10-09-data-session-reuse-result.md` |
| DIR-V2-SHORT-COMPARE | 待实验指令 | 固定提交/二进制，两端空间与 GSI 重验，短目录 reuse 开关 + GridFTP；脚本执行，不持续轮询 |
| DIR-V2-BOUNDED-SCHEDULING | 后续 | 固定通道预算、有界 pending 和异步读写，目前未实现 |

以下为历史任务记录；时间、blocked 和旧门只描述当时事实，不是当前执行前置条件。

| ID | 工作 | 责任角色 | 状态 | 前置条件/验收 |
|---|---|---|---|---|
| SETUP-01 | 新 Codex 项目与六个聊天 | 当前设置任务 | blocked，自动派单验证未完成 | 项目、六个持久聊天、prompt 与回执已核验；00 缺少终端工具，SETUP-CHECK 未派出 |
| ENV-01 | 上海磁盘只读盘点和清理方案 | 05 运维 | ready，待总指挥派发 | 排除受保护项目；列目录归属、证据备份、可释放空间；本任务本身不删除 |
| SPEC-01 | 校正 profiling 时间边界、schema、矩阵 | 01 架构 + 04 质量审查 | ready，待派发 | 区间定义可验证；消除 first_payload 不等式和旧带宽歧义；区分 off 基线与 scheduler 专项 |
| VERIFY-01 | 固定构建与阶段 0 验收复核 | 04 质量 + 05 运维 | blocked | 实验资源恢复、记录明确 commit/hash、解决测试 token 注入；不以 skip 当 pass |
| IMPL-01 | 目录阶段 instrumentation | 03 实现 | blocked | SPEC-01 确认，任务单明确文件与验收；不得顺带优化协议 |
| EXP-01 | 代表性小矩阵 | 02 实验 + 05 运维 | blocked | 环境、schema、固定构建与 QA 通过；两端空间充足 |
| DECIDE-01 | 根据证据选择下一项性能优化 | 00 总指挥 + 01 架构 | blocked | EXP-01 完整报告；用户确认有实质取舍的新方向 |

状态含义：`ready` 可被派发但未启动；`in_progress` 需真实执行证据；`review` 待验收；`done` 有验收链接；`blocked` 有依赖；`superseded` 规格已失效。不得仅因 prompt 写好就把角色或任务标记完成。

## 历史阻塞快照（不代替上方当前状态）

- 设置阻塞：六个聊天和资料已建立；总指挥部分轮次缺少 shell/文件工具。00→04 最小派单测试未执行，不能称为全自动协作已验收。等待总指挥会话实际恢复工具后执行现有 SETUP-CHECK；不重复创建角色。
- 上海 `/` 和 `/tmp` 可用空间 0，不能启动新性能实验或大构建。
- 全套 CTest 尚无全绿证据，代表性重测未完成。05 运维复核两端已有 liburing 开发包，但真实 io_uring 构建/运行能力未验收；旧实验缺库结论只适用于当时构建。
- 此处的先后顺序是接手建议；初始化聊天只阅读、写自己的回执，不自动执行整张任务板。

## 文件写入与整合

总指挥独占维护本文件、ROSTER 和对用户的状态汇总；每个角色独占 `receipts/NN-role.md`。任务具体修改范围在任务单登记，重叠时先排队。

旧多人 worktree 要求已由用户最新串行模式取代：直接在深圳根目录开发，禁止其他角色同时写入/index 操作。使用 `codex/<task-id>` 分支，仅提交逐项审查文件，GitHub push 与 SHA 回读后才称为已备份，不使用 `git add .`。
