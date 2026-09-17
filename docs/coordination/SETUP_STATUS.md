# 设置交接：尚缺 Codex 界面创建

2026-09-17，本轮已经完成：

- 六份完整角色 prompt，包含背景、当前代码/测试/实验边界、SSH、本地与云端规则和首次接手任务。
- 总指挥入口、项目简报、环境说明、任务板、派单模板、角色登记与回执规范。
- AGENTS、README、协作说明、旧五角色章程指向新入口。

还没有完成，不能冒称完成：

1. 在 Codex 添加 `D:\Project\CPNetFlux` 为独立项目。
2. 在该项目下创建六个长期聊天并实际提交对应 prompt。
3. 检查六个角色的接手回执、登记实际聊天标识、让总指挥汇总接手。

当前直接项目/聊天创建工具不可用。桌面工具可读到应用、现有旧项目和控件，但添加项目/新对话输入未产生可验证结果；重复无效点击没有价值。若后续工具恢复，继续上述三步；否则需用户通过界面添加项目并创建聊天。不要用改写 Codex 内部数据库或伪造会话文件替代正常创建流程。

## 可直接粘贴的六条启动消息

先在 Codex 选择新项目 `D:\Project\CPNetFlux`。每条放进一个独立的新聊天，建议标题与下列角色一致。对应文件内是完整 prompt，角色读完即能接手。

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
