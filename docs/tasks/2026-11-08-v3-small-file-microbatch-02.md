# V3 Small-File Microbatch 02：fixed microbundle 实现与对照

日期：2026-11-08
状态：scope ready；尚未修改源码或运行候选 A/B。

## 目的

在 01 的 per-file 基线已确认固定开销主导后，先实现一个可审计、默认关闭的 fixed microbundle 候选，减少连续小文件对树清单状态持久化和控制回调的频率，再与当前 tuned per-file 路径交错对照。该 scope 不扩大三阶段 writer/worker 架构，不涉及 RDMA verbs，也不承担大文件 100Gbps 目标。

## 输入、非目标和约束

- 权威代码为 R770-1 /home/Su/projects/CPNetFlux；R770-2 只在固定提交后构建验收。
- V3 runtime、V3 数据协议和 V3 工作租约保持不变；不引入 V3 以前的运行时降级。
- 第一候选优先采用清单/状态事件的固定批量提交：成员 FILE_BEGIN/FILE_END、成员索引、长度、CRC32C、原子发布和成员级恢复语义必须保留；不能用一个未经索引的拼接文件替换成员语义。
- 不先实现 adaptive microbundle；不同时改变 file parallelism、连接数、chunk/buffer、socket、RDMA、writer 并发或 commit-sync。
- 采用固定 batch size 与最大延迟双重上限，默认行为和默认延迟不变；候选参数必须进入 JSON summary，便于复现实验和恢复审计。
- 若实现需要 wire 扩展，先单独写 protocol/compatibility 设计并通过旧端拒绝/协商测试；本 scope 默认优先选择无需 wire 扩展的 manifest/event batching。

## 实现步骤

1. 为 manifest/event batching 增加显式内部配置（默认值保持现状），限制 batch size、最大延迟和未持久化成员数；错误、取消、连接断开、任务结束和恢复前强制 flush。
2. 增加批次计数、最大批次大小、flush 原因、待持久化成员峰值和成员 checksum/发布状态统计；不把累计线程时间当吞吐。
3. 增加单元测试：批次阈值、时间阈值、finish barrier、保存失败、取消/恢复、成员顺序与重复事件。
4. 固定提交同步到 R770-2，构建、CTest、二进制 SHA-256 一致后再实验。
5. 先做 4 KiB/16 KiB × 10,000/100,000，单连接、内存/受控 tmpfs、每格至少 5 次交错；总逻辑字节每样本 ≤2 GiB。每样本保留完整性、T_ready、p50/p95、manifest flush、CPU/RSS、MemAvailable、owned VmSwap、双端 pswpout、route、接口计数和清理证据。
6. fixed candidate 必须至少达到密集小文件 T_ready 中位下降 20%，且 p95 不恶化超过 10%、成员级恢复和完整性通过、候选默认关闭时旧路径无回归；否则 no-go。
7. 只有 fixed 候选通过后，才设计 adaptive 候选。adaptive 只增加文件大小、队列水位和服务率输入，先在同一矩阵比较；若不优于 fixed，研究结论降级为工程优化。

## 验收命令

cmake --build build -j2 --target cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux-gridftp-server cpnetflux_unit_tests
ctest --test-dir build --output-on-failure -R 'tree|directory|file_transfer_smoke'
sha256sum build/cpnetflux-tree-upload-client build/cpnetflux-tree-download-client build/cpnetflux-gridftp-server

实验结束只删除本 scope 自有的 source/server-root、日志、事件、计时和 compact 结果以外的 payload；不触碰生产根目录、/home/Su/datasets、CPSS(DCC) 或其他任务证据。
