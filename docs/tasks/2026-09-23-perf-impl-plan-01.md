# PERF-IMPL-PLAN-01：单文件与目录优化实现拆解

- 状态：done（03 已追加方案，00 已验收）
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：00 总指挥
- 目标及理由：在用户要求尽早优化单文件与多文件目录传输的背景下，结合当前核心源码、03 首次接手源码映射、02 矩阵设计和 04 独立 QA blocker，提出可在 100 Mbps 阶段推进的最小实现任务切分；明确哪些候选有现存证据、哪些只是待测假设，避免盲目改协议或把目录与单文件混成一个优化项。
- 非目标：不改源码、测试、runner、路线决策或任务板；不构建、测试、profile、benchmark、SSH、清理云端；不调整默认值、协议、checksum/resume 或 IO backend；不把历史 dirty 跨云结果当作新基线；不批准任何性能代码直接合并或宣称达到目标。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。执行前实查；工作区包含 00 的 BOARD/任务单与 02/04/05 回执未提交改动，不覆盖或暂存。
- 必读资料和证据路径：本任务、`docs/coordination/BOARD.md`、`docs/coordination/receipts/03-implementation.md`、`docs/coordination/receipts/02-experiments.md` 的 PERF-MATRIX-01、`docs/coordination/receipts/04-quality.md` 的 PERF-QA-01、`docs/coordination/receipts/05-operations.md` 的 ENV-01、`docs/RESEARCH_BASELINE.md`、相关单文件/tree transfer client 与现有 phase metrics。只定向阅读，不重读整份历史材料。
- 允许修改的文件/目录；禁止修改的资源：仅追加 `docs/coordination/receipts/03-implementation.md` 的 `PERF-IMPL-PLAN-01 v1` 小节。禁止改任务单、BOARD/ROSTER、其他回执、决策、源码、测试、runner、原始证据或云端数据。
- 工作分支/worktree：只读方案任务，不建 worktree、不切分支、不操作共享 Git index。
- 前置条件、环境占用和停止条件：路线目标为 100 Mbps 两类传输；上海当前 0 可用，禁止任何云端动作。01 的联合性能路线仍因既有聊天 active writer 未交付；因此这里只能形成候选任务包，必须标注最终方案依赖 01 对 checksum/timer/auth 契约的冻结。若源码输入或路线版本变化，停止并报告，不改按旧 commit 做假设。
- 验收标准及实际可执行命令：给出单文件、目录各自的现存观测能力/缺口；至少列 2 个候选优化或配置扫描方向及证据等级、可能收益机制、风险、能证伪它的测量；区分 100 Mbps 先行目标与 100G 高速验证；将 QA blocker 分类到 02 runner/证据工具、03 核心客户端、01 契约、04 回归验证、05 环境；提出有先后依赖的最小任务切分和各自明确文件边界、测试入口。不得声称某候选已证实是瓶颈。执行 `git rev-parse HEAD`、`git status --short --branch`、定向 `rg`/源码读取和 `git diff --check`；不运行 CMake/CTest/实验。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用；禁止 SSH。ENV-01 记录上海 0 可用，当前批准清理量为 0 GiB。
- 预期产物路径：仅追加 `docs/coordination/receipts/03-implementation.md` 中的 PERF-IMPL-PLAN-01 v1。

## 派给角色的消息

请续接 ROSTER 中既有 03 聊天。核对本任务、BOARD、输入 HEAD 和 02/04/05 新回执后，只做源码方案拆解；输出单文件和目录分开的候选、证据等级、最小代码与测试边界、依赖及顺序。当前路线尚未由 01 冻结，上海空间也阻塞构建与实验，所以不得开始改代码或建立 worktree。只追加自己的回执并通知总指挥，不触碰其他共享文件。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 单文件候选与证据等级：
- 目录候选与证据等级：
- 依赖、停止条件与下一角色可执行任务：
- 未完成限制：

## 验收与路线变化

00 总指挥验收。若发现现路线契约与代码事实矛盾，报告并等待 01/00 更新路线；不自行改路线或实现。
