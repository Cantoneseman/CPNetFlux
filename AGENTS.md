# CPNetFlux 本地 AI 协作入口

本目录是 CPNetFlux 的长期开发工作区，绝对路径 `D:\Project\CPNetFlux`。协作结构为“00 总指挥 + 五个专职角色”，用户主要与总指挥沟通。

开始先读 `docs/coordination/START_HERE.md`、`PROJECT_BRIEF.md`、`ENVIRONMENT.md`、`BOARD.md`（均在同目录）和自己的角色 prompt/回执，再按任务读取 `README.md`、`docs/DESIGN.md`、`docs/ENGINEERING.md`、相关决策和源码。不要以全量旧聊天或巨大历史文档替代当前任务单。

规则：

- `GridFTP` 是外部协议/对照工具名称，`CPNetFlux` 是项目品牌；新增代码、目标名、命令和文档统一使用 CPNetFlux/cpnetflux。
- 使用 GridFTP 做对照实验前，必须在深圳实验端检查 GSI proxy 的有效时间；无有效 proxy 或剩余时间低于任务单要求时，实验脚本必须拒绝启动。需要刷新时，只能在交互式 SSH 终端调用 `grid-proxy-init`，由操作者输入密码；密码、私钥和 proxy 内容不得写入脚本、环境文件、任务单、日志或 Git。GSI 刷新完成后仍需记录 proxy 剩余时间、认证模式和服务端端口，不记录凭据值。
- 不把历史实验的 go 结论当作生产 readiness；先查 `docs/RESEARCH_BASELINE.md` 的边界。
- 修改前先写任务范围、非目标、验收标准和受影响文件。
- 按 `docs/DECISIONS/2026-09-24-cloud-first-development-and-github-backup.md` 执行云端优先：迁移验收后，深圳是 CPNetFlux 唯一权威 Git 工作区，项目源码、文档、Git、构建、测试、分析和实验都在其中或隔离 worktree 完成；上海只作传输对端，Windows 用于连接/查看/应急回收，不作日常开发副本。迁移完成前不得宣称已切换；新源码实现/实验先暂停在本地，迁移准备只能按 `CLOUD-GITHUB-01` 和上述决策进行。
- 每项可独立审查的工作阶段完成后，在 `codex/<task-id>` 分支提交明确文件并推送到已核实的 CPNetFlux GitHub remote；记录 commit SHA、push 状态及远端 SHA 回读。禁止混入其他 dirty 文件、凭据、构建产物或大 payload。Push 失败就明确报告备份未完成，不得把本地 commit 或云端磁盘副本称作 GitHub 备份。
- 不把 `tools/perf/results` 的大 payload 恢复到工作区；历史证据位于 `D:\Project\GridFlux Beta\_server_backups`。
- 发现当前路线与新需求冲突时，先更新架构决策和路线图，标记旧任务失效，再开始实现。
- 完成前必须运行与改动相关的 CMake/CTest 或脚本门禁，并记录失败原因。
- 长期任务必须留下任务范围、输入提交、验收命令、输出证据和下一步交接记录；不要只依赖聊天上下文。
- 用户目标决定方向；代码、测试和实验决定事实。两者冲突时指出缺口并更新计划，不能用当前实现覆盖用户的新需求。
- 总指挥维护任务板；每个角色只写自己的接手回执。实现/测试修改使用独立 `codex/<task-id>` worktree，不在多人共享目录切分支或操作共享 index，不使用 `git add .` 混入他人改动。
- 文件更新不会自动唤醒其他聊天。没有跨聊天调用能力时明确记录待派发，不能冒称任务已执行。

每轮工具核验与派单状态：

- 每轮开始执行前，检查本轮实际暴露的工具；不得根据历史轮次推断本轮工具可用或不可用。
- 分别判断专用聊天派单入口、shell/终端、CLI 续接既有聊天三种能力。缺少其中一种，不代表另外两种也不可用。使用 `functions.exec` 时，先检查其本轮可用工具列表。
- 用户已授权派单，且本轮有 shell/终端时，先查 `docs/coordination/DISPATCH.md` 与 ROSTER 中的既有聊天，再按登记的方式尝试续接；不要仅因缺少专用派单工具就报告无法派单。跟踪同一进程/session 到终态，并读取、核验任务要求的回执或产物。
- 如实区分状态：未发现可用工具、工具可用但未调用、命令/轮次已启动（记录 session 或 turn 标识）、进程/轮次已结束、产物已读取核验。没有实际检查或调用，不得报告“不可用”“已派发”或“已完成”。
- 如果本轮确实没有所需工具，说明实际检查过的入口以及缺少的具体能力；不要把“没有专用派单接口”表述成“没有任何派单路径”。

云端参考：Windows SSH 别名 `gridflux-beta-shenzhen`、`gridflux-beta-shanghai`。历史项目目录仍是 `/root/projects/GridFlux-Beta`，两处均为 dirty 树，不覆盖。`/root/projects/CPSS(DCC)` 是用户另一实验，不得触碰。2026-09-17 上海磁盘已满；新实验前必须重新核实两端资源并解除阻塞。完整规则见 `docs/coordination/ENVIRONMENT.md`。
