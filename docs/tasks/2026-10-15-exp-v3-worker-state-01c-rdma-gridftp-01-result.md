# EXP-V3-WORKER-STATE-01C-RDMA-GRIDFTP：结果

日期：2026-10-15
状态：完成
输入提交：922401b49450b47366a026a8285bc72fb52067dd
实验分支：codex/EXP-V3-WORKER-STATE-01C-RDMA-GRIDFTP

## 环境与边界

- R730：192.168.100.1，ConnectX-5 RoCE，`rocep130s0/1 ACTIVE`，`enp130s0np0 100000Mb/s Full`，MTU 9000。
- R750：192.168.100.2，RoCE，`rocep177s0/1 ACTIVE`，`enp177s0np0 100000Mb/s Full`，MTU 9000。
- CPNetFlux 01C 临时服务：R750 `22622`；R730 `22623`。两端均用临时 CA、节点证书、token、TLS/data-TLS required；实验结束后已停止并清理临时服务、凭据和 payload。
- GridFTP GSI：复用 R750 既有 `nodeb.grid:22522`，服务未停止、未改配置。GSI proxy 预检通过，远端写入和哈希回读通过。
- 原生 `ib_write_bw/ib_read_bw` 的用户报告结果（单向约 59–60 Gbps、双向合计约 114 Gbps）只作为链路上限参考。01C 源码没有 `libibverbs/rdma_cm` 数据通道，本次 CPNetFlux 走 RoCE 网口承载的 TLS/TCP framed data path，不是 verbs 结果。

## 运行组合

CPNetFlux 每个样本显式使用：

`connections=4`、`file-parallelism=4`、1MiB chunk/buffer、CRC32C、compression=auto、control-reuse=worker、data-session-reuse=tree、file-scheduling=dynamic、data-pending-window=2、control-pipeline-depth=2、scheduler=off、TLS/data-TLS required、phase-timing=on。

所有 CPNetFlux 样本均回报：

- `actual_mode=persistent_tree`
- `actual_file_scheduling=dynamic`
- `control_pipeline_actual_mode=shared_persistent_dispatch`
- `control_reuse_mode=worker`
- `checksum_algorithm=crc32c`
- `result=pass`

GridFTP 使用现有 GSI、`-fast -dcpriv -cd -rp -r -p 8 -cc 8 -tcp-bs 1048576`，目录传输显式递归。

## 样本与有效性

3 类数据 × 2 方向 × 3 重复 × 2 系统 = 36/36 有效样本；任一样本失败即停止扩展矩阵的门禁通过。每个方向均完成内容大小/文件数校验：

- `single_256MiB`：1 文件，256 MiB。
- `tree_dense_128MiB`：128 文件，每个 1 MiB，总 128 MiB。
- `tree_mixed_256MiB`：148 文件，总 256 MiB。

## 吞吐中位数（Gbps，3 次重复）

| 工况 | 方向 | CPNetFlux V3 01C | GridFTP GSI | CPNetFlux / GridFTP |
|---|---:|---:|---:|---:|
| single 256MiB | upload | 2.684 | 2.065 | 1.30x |
| single 256MiB | download | 3.241 | 1.600 | 2.03x |
| dense 128×1MiB | upload | 2.690 | 0.543 | 4.96x |
| dense 128×1MiB | download | 3.454 | 0.532 | 6.50x |
| mixed 256MiB | upload | 3.445 | 0.809 | 4.26x |
| mixed 256MiB | download | 5.077 | 0.857 | 5.93x |

这些是本次固定短样本、文件系统和 TLS/GSI 配置下的观测值，不等同于 100Gbps 链路或跨域生产结论。CPNetFlux 仍明显低于原生 verbs 上限，主要因为本次路径包含 TCP framing、TLS、CRC32C、manifest/finalize 和本地文件 I/O；本实验没有把 native verbs 假设混入 CPNetFlux。

## 验证与证据

- 01C Release 构建：通过。
- 全量 CTest：299/299 通过。
- `git diff --check`：通过。
- 双向 CPNetFlux auth probe：TLS identity、token login、capabilities、authentication 均通过。
- GridFTP GSI 预检：service-name 校验使用 `nodeb.grid`，被动端口和远端哈希回读通过。
- 原始 36 行 JSONL 曾保存在 R730 `/tmp/cpnetflux-v3-worker-state-01c/results/raw.jsonl`，汇总完成后已按回滚策略清理临时实验目录；未提交 payload、私钥、token 或 proxy。

## 未完成项与后续

- 本任务没有新增 native verbs/rdma_cm 数据通道；如果要把 CPNetFlux 的数据面真正改成 verbs，需要另立协议/实现任务、单独门禁和可回滚设计。
- 本次只验证 01C 的 persistent worker/shared dispatch 组合，没有启用 01C 明确排除的 global scheduler。
- 后续若要扩大到 1GiB、长时间稳定性或双端同时发起，需要新的实验任务和资源复核。
