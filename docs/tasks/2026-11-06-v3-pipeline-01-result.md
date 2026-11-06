# V3 三阶段流水线 01 结果

日期：2026-11-07。输入提交 `273ef42cb7bc4a9c126cad4357645254a87a0cfa`；实现提交
`50efc0eaf0ac3d2c4bc470d69e93de4926c9fbde`。GitHub 分支
`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01` 已推送并回读同一 SHA。
本轮按单人串行规则在 R770-1 开发，R770-2 只同步固定提交、构建和验收；RDMA 资源池、CQ、RQ/SQ、QP 和系统网络均未修改。

## 实现

接收端从原来的“读帧、校验、写文件”串行路径改为有界的两段流水线：前段在连接线程中完成帧头解析、CRC/压缩校验和 V3 session 状态推进，后段由单独 writer 线程执行 whole-file 或 range 写入、工作租约计数和写入观察。中间使用现有 `BoundedPayloadPipeline`，预算为 `kMaxPayload × CPNETFLUX_PAYLOAD_PIPELINE_DEPTH`，深度仍只允许 2/4/8/16 等有界配置。

未压缩帧通过 `PreparedPayload.logicalOffset/logicalBytes` 保留 CRC 前缀和有效 payload 的逻辑视图，避免为了跨线程而复制整帧；压缩帧使用独立的解码 bounded vector。FILE_END 会先关闭并排空队列，再进入 state.end、commit 和 result；读失败、校验失败和协议失败也采用排空语义，只有 writer 自身的致命错误才取消队列，避免丢失已经被 session 接受的数据和恢复 checkpoint。新增 timing 字段记录阶段标识、队列水位/等待和 writer 线程 CPU。

## 构建、测试和固定版本

R770-1 与 R770-2 均从 `50efc0eaf0ac3d2c4bc470d69e93de4926c9fbde` 构建成功，版本、CMake 配置和以下八个二进制 SHA-256 一致：`cpnetflux-gridftp-server`、`cpnetflux-tree-download-client`、`cpnetflux-tree-upload-client`、`cpnetflux-rdma-socket-bench`、`cpnetflux_unit_tests`、`cpnetflux-storage-bench`、`cpnetflux-session-record-bench`、`cpnetflux_framed_socket_tests`。

新增逻辑视图 metadata 单元测试，并回归恢复/断连语义；R770-1 精确相关测试 19/19 通过，R770-2 固定版本抽查 3/3 通过。R770-1 串行 CTest 为 550/552 通过，2 项失败是未设置 `CPNETFLUX_TEST_TOKEN` 导致的外部认证日志 smoke（`cpnetflux_gridftp_control_token_smoke`、`cpnetflux_event_log_smoke`），不是本次代码失败；3 个原生 RDMA 专项测试按当前环境跳过。`git diff --check` 和四个目标构建通过。

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

## 资源和清理

六个正式样本的输出均在 SHA/大小/inode 检查后删除，R770-1/R770-2 的源文件也在最终 SHA 核对后删除；`acceptance/cleanup-final.json` 记录了删除前的 inode、大小和 SHA。两端最终实验 tmpfs 根均不存在。守护观测中最低 `MemAvailable` 约为 R770-1 23.57 GiB、R770-2 24.13 GiB，峰值实验 RSS 约 2.07 GiB，未观察到 owned swap 或主机 swap-out。

本轮没有修改 RDMA 资源和 CQ 生命周期，因此既有 CQ response/flush 增量、偶发 peer-mapping 拒绝以及生产稳定性门仍保持未解释；磁盘约 4 Gbps 的历史结果也没有被本轮 tmpfs 结果覆盖。该实验不代表持久化存储带宽、稳态线速或生产 readiness。

## 下一步

1. 在保持 bounded queue、checkpoint 和 atomic commit 语义的前提下，继续拆分 writer 内部的 write/flush/commit 子阶段，先验证批量 write 与单 writer 的 CPU/锁等待是否是主路径成本。
2. 把 data connect、payload processing 和 FILE_RESULT/DirectoryEnd 分段做固定样本，避免用建连成本掩盖流水线变化。
3. 只在单 lane 的 writer 阶段出现可重复收益后，再比较深度 4/8、有限 writer 并发和多 lane；每步继续使用 1 GiB tmpfs、RSS/交换分区守护和完整清理。
4. RDMA CQ/RQ/SQ/QP 和 peer-mapping 继续独立调查，不把本轮流水线结果解释为 RDMA 优化或 100 Gbps 证明。