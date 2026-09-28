已完成源码级调用链梳理，并将结果写入[目录性能源码调查回执](/D:/Project/CPNetFlux/docs/tasks/2026-09-23-dir-perf-source-01-result.md)。

结论是：dense 目录的历史 fp1/fp8 结果支持“低并行时 worker 供给不足”作为待测候选，但不证明具体瓶颈。源码确认 worker 会串行完成单文件控制交互、每流数据连接、传输完成等待和 manifest 更新后才取下一项；manifest 保存还在全局锁内重写完整文件。建议先做按 file/worker/stream 分阶段 profiling，再决定是否优化 manifest 持久化或其它路径。

写后 UTF-8、空白、`git diff --check`、HEAD 与空暂存区检查通过；HEAD 未变。所有构建、测试、smoke、profile、传输和云端操作均为 `NOT_RUN`。