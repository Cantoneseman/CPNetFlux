# CPNetFlux

CPNetFlux 是面向算力网的大规模可靠数据传输底座。它提供 GridFTP 风格控制接口，并在内部使用自研的分块、校验、断点恢复和目录编排引擎。

## 当前定位

这是从 2026-09-16 云端半成品提取的长期开发工作区。历史实验结果已保存在外部备份目录；本仓库只保留源码、测试、工具和必要文档。

## 技术基线

- C++20、CMake、Linux
- epoll TCP 多流传输基线
- POSIX 文件 IO，io_uring 为可选 file-IO-only 后端
- GridFTP 风格控制面与 CPNetFlux framed data channel
- manifest、chunk checksum、断点续传、目录级 file orchestration

## 开发入口

1. 从 [总指挥与五角色入口](docs/coordination/START_HERE.md) 接手，读取项目简报、环境和任务板。
2. 按角色/任务读取 `docs/DESIGN.md`、`docs/ENGINEERING.md`、最近有效的决策及实验原始证据；旧 ROADMAP 保留历史，不能当作自动执行清单。
3. 用户主要与总指挥沟通；总指挥派给架构、实验、实现、质量、运维五个独立角色。[六份完整初始 prompt](docs/coordination/START_HERE.md) 保存在仓库中。
4. 修改前固定任务范围、输入提交和验收。Windows 本地开发；Linux 测试使用经过资源检查的云端隔离构建，未运行的测试不能宣称通过。
5. 代码任务使用独立 `codex/<task-id>` 分支/worktree；协作规则见 `docs/AI_COLLABORATION.md`。

CPNetFlux 不等同于完整 GridFTP 实现；`GridFTP` 在文档中仅表示外部兼容协议和对照工具名称。
