> 2026-10-19：单文件 STOR/RETR 与目录都只使用 V3 事务内核。目录控制面只接受 XCPNETFLUX V3 能力协商，旧 V2 greeting 被拒绝；file-transactions/1 是事务格式编号，不会选择旧执行器。项目尚未正式部署，当前 V3 只读写自己的 manifest/checkpoint；内部数字 schema 不是 V1/V2 运行版本。checksum/resume/TLS/压缩/IO/提交策略保留。

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

1. 先读 `AGENTS.md`、[环境](docs/coordination/ENVIRONMENT.md)、[研究基线](docs/RESEARCH_BASELINE.md) 和 [路线图](docs/ROADMAP.md)。
2. 2026-10-30 起在实验室 R770-1 的 `/home/Su/projects/CPNetFlux` 直接开发；R770-2 为同步构建/传输对端。Windows 只用于连接、查看和回收。
3. 只由当前总指挥单人串行执行，不使用子代理或并行工作区，保留已有 dirty 改动，沿用当前开发分支。
4. 修改前固定范围、输入提交、非目标和验收；只提交明确相关文件并推送 GitHub，回读远端 SHA。
5. 两端互传前同步提交并完成构建和二进制校验；测试走 RDMA 网卡的 IP 路径，当前 TCP 数据面不等于原生 RDMA verbs。

迁移范围与核验见 [R770 迁移任务](docs/tasks/2026-10-30-r770-migration.md)。旧角色和云端工作流文档属于历史，不覆盖当前规则。

CPNetFlux 不等同于完整 GridFTP 实现；`GridFTP` 在文档中仅表示外部兼容协议和对照工具名称。
