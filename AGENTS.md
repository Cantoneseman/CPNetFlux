# CPNetFlux 本地 AI 协作入口

本目录是 CPNetFlux 的长期开发工作区，绝对路径 `D:\Project\CPNetFlux`。协作结构为“00 总指挥 + 五个专职角色”，用户主要与总指挥沟通。

开始先读 `docs/coordination/START_HERE.md`、`PROJECT_BRIEF.md`、`ENVIRONMENT.md`、`BOARD.md`（均在同目录）和自己的角色 prompt/回执，再按任务读取 `README.md`、`docs/DESIGN.md`、`docs/ENGINEERING.md`、相关决策和源码。不要以全量旧聊天或巨大历史文档替代当前任务单。

规则：

- `GridFTP` 是外部协议/对照工具名称，`CPNetFlux` 是项目品牌；新增代码、目标名、命令和文档统一使用 CPNetFlux/cpnetflux。
- 不把历史实验的 go 结论当作生产 readiness；先查 `docs/RESEARCH_BASELINE.md` 的边界。
- 修改前先写任务范围、非目标、验收标准和受影响文件。
- 不直接在云端正式目录开发；云端只运行已提交 commit 的构建、测试和受控实验。
- 不把 `tools/perf/results` 的大 payload 恢复到工作区；历史证据位于 `D:\Project\GridFlux Beta\_server_backups`。
- 发现当前路线与新需求冲突时，先更新架构决策和路线图，标记旧任务失效，再开始实现。
- 完成前必须运行与改动相关的 CMake/CTest 或脚本门禁，并记录失败原因。
- 长期任务必须留下任务范围、输入提交、验收命令、输出证据和下一步交接记录；不要只依赖聊天上下文。
- 用户目标决定方向；代码、测试和实验决定事实。两者冲突时指出缺口并更新计划，不能用当前实现覆盖用户的新需求。
- 总指挥维护任务板；每个角色只写自己的接手回执。实现/测试修改使用独立 `codex/<task-id>` worktree，不在多人共享目录切分支或操作共享 index，不使用 `git add .` 混入他人改动。
- 文件更新不会自动唤醒其他聊天。没有跨聊天调用能力时明确记录待派发，不能冒称任务已执行。

云端参考：Windows SSH 别名 `gridflux-beta-shenzhen`、`gridflux-beta-shanghai`。历史项目目录仍是 `/root/projects/GridFlux-Beta`，两处均为 dirty 树，不覆盖。`/root/projects/CPSS(DCC)` 是用户另一实验，不得触碰。2026-09-17 上海磁盘已满；新实验前必须重新核实两端资源并解除阻塞。完整规则见 `docs/coordination/ENVIRONMENT.md`。
