# CPNetFlux 本地 AI 协作入口

本目录是 CPNetFlux 的长期开发工作区。开始工作前依次阅读：

1. `README.md`
2. `docs/RESEARCH_BASELINE.md`
3. `docs/DESIGN.md`
4. `docs/ROADMAP.md`
5. `docs/ENGINEERING.md`
6. `docs/AI_COLLABORATION.md`

规则：

- `GridFTP` 是外部协议/对照工具名称，`CPNetFlux` 是项目品牌；新增代码、目标名、命令和文档统一使用 CPNetFlux/cpnetflux。
- 不把历史实验的 go 结论当作生产 readiness；先查 `docs/RESEARCH_BASELINE.md` 的边界。
- 修改前先写任务范围、非目标、验收标准和受影响文件。
- 不直接在云端正式目录开发；云端只运行已提交 commit 的构建、测试和受控实验。
- 不把 `tools/perf/results` 的大 payload 恢复到工作区；历史证据位于 `D:\Project\GridFlux Beta\_server_backups`。
- 发现当前路线与新需求冲突时，先更新架构决策和路线图，标记旧任务失效，再开始实现。
- 完成前必须运行与改动相关的 CMake/CTest 或脚本门禁，并记录失败原因。
- 长期任务必须留下任务范围、输入提交、验收命令、输出证据和下一步交接记录；不要只依赖聊天上下文。

云端参考：SSH 别名 `gridflux-beta-shenzhen`，正式项目仍是 `/root/projects/GridFlux-Beta`；没有明确任务前不要修改它。
