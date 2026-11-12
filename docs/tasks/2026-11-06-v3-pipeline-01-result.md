# V3 三阶段流水线 01 结果

日期：2026-11-07。输入提交 `273ef42cb7bc4a9c126cad4357645254a87a0cfa`；实现提交
`50efc0eaf0ac3d2c4bc470d69e93de4926c9fbde`。GitHub 分支
`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01` 已推送并回读同一 SHA。
清理提交 `2f7e5cbf1dbf8616d2987d24c58114a380ced93a` 只删除一个未使用的停止 lambda；重复实验数据仍对应 `50efc0e`，不改写为在后续提交上重测。
本轮按单人串行规则在 R770-1 开发，R770-2 只同步固定提交、构建和验收；RDMA 资源池、CQ、RQ/SQ、QP 和系统网络均未修改。

## 实现

接收端从原来的“读帧、校验、写文件”串行路径改为有界的两段流水线：前段在连接线程中完成帧头解析、CRC/压缩校验和 V3 session 状态推进，后段由单独 writer 线程执行 whole-file 或 range 写入、工作租约计数和写入观察。中间使用现有 `BoundedPayloadPipeline`，预算为 `kMaxPayload × CPNETFLUX_PAYLOAD_PIPELINE_DEPTH`，深度仍只允许 2/4/8/16 有界配置。该数字是排队 payload 预算，不含生产中、消费中和空闲回收缓存；这些缓冲也由既有 pipeline 容量和槽数限制，不能把队列预算等同于总 RSS。当前每个活动文件一个 writer，尚未提供独立可调的读/网络/写 worker 池。

未压缩帧通过 `PreparedPayload.logicalOffset/logicalBytes` 保留 CRC 前缀和有效 payload 的逻辑视图，避免为了跨线程而复制整帧；压缩帧使用独立的解码 bounded vector。FILE_END 会先关闭并排空队列，再进入 state.end、commit 和 result；读失败、校验失败和协议失败也采用排空语义，只有 writer 自身的致命错误才取消队列，避免丢失已经被 session 接受的数据和恢复 checkpoint。新增 timing 字段记录阶段标识、队列水位/等待和 writer 线程 CPU。

## 构建、测试和固定版本

互传使用的 server/download/upload 在 R770-1 与 R770-2 均从 `50efc0eaf0ac3d2c4bc470d69e93de4926c9fbde` 构建成功；初次版本/配置及八个二进制摘要一致，但独立 framed-socket 测试目标当时未重新链接，不能把这一步当成全量构建验收。检查的八个目标为：`cpnetflux-gridftp-server`、`cpnetflux-tree-download-client`、`cpnetflux-tree-upload-client`、`cpnetflux-rdma-socket-bench`、`cpnetflux_unit_tests`、`cpnetflux-storage-bench`、`cpnetflux-session-record-bench`、`cpnetflux_framed_socket_tests`。

新增逻辑视图 metadata 单元测试，并回归恢复/断连语义；R770-1 精确相关测试 19/19 通过，R770-2 固定版本抽查 3/3 通过。R770-1 初次串行 CTest 为 547 通过、2 失败、3 跳过（共 552 项），2 项失败是未设置 `CPNETFLUX_TEST_TOKEN` 导致的外部认证日志 smoke（`cpnetflux_gridftp_control_token_smoke`、`cpnetflux_event_log_smoke`），失败原因是测试环境缺失，不能计作通过；3 个原生 RDMA 专项测试因缺少显式测试 bind 跳过。`git diff --check` 和四个目标构建通过。

最后门禁使用实现清理及测试提交 `efbc3e5f7e19885040f66510cf27fc9f425ba98b`，执行全量 `cmake --build build -j2` 后再跑 CTest。独立 `cpnetflux_framed_socket_tests` 重新链接揭示两条旧断言仍假定接收单帧；第二次全量为 550 通过、2 条分配断言失败、无跳过，失败证据保留在 `acceptance/final-ctest.log`。修正测试为深度 2/8 的真实满帧分配上限（queue+producer+consumer，另计已验证 chunk 的复读缓冲），并保留短帧容量预算、wire/CRC/压缩/output oracle；两条测试连续 30 轮、60 次通过。最终临时生成测试 token（不记录值）并显式设置 `CPNETFLUX_TEST_RDMA_BIND=192.168.100.1` 后，R770-1 串行 **552/552 通过、无跳过**，耗时 253.93 秒。完整命令、退出码与日志保留在 `acceptance/verified-gates.json` 和 `verified-ctest.log`。最终测试提交没有修改热路径行为，不把这些回归改写为新的互传样本。

R770-2 同提交全量构建成功，相关 CTest **134/134 通过、无跳过**（65.87 秒）；最终八个二进制、配置、版本一致，见 `acceptance/matching-builds-final-code.json`、`R770-2-related-gates.json` 和 `R770-2-related-ctest.log`。

## 内存实验

证据根目录：`/home/Su/experiments/CPNetFlux/2026-11-07-v3-pipeline-02`。R770-2 → R770-1，显式使用 RDMA 网卡地址 `192.168.100.2` / `192.168.100.1` 和 `eno17095np0`，实际 `data_transport=rdma`；控制面仍为 TCP。源和目标均在 tmpfs，1 GiB、单 lane、CRC32C、1 MiB chunk、`verified_chunks`、direct write、`commit_sync=none`，源 SHA-256 为
`85025c12a1ff8e8952bd1727a842c369fe10fc2a97938f3907c68ae189c9d403`。

每个配置三次，均通过协议终态、原生 QP、CRC、独立 SHA、源驻留和内存守护；正式样本为深度 2 对照和深度 8 候选。

| 指标（三次中位数） | 深度 2 对照 | 深度 8 候选 |
|---|---:|---:|
| 完整 task wall，秒 | 1.566078 | 1.566170 |
| 完整 task rate，Gbps | 5.484997 | 5.484676 |
| 接收文件阶段 wall，秒 | 0.394434 | 0.391830 |
| 接收文件阶段 rate，Gbps | 21.777862 | 21.922598 |
| data connect，秒 | 1.063600 | 1.053100 |
| 接收 pipeline 预算，字节 | 131072 | 524288 |
| 接收高水位，字节 | 131072 | 524288 |
| producer waits / 秒 | 12274 / 0.129048 | 11970 / 0.128923 |
| consumer waits / 秒 | 12 / 0.001376 | 1 / 0.000829 |
| receiver writer CPU，秒 | 0.391983 | 0.390361 |
| reader lane CPU，秒 | 0.293876 | 0.292410 |
| CRC 阶段，秒 | 0.070213 | 0.068998 |
| write 阶段，秒 | 0.347288 | 0.346238 |
| 实际 writeAtAll 次数 | 17408 | 17408 |

深度 8 将消费者等待从 12 次降到 1 次，并把可吸收的写入 backlog 从 128 KiB 提高到 512 KiB；但完整 task rate 变化约 -0.006%，文件阶段 rate 变化约 +0.665%，在三次小样本和约 1 秒建连成本下不能作为端到端提速结论。写入阶段仍约 0.345 秒/GiB，且 writer CPU 与 reader lane CPU 相加约 0.68 秒；当前改造证明了阶段解耦和有界反压语义，尚未证明整体 CPU 或 100 Gbps 收益。

## 改造前后和 CPU 代价

独立基线证据位于 `/home/Su/experiments/CPNetFlux/2026-11-06-v3-pipeline-01`，输入为 `273ef42`，深度 2/8 各三次；当时仅发送端有队列，接收端仍为同步处理。相同 1 GiB、单 lane 和校验配置下，改造前后深度 2 的中位数如下。两组顺序执行，源随机块不同，未随机交错，不能作为普适因果或稳态上限证明。

