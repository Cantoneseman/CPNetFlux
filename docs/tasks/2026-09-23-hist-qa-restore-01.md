# HIST-QA-RESTORE-01：恢复独立质量审查全文

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：04 测试与质量；验收角色：00 总指挥
- 目标及理由：HIST-QA-01 的结果文件被上一轮 CLI 最后摘要覆盖；恢复既有独立复算与风险审查全文，以供实现设计审查追溯。
- 非目标：不重新审计全量 216 行、不重算 payload、不连接服务器、不构建/测试/实验、不改源码、BOARD、ROSTER 或 02 结果。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；执行前核对，保留共享文档改动，不操作 Git index。
- 必读资料和证据路径：本任务、原 `docs/tasks/2026-09-23-hist-qa-01.md`、当前短摘要 `docs/tasks/2026-09-23-hist-qa-01-result.md`、HIST-FORENSIC-01 的源表索引和既有 04 复核上下文。
- 允许修改的文件/目录；禁止修改的资源：只允许恢复完整正文至 `docs/tasks/2026-09-23-hist-qa-01-result.md`；CLI 摘要写 `docs/tasks/2026-09-23-hist-qa-restore-01-last-message.md`。禁止改其它文件或资源。
- 工作分支/worktree：只恢复文档，不建 worktree、不切分支。
- 前置条件、环境占用和停止条件：使用既有 04 聊天，待 02/03 现有 writer 结束后派发；缺少证据的结论保持 PARTIAL，不编造完整复核。
- 验收标准及实际可执行命令：恢复原报告中范围/证据、58/14/38 计数复算、dense 数值表、配置/计时差异和候选边界、未运行项；严格 UTF-8、行尾空白、`git diff --check`、HEAD 不变、index 空。完整正文与 last-message 输出使用不同路径。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用，禁止远端操作。
- 预期产物路径：更新原 HIST-QA-01 result，新增独立 CLI last-message。

## 派给角色的消息

请续接 ROSTER 中既有 04 聊天。上一轮 CLI `--output-last-message` 与审查全文误用了同一路径，当前 result 仅有摘要。请从上一轮已有复核结果恢复完整正文到授权 result 文件；不要重做全量审计。CLI `-o` 必须写入 `docs/tasks/2026-09-23-hist-qa-restore-01-last-message.md`。确认完成后，再等待下一份独立设计审查任务。

## 执行回执

- 实际输入/输出 commit：
- 恢复章节与原始复核来源：
- 实际门禁与退出码：
- 无法恢复/仍为 PARTIAL 的事项：
- 后续任务状态：

## 验收与路线变化

00 总指挥验收；恢复文本不升级为新的 payload 或性能实验结论。
