# V3 Small-File Microbatch 01：per-file 固定开销剖析

日期：2026-11-08  
状态：scope ready；尚未开始实验或修改源码。

## 目的

threaded memory sink 候选因高方差 no-go 后，大文件三阶段和 sink 并发不再扩大。本 scope 转向独立的小文件任务完成时间问题，先测当前 V3 per-file 路径的固定开销，再决定是否实现 fixed microbundle 或 adaptive microbundle。该 scope 不承担大文件 100Gbps 目标。

## 输入与非目标

- 权威代码：R770-1 `/home/Su/projects/CPNetFlux`，当前固定提交 `4a361c2c9e75bd89cf49b19861c5599632c258ef`；R770-2 只用于构建和对端验收。
- 运行时统一使用 V3 执行器、V3 数据协议和 V3 工作租约；旧单文件流程不能作为运行时降级路径。
- 先不实现微批，不改变 wire format、manifest/checkpoint、checksum、原子发布、恢复或默认并发。
- 不修改 RDMA、三阶段 writer、socket buffer、内核/MTU/NUMA、生产服务或 CPSS(DCC)。
- 实验不使用 64 GiB payload；证据根目录为 `/home/Su/experiments/CPNetFlux/2026-11-08-v3-small-file-microbatch-01`，只保留日志、清单和小型统计。

## 第一阶段：当前 per-file 基线

- 使用 R770-1/R770-2 的 V3 目录传输入口，先固定单方向和一组显式 local bind；记录代码提交、二进制 SHA-256、配置和 route。
- 文件大小：4 KiB、16 KiB、64 KiB、256 KiB；文件数：1,000、10,000。若前两组 MemAvailable 和 inode 余量均安全，再追加 100,000×4 KiB 与 100,000×16 KiB；每个样本总逻辑字节不超过 2 GiB。
- 每格 5 次交错重复，首轮只测当前默认 per-file。源和目标在受控 tmpfs/实验根目录内，样本结束立即删除该样本的精确目录。
- 记录 `T_ready`、p50/p95、setup/data/commit 分段、文件成功数、树 manifest/SHA-256、双端 CPU/RSS、MemAvailable、进程 VmSwap、pswpout、路由和接口计数；不能把每文件线程累计时间相加成任务吞吐。
- 每次传输前后检查内存门：两端 MemAvailable ≥12 GiB；实验进程 owned swap=0；pswpout 增量=0；发现 inode、空间或内存不足立即停止并清理。

## 进入 fixed/adaptive 实现的门

只有当基线显示 open/FILE_BEGIN/FILE_END/manifest/commit 等固定成本占 `T_ready` 的主要部分，且 5 次重复的 p50/p95 可复现，才创建下一份实现 scope。下一 scope 必须先比较：

1. 当前 tuned per-file；
2. 保留成员索引、长度、checksum、原子发布和成员级恢复语义的 fixed microbundle；
3. 以文件大小、队列水位和服务率为输入的 adaptive microbundle。

候选目标为密集小文件 `T_ready` 中位至少下降 20%，p95 和恢复成本不恶化，大文件回退不超过 5%。若 adaptive 不优于 fixed，研究结论降级为工程优化；若固定开销不占主导，则停止微批实现并回到服务率/调度剖析。

## 验收门与清理

- 构建与 CTest 先通过；两端同提交、同配置、二进制 SHA-256 一致后才互传。
- 每个样本成员数量、相对路径、长度、checksum、最终树 SHA-256 和 V3 终态全部通过；失败样本必须保留原因，不改写为性能数据。
- 结果同时报告 `T_ready`、p50/p95、CPU/RSS、网络和清理证据；不报告为 100Gbps 或持久化带宽。
- 实验结束只删除本 scope 自有的源/目标临时目录、运行时日志和 payload；不删除 `/home/Su/datasets`、生产根目录或其他任务证据。

## 验收命令

```bash
cmake --build build -j2 --target cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux-gridftp-server cpnetflux_unit_tests
ctest --test-dir build --output-on-failure -R 'tree|directory|file_transfer_smoke'
sha256sum build/cpnetflux-tree-upload-client build/cpnetflux-tree-download-client build/cpnetflux-gridftp-server
```

若构建目标名称或目录入口与当前 CMake 不一致，先读取现有 CMake/CLI 帮助并在结果中记录实际命令；不通过兼容别名绕过 V3 门。
