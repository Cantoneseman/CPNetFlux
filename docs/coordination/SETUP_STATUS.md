# 六角色设置进度

2026-09-17，本轮已经完成：

- 六份完整角色 prompt，包含背景、当前代码/测试/实验边界、SSH、本地与云端规则和首次接手任务。
- 总指挥入口、项目简报、环境说明、任务板、派单模板、角色登记与回执规范。
- AGENTS、README、协作说明、旧五角色章程指向新入口。
- 已通过本机 Codex app-server 协议创建独立项目，根目录 `D:\Project\CPNetFlux`；六个长期聊天的真实 ID 与项目归属已逐一核验，见 ROSTER。
- 六个聊天均已提交完整初始 prompt，六份角色回执齐备。总指挥已汇总五份回执。01/00 的资料来源及文件工具限制已在回执明示。
- 原设置控制进程正常退出后，又通过新进程回读项目与六个会话：名称、projectId、cwd、非 ephemeral 状态和原始 prompt 全部符合；CLI 可续接原架构聊天。
- 已调用桌面导航打开正式总指挥入口，返回 navigated=true。额外验证草稿已归档，正式角色仍为六个。

尚未完成的功能验收：

总指挥 00→04 的最小派单检查仍未发出。00 最初读过本地文件，但后续轮次实际工具列表没有 shell/文件接口；普通 CLI 续接、临时启用 code_mode 和精简上下文验证草稿均未恢复它的工具。权限不足与工具缺失不同，调整访问设置不能替代工具实际存在。

恢复后在正式 00 聊天执行 `docs/tasks/2026-09-17-setup-dispatch-check.md`，通过既有 04 会话回收 `receipts/dispatch-check.md`，核验后才把 SETUP-01 标 done。暂时不要以书面职责和已有 CLI 命令冒充总指挥已自动派单成功。现有任务与资料可直接继续使用，不需用户重新说明项目。

此前界面操作阻塞已通过受支持的 CLI/API 创建流程解决，不需要用户再创建一套。未修改 Codex 私有数据库或伪造会话文件。首次接手过程中发现默认只读设置导致回执写入等待，已对限定的回执操作处理授权，并将新角色后续轮次与当前用户的完全访问设置对齐；工作范围仍受任务单约束。

## 重建或换聊天时的启动消息

日常使用 ROSTER 中已存在的六个聊天。只有确实需要更换角色聊天时，才在 Codex 选择 `D:\Project\CPNetFlux` 并用下面的对应消息启动；完成后由总指挥更新登记，保留旧回执。

### CPNetFlux｜00 总指挥

请读取并执行 `D:\Project\CPNetFlux\docs\coordination\prompts\00-commander.md`，按“首次接手”要求初始化。你是本项目总指挥，我今后主要与你沟通。不要重复创建已有角色。

### CPNetFlux｜01 架构与需求

请读取并执行 `D:\Project\CPNetFlux\docs\coordination\prompts\01-architecture.md`，按“首次接手”要求初始化，写自己的回执并等待总指挥任务单。

### CPNetFlux｜02 实验与证据

请读取并执行 `D:\Project\CPNetFlux\docs\coordination\prompts\02-experiments.md`，按“首次接手”要求初始化，写自己的回执并等待总指挥任务单。

### CPNetFlux｜03 核心实现

请读取并执行 `D:\Project\CPNetFlux\docs\coordination\prompts\03-implementation.md`，按“首次接手”要求初始化，写自己的回执并等待总指挥任务单。

### CPNetFlux｜04 测试与质量

请读取并执行 `D:\Project\CPNetFlux\docs\coordination\prompts\04-quality.md`，按“首次接手”要求初始化，写自己的回执并等待总指挥任务单。

### CPNetFlux｜05 云端运维与发布

请读取并执行 `D:\Project\CPNetFlux\docs\coordination\prompts\05-operations.md`，按“首次接手”要求初始化，写自己的回执并等待总指挥任务单。

创建完成后给总指挥：

请读取五个角色在 `docs/coordination/receipts` 中的回执，核实当前事实，更新 BOARD 和 ROSTER，并向我报告建议先派发的任务。没有收到的回执明确标缺失，不假设完成。先解决 ENV-01 与 SPEC-01 的前置问题，不自动开启新性能实验。
