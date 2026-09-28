# LOWLEVEL-TREE-TELEMETRY-ARCH-REVIEW-02-RESTORE

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.7`，`v1`
- 发起人：00 总指挥；执行角色：01 架构与需求；验收角色：00 总指挥
- 目标及理由：恢复 ARCH-REVIEW-02 完整正文。原角色已完成静态核验并通过 HEAD/index/source/UTF-8/空白门禁，但 CLI `--output-last-message` 将指定结果路径替换为简短终态摘要；摘要可证两项主结论，缺少任务要求的完整向量和源码位置。原子会话已终态，可续接同一 01 长期聊天。
- 非目标：不重复扩展核查范围，不改源码/测试/runner/CMake/decision/BOARD/ROSTER，不构建、不测试、不实验、不 SSH、不清理。
- 输入 commit（完整 SHA）、工作树状态：固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。当前工作区其他文档有未提交改动；保护不覆盖。
- 必读资料和证据路径：ARCH-REVIEW-02 原始任务、ARCH-REVIEW-01 结果、PLAN-03 BLOCKED 结果、生命周期裁定、树 telemetry decision、源码映射 `src/core/io/tree_transfer_client.cpp`、`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_client.cpp`。
- 允许修改的文件/目录；禁止修改的资源：仅完整替换 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`；另将 CLI `--output-last-message` 指向 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arch-review-02-last-message.md`，两者绝不可同路径。不得操作源码、测试、共享 index 或其他文件。
- 工作分支/worktree：原工作区，不切分支、不建 worktree。
- 前置条件、环境占用和停止条件：当前 01 writer 已由原 CLI 终态释放；仅复用登记聊天 ID。若无法恢复完整证据则结果中写清 partial，不补造行号或命令。
- 验收标准及实际可执行命令：正式结果应是可独立审阅的 Markdown 正文，含源码证据边界、两种 skip 的状态向量、Completed 校验成功/失败路径、Changed manifest finalize 表达问题、no-range transfer/integrity/evidence 映射、coverage/eligibility、所有未运行项及本轮核验命令/结果；结尾只作执行摘要，不将其替代报告。CLI 输出文件另存。00 读回正式结果后验 UTF-8、末尾 LF、无尾随空白、HEAD/index/fixed source unchanged、`git diff --check`。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用，禁止云端工作。
- 预期产物路径：正式 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`；CLI 末消息另存 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arch-review-02-last-message.md`。

## 派给角色的消息

请续接既有 01 长期聊天。根据刚完成的 ARCH-REVIEW-02 核查恢复一份完整独立审阅结果，不能把上一轮 CLI 简短末消息当作结果正文。尤其保留可审计源码位置和两项未决 schema 向量。最终 CLI `--output-last-message` 由总控设置为 last-message 专用路径，因此你只负责写正式 result。不得扩展范围或修改其它文件；动态工作全部写 NOT_RUN。

## 执行回执

- 实际输入/输出 commit：待执行。
- 实际改动：待执行。
- 实际命令、退出码与证据文件：待执行。
- transfer / integrity / evidence / wire accounting：逐状态独立写清，不用 unknown 冒充 mismatch。
- 失败/跳过/阻塞及原因：待执行。
- 剩余风险、未完成事项：待执行。
- 下一角色：00 收口 schema 向量后派 03 PLAN-04，再由 04 QA-04 独立审查。

## 验收与路线变化

00 仅在正式结果正文已读回且与本任务验收项一致后验收。不得依据 CLI 摘要批准代码或实验。
