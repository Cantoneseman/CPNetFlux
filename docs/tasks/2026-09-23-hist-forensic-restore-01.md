# HIST-FORENSIC-RESTORE-01：恢复完整历史取证报告

- 状态：in_progress
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：02 实验与证据；验收角色：00 总指挥（04 的独立复核结果单独保留）
- 目标及理由：HIST-FORENSIC-01 的结果文件当前只保留 CLI 最后摘要，无法审阅其要求的分层配对表和排除细节；用 02 上一轮已完成的分析与已有源证据恢复完整报告，保证后续架构选型引用可审计材料。
- 非目标：不重新执行整套历史分析、不读取/重算 payload、不 SSH、不跑实验、不改旧证据、源码或 04 复核结果；不改 BOARD/ROSTER 之外的角色文档。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；执行前重新核对。共享文档和设计任务均有未提交变动；不得操作 Git index。
- 必读资料和证据路径：本任务；原 `docs/tasks/2026-09-23-hist-forensic-01.md`；现有简短 `docs/tasks/2026-09-23-hist-forensic-01-result.md`；完整独立审查 `docs/tasks/2026-09-23-hist-qa-01-result.md`；必要时只读旧 CSV/JSONL 索引和既有 02 分析上下文。
- 允许修改的文件/目录；禁止修改的资源：只允许将完整正文恢复至 `docs/tasks/2026-09-23-hist-forensic-01-result.md`；`docs/tasks/2026-09-23-hist-forensic-restore-01-last-message.md` 只供 CLI 摘要。禁止改其它结果、共享文件、源码、测试、原始数据、云端或 index。
- 工作分支/worktree：仅文档恢复；不建 worktree、不切分支。
- 前置条件、环境占用和停止条件：续接既有 02 聊天；若上下文无法恢复某张表，明确列缺项，不编造。无需重新扫全量数据。
- 验收标准及实际可执行命令：result 文件恢复为完整分层分析，至少保留匹配键/58 与 38 计数口径、配置与流数排除、single/dense/mixed 可比边界、候选证据来源、时钟/安全差异、hash 方法限制和云端实验不准入条件；源表无证据支持的数字留空并明确标记。完整正文与 `--output-last-message` 不得指向同一路径。严格 UTF-8、尾随空白和 `git diff --check` 检查；HEAD 不变、index 空。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用，禁止云端操作。
- 预期产物路径：更新原 HIST-FORENSIC-01 result 文件；新增独立 last-message 文件。

## 派给角色的消息

请续接既有 02 聊天。上一轮 CLI `--output-last-message` 与完整结果误用了同一文件路径，现有结果被摘要覆盖。请从上一轮已有分析上下文恢复完整正文到授权的 `docs/tasks/2026-09-23-hist-forensic-01-result.md`，不要重做全量审计或猜测遗漏数值；无法恢复的细节标注。CLI `-o` 必须使用 `docs/tasks/2026-09-23-hist-forensic-restore-01-last-message.md`。本轮仅恢复证据文档，不做代码/云端工作。

## 执行回执

- 实际输入/输出 commit：
- 恢复的章节和来源：
- 实际命令、退出码与检查：
- 无法恢复/仍缺的内容：
- 下一角色可直接执行的下一步：

## 验收与路线变化

00 总指挥验收。不得把恢复报告视为重新核验原始 payload 或公平性能对照。
