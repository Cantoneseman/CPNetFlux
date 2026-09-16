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

1. 阅读 `docs/DESIGN.md`、`docs/ROADMAP.md`、`docs/ENGINEERING.md`。
2. 阅读 `docs/RESEARCH_BASELINE.md`，了解现有实验真正证明和没有证明的内容。
3. 先运行本地构建和单元测试，再开始新的任务。
4. 每个任务使用独立分支，并在任务规格中写明验收标准。

CPNetFlux 不等同于完整 GridFTP 实现；`GridFTP` 在文档中仅表示外部兼容协议和对照工具名称。