| 深度 2，中位数 | 同步基线 | 接收 writer 解耦 |
|---|---:|---:|
| 文件阶段 rate，Gbps | 19.366808 | 21.777862 |
| 完整 task rate，Gbps | 5.315171 | 5.484997 |
| 接收线程 CPU 合计，秒/GiB | 0.442168 | 0.685859 |
| 全部 payload 线程 CPU 合计，秒/GiB | 0.778573 | 1.029795 |
| CLI user+sys，秒 | 1.04 | 1.26 |
| writeAtAll 累计 wall，秒 | 0.244892 | 0.344820 |

文件阶段约 +12.45%，task 约 +3.20%，但外层 Python wait 约 50 ms 的轮询粒度影响小任务分辨率；GNU elapsed 中位数为 1.58/1.53 秒。接收线程 CPU 增加约 55.11%，所有 payload 线程 CPU 增加约 32.27%，且实际写入累计 wall 变长。不能只报告 reader CPU 下降；本轮整体性能验收尚未通过。需要剖析跨线程交接、调度和写入成本，不能据此提高默认深度或承诺 100 Gbps。

调试中有 4 次 harness 断言失败，均在应用摘要成功后因旧脚本假设“接收帧只分配一次”或修改脚本时的 sender 预算断言失败；这些样本未走完独立 SHA 验收，全部排除并保留失败 evidence，不算性能成功。另有 1 次深度 8 完整通过的诊断样本未计入三次正式组。短 DATA 帧的容量增长也会计数（诊断中观察到额外 68 字节），所以累计增长次数不等于同时保留的满帧缓冲个数；不据此断言内存泄漏或只用一个缓冲。验收脚本已按有界容量和完整性重新约束，旧证据未覆盖。详情见 `acceptance/excluded-diagnostics.json`、`before-after.json` 和每次 cleanup 收据。

## 资源和清理

六个正式样本的输出均在 SHA/大小/inode 检查后删除，R770-1/R770-2 的源文件也在最终 SHA 核对后删除；`acceptance/cleanup-final.json` 记录了删除前的 inode、大小和 SHA。两端本轮和前一组基线 tmpfs 根均已删除；基线清理收据为原证据根 `acceptance/cleanup-final-baseline.json`。实验期间前一组基线各保留 1 GiB，所以本轮 tmpfs 峰值为接收端约 3 GiB、发送端约 2 GiB，不把它们说成进程 RSS 或仅单个源的预算。守护观测中最低 `MemAvailable` 约为 R770-1 23.56 GiB、R770-2 24.13 GiB，峰值实验 RSS 约 2.07 GiB，未观察到 owned swap 或主机 swap-out。

六个正式样本的 RNR/out_of_buffer 为零，但 R770-1 CQ response error/flush 仍为 257/256 或 321/320，R770-2 为 129/128（原始差分见 `acceptance/link-error-deltas.json`）。本轮没有修改 RDMA 资源和 CQ 生命周期，CQ 增量、既有偶发 peer-mapping 拒绝的根因以及生产稳定性门仍未关闭；磁盘约 4 Gbps 的历史结果也没有被本轮 tmpfs 结果覆盖。该实验不代表持久化存储带宽、稳态线速或生产 readiness。

## 下一步

1. 在保持 bounded queue、checkpoint 和 atomic commit 语义的前提下，先拆分队列交接、线程调度与 writer 内部 write/flush/checkpoint/commit 的 CPU、缺页和锁等待。已有同步写合并无净收益，不重复堆叠缓冲。
2. 把 data connect、payload processing 和 FILE_RESULT/DirectoryEnd 分段做固定样本，避免用建连成本掩盖流水线变化。
3. 只在单 lane 的 writer 阶段出现可重复收益后，再比较深度 4/8、有限 writer 并发和多 lane；每步继续使用 1 GiB tmpfs、RSS/交换分区守护和完整清理。
4. RDMA CQ/RQ/SQ/QP 和 peer-mapping 继续独立调查，不把本轮流水线结果解释为 RDMA 优化或 100 Gbps 证明。
