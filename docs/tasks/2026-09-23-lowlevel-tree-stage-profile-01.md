# LOWLEVEL-TREE-STAGE-PROFILE-01：目录关键路径阶段观测

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：02 实验与证据；验收角色：04 测试与质量，最终由 00 收口
- 目标及理由：在固定实现提交上测量目录 worker 的 EPSV/passive listener、TCP connect、STOR/RETR 150、SessionInit、首 payload、payload I/O、226/complete、manifest finalize 和 inter-file idle，判断一槽 lookahead 是否有可重叠的关键路径收益。
- 非目标：不修改源码、测试、runner、schema、默认并行度、manifest 语义；不实现 lookahead、不改协议、不 SSH、不访问云端、不运行 GridFTP 对照、不改旧证据、不提交 Git。
- 输入 commit：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；文档 HEAD 仅作审计记录；固定 commit 归档和 build/hash 必须记录。
- 必读资料：本任务、`docs/tasks/2026-09-23-lowlevel-single-profile-00-result.md`、`docs/tasks/2026-09-23-lowlevel-design-01-result.md`、`docs/tasks/2026-09-23-lowlevel-design-qa-01-result.md`、`docs/tasks/2026-09-23-dir-perf-source-01-result.md`、`docs/DECISIONS/2026-09-17-directory-data-plane-profiling.md`。
- 允许修改：只新增 `docs/tasks/2026-09-23-lowlevel-tree-stage-profile-01-result.md` 和 CLI 摘要 `...-last-message.md`。原始 trace、日志、payload、build 只放 WSL ext4 任务目录；禁止修改源码、角色回执、BOARD/ROSTER、Git index。
- 工作区：无代码 worktree；固定 commit archive + 独立 ext4 `source/build/results`。不要从 Windows `/mnt/d` build path 计时。

## 测量口径

1. 优先 scheduler off、显式 worker control reuse、checksum none、compression off、POSIX、fresh target；目录使用 dense 小矩阵（例如 32×1 MiB 和 128×1 MiB），upload/download 各至少 3 次。记录 file_parallelism 1 与 8；若只支持更小 case，明确偏差。
2. 同一进程单调时钟区间分别记录：控制/被动端点准备、TCP connect、STOR/RETR 150、SessionInit/ResumeResponse、first payload、payload_io、transfer complete/226、manifest finalize、下一文件 dequeue/start；不得跨主机相减，不把并发阶段 sum 冒充 wall。
3. 若现有程序没有这些边界事件，不得猜测或从日志字符串伪造精确阶段；报告 `PARTIAL`，给出已有事件能支持的最小区间和需要的 instrumentation 任务。
4. 传输、integrity、evidence、wire accounting 独立列出；每个目标 tree hash 与源 tree hash 必须保留。失败、未完成、缺事件、未适用不能填零。
5. 结论只回答是否存在 EPSV/TCP connect 可与前一文件 payload 重叠且减少 inter-file idle 的机制证据；不得据此宣称 100 Mbps 或 GridFTP 收益。

## 验收命令与停止条件

- 固定 commit archive SHA-256、CMake 配置、compiler/binary SHA、实际 case plan、端口/PID 归属和完整命令必须写入结果。
- 至少运行现有 tree upload/download、control reuse、resume、changed-file/edge smoke 中与观测配置相符的项；真实退出码逐项记录，不能以 skip 当 pass。
- WSL/依赖/空间、阶段事件或 tree hash 无法取得时停止对应 case，标记 `BLOCKED`/`PARTIAL`，不切换云端、不清理未知目录。
- 若证据显示 connect/endpoint 准备经常晚于前一文件完成，或 idle/wall 降低不超过重复噪声，则不派 lookahead 实现；若关键路径重叠明确，再由 00 单独派 `LOWLEVEL-TREE-PRECONNECT-IMPL-01`。

