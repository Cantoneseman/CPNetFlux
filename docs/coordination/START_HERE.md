# CPNetFlux：总指挥与五个专职角色

本目录是新 Codex 项目的交接入口。项目文件夹固定为 `D:\Project\CPNetFlux`，不要把历史备份目录 `D:\Project\GridFlux Beta` 添加为新开发项目。

**项目操作遵循云端优先原则。** 迁移验收后，深圳 CPNetFlux clone 是唯一权威工作区，上海只作跨域传输对端；Windows 仅用于连接、查看和应急证据回收。迁移和 GitHub remote 尚未核验，不能声称已经切换。新会话与续接中的旧聊天都必须遵守 [云端优先决策](../DECISIONS/2026-09-24-cloud-first-development-and-github-backup.md)；迁移任务 `CLOUD-GITHUB-01` 完成前暂停源码实现、构建和性能实验。

用户日常只与 **00 总指挥** 沟通。总指挥把需求转成任务，交给五个专职角色，再汇总证据、风险和下一步。五个专职角色保留独立聊天，避免把全部历史塞进一个上下文。

## 六个聊天

| 建议标题 | 初始完整 prompt | 职责 |
|---|---|---|
| CPNetFlux｜00 总指挥 | [00-commander.md](prompts/00-commander.md) | 与用户沟通、排优先级、派单、收口、管理路线变更 |
| CPNetFlux｜01 架构与需求 | [01-architecture.md](prompts/01-architecture.md) | 问题定义、设计、测量契约、决策 |
| CPNetFlux｜02 实验与证据 | [02-experiments.md](prompts/02-experiments.md) | 实验设计、原始证据、统计、结论边界 |
| CPNetFlux｜03 核心实现 | [03-implementation.md](prompts/03-implementation.md) | 根据已确认任务实现、提交、自测 |
| CPNetFlux｜04 测试与质量 | [04-quality.md](prompts/04-quality.md) | 独立审查、正确性与回归验收 |
| CPNetFlux｜05 云端运维与发布 | [05-operations.md](prompts/05-operations.md) | SSH、隔离构建、资源、证据回收、发布准备 |

每个 prompt 可完整粘贴为相应新聊天的第一条消息。也可以在已经选定本项目的新聊天中输入：

> 请读取并执行 `D:\Project\CPNetFlux\docs\coordination\prompts\00-commander.md`，按其中的“首次接手”要求完成初始化。

其他角色替换文件名。不要把一份 prompt 分别发给多个同名聊天。实际聊天登记见 [ROSTER.md](ROSTER.md)；未有界面或工具证据时不能声称已创建。

## 每次接手只先读这四份

1. 根目录 `AGENTS.md` 和自己的 prompt。
2. [PROJECT_BRIEF.md](PROJECT_BRIEF.md)：背景、证据边界、当前代码状态。
3. [ENVIRONMENT.md](ENVIRONMENT.md)：本地/云端路径与 SSH、实验约束。
4. [BOARD.md](BOARD.md)：当前路线、任务状态、依赖和下一步。

每次项目写操作还须先读 [云端优先决策](../DECISIONS/2026-09-24-cloud-first-development-and-github-backup.md)。它覆盖旧聊天、旧 prompt 和历史回执中“本地负责日常源码/Git、云端只做构建/实验”的旧流程表述；当前工作区未通过迁移验收前，不能假装已经云端开发或已备份到 GitHub。

然后按任务读取决策、源码和原始实验。不要一次读完 200 KB 的旧 PROJECT_STATE，或把旧 ROADMAP 后半部分当作当前任务。

## 总指挥如何指挥

- 每项工作先生成 [TASK_TEMPLATE.md](TASK_TEMPLATE.md) 所示任务单，由总指挥维护 BOARD。
- 本机可使用 CLI 直接续接已有长期角色，具体见 [DISPATCH.md](DISPATCH.md)；无需用户手工转述每个任务。执行前核实该角色没有正在运行的轮次。
- 若当前 Codex 会话提供向既有聊天发送任务的工具，总指挥按 ROSTER 的真实聊天标识派发。发送成功也不等于执行完成，须检查角色回执和证据。
- 若只有临时子代理能力，它们可完成有边界的子任务，但不等于这里六个长期聊天；不得悄悄把长期角色换成临时代理。
- 若没有跨聊天工具，总指挥把完整派单文字写到任务单，并明确告诉用户应发送到哪个角色。不要假装文件一写就能自动唤醒其他聊天。具备合适桌面工具时可按用户已有授权操作界面。
- 专职角色仅在被启动时读任务单、执行并写回自己的回执。共享文件提供状态，独立聊天不会仅因文件更新自动运行。
- 所有进度从“分配→执行→待验收→完成”逐步推进。只有总指挥在质量证据齐备后标记完成；阻塞不算失败，更不算完成。

## 更换聊天而不丢失路线

角色离开前更新自己的 `receipts/NN-role.md`：输入提交、做了什么、实际命令与结果、产物路径、未解决问题、下一步。新聊天读入口和这份回执，重新验证 HEAD、工作树和环境；不依赖旧聊天记忆。

路线改变时，总指挥先更新任务单版本和 BOARD，标记旧任务 `superseded`；通知相关角色停止依赖旧规格的新动作，保留已经产生的证据。长实验只由运维按任务归属和 PID 确认后停止，不随意杀服务。
