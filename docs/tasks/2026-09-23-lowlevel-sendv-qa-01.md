# LOWLEVEL-SENDV-QA-01：plain DATA vectored write 独立质量复核

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；验收角色：04 测试与质量；最终收口：00
- 目标及理由：独立检查 03 的未提交 sendv worktree，确认数据帧字节/错误路径/范围符合 LOWLEVEL-SENDV-IMPL-01，并复核 00 补跑的构建、功能测试与固定 before/after 证据；决定该局部实现是否可保留，不能据 loopback 推出跨域收益。
- 非目标：不改源码或测试、不操作 Git index/分支、不 SSH、不构建云端、不清理、不提交，不修改 GridFTP/TLS 默认策略，不决定目录 lookahead。
- 输入 commit（完整 SHA）、工作树状态：输入基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；实现位于 `C:\Users\12563\AppData\Local\Temp\cpnetflux-lowlevel-sendv-impl-01\worktree`，分支 `codex/LOWLEVEL-SENDV-IMPL-01`，未提交。请记录复核时 HEAD/status；00 已在 Windows 核实代码范围。
- 必读资料和证据路径：本任务；`docs/tasks/2026-09-23-lowlevel-sendv-impl-01.md`；`docs/tasks/2026-09-23-lowlevel-sendv-impl-01-result.md`；`docs/tasks/2026-09-23-lowlevel-single-profile-00-result.md`；worktree 中 7 个实现/测试/CMake 改动；00 的小型汇总 `ab/summary.json`、`ab/results-corrected.csv`（WSL ext4 证据根 `/home/sumu/cpnetflux-evidence/LOWLEVEL-SENDV-IMPL-01-root-final-20260923`）。原始 strace 留在同目录 `ab/`，只按需要抽查。
- 允许修改的文件/目录；禁止修改的资源：04 仅写 `docs/tasks/2026-09-23-lowlevel-sendv-qa-01-result.md` 与 `docs/tasks/2026-09-23-lowlevel-sendv-qa-01-last-message.md`；源码、测试、其他任务文档、BOARD/ROSTER、Git index、云端均只读/禁止改动。
- 工作分支/worktree：只读查看上述独立 worktree；不要在共享根目录切分支或创建修改。
- 前置条件、环境占用和停止条件：实现 00 补跑 Release Linux build 111/111、FramedDataSocketTest 6/6、file smoke 3/3、tree/resume/control-reuse/changed/edge/manifest smoke 8/8；固定 256 MiB trace/wall 矩阵 24/24 hash/退出码正确。若无法访问 worktree/WSL 证据，按可见材料复核并将无法复核项标 `unknown`，不将 00 的日志记为 04 独立运行。
- 验收标准及实际可执行命令：静态核对 `writeSegments` partial iovec 推进、EINTR、零写、errno、MSG_NOSIGNAL、TLS/空 payload 回退与调用点仅 DATA；检查真实 64-byte FrameHeader 拼接断言和错误路径测试。复算 trace 计数（每组每次 4,096 帧、基线 8,192 个 DATA 写调用、after 4,096 个组合 sendmsg）、hash/退出码；评估 1/8 connections CPU/GiB 与 wall 中位数，明确小样本限制。可只读执行 `git -C <worktree> diff --check`，不得改文件。明确给出 PASS/PARTIAL/BLOCKED 及保留/回退建议；不要求额外重复构建，除非能在不改代码前提下复核已有固定快照。
- 云端运行目录、端口、资源上限、清理与归档方案：不适用；严禁云端工作。
- 预期产物路径：`docs/tasks/2026-09-23-lowlevel-sendv-qa-01-result.md`、`docs/tasks/2026-09-23-lowlevel-sendv-qa-01-last-message.md`。

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务和指定结果，独立查看 03 的 worktree diff。只写你的结果文件。不要因 syscall 下降就写成已证明 100 Mbps 提速；指出 8-connection wall 中位数变化、样本与 loopback 外推边界。不得修改代码、任务板、Git 状态或启动云端工作。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- transfer / integrity / evidence / wire accounting（适用时）：
- 失败/跳过/阻塞及其原因：
- 剩余风险、未完成事项：
- 下一角色可直接执行的下一步：

## 验收与路线变化

验收人、结论和依据：
