# DIR-V2-MULTICHANNEL-01 结果

- 任务：有界持久目录多通道；输入提交 `414646cfc5812867ca6382ba8773dde6ef9c81f6`。
- 实现提交：`ea0dda9fdc90686487999a53757e08390ab48d99`；分支 `codex/DIR-V2-TRANSFER-ENGINE-01`；已推送 `origin`，`ls-remote` 回读与提交一致。
- 权威环境：深圳 `/root/projects/CPNetFlux`；其余预存 dirty 文件未纳入提交。

## 实现

- `--data-session-reuse tree` 下以 `--file-parallelism N` 建立 N 组独立控制 TCP 和数据 TCP；N 上限 8。每条数据通道按稳定 `fileId` 分片，通道内维持有界 pending window（1..16）。
- 新增 V2 channel negotiation 与 `XDIRP` 分片命令；服务器逐通道校验总文件数、分片文件数、file ID 和通道归属。
- 首个 payload 前 capability 不匹配才回退旧路径；开始事务后不重放。首错取消会 shutdown 同批控制/数据连接；服务端等待数据连接时监听控制连接断开，释放 passive listener。
- 增加 `control_prepare_seconds`、`transfer_complete_wait_seconds`；保留旧单通道 V2 与 legacy 路径。resume、checksum、compression、scheduler 和 data TLS 不在此优化的支持范围。

## 验收

- 源码归档：从实现提交生成，`/tmp/cpnetflux-runs/DIR-V2-MULTICHANNEL-01/committed/source.tar.gz`，SHA-256 `fd117fcb8cde3ff7bd60b8c08ed839f3e3a35ed4e3a88b3a156c45712bae9072`。
- 配置：`cmake -S <source> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON`；`cmake --build <build> --parallel 2`，构建成功。
- 定向单测：`PersistentSessionTest.*` 与 `TreeTransferOptionsTest.*`，27/27 通过。覆盖 1/2/4/8 分片唯一归属、错通道拒绝、channel 上限及现有 checkpoint 行为。
- loopback：`tools/test/run_cpnetflux_tree_data_reuse_smoke.py` 通过；包括旧单通道、能力拒绝回退、断开取消后 socket 回收、并行碰撞不覆盖已有文件，以及 128×1MiB 双向四通道 SHA-256 树比较。
- 本机 loopback 单次：上传 128×1MiB，4 控制 + 4 数据连接，0.116665 s、9.20 Gbps；下载 4+4 连接，0.228597 s、4.70 Gbps。两向文件数和独立 SHA-256 树结果一致。此处吞吐是 loopback 数值，不代表跨域表现。
- 全量 CTest：250 项，249 passed、0 failed、1 skipped（`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable`）。构建存在 stub/环境跳过，不能据此声称 io_uring 已验收。

## 二进制与证据

- `cpnetflux-tree-upload-client` SHA-256：`9c4d63b1eab570cb7bccd21ff4df15508c4e77423ccb54f07a3f3d3b2f8a73dd`。
- `cpnetflux-tree-download-client` SHA-256：`859422345943147dbb41112894c8fa03e82c08789c30308ba3ee42a7e7749941`。
- loopback server SHA-256：`8905de3907b5f9aebd6ccbe8840fd31fe95a3deb039d3894fa13f92b35fdbe69`。
- 证据目录：`/tmp/cpnetflux-runs/DIR-V2-MULTICHANNEL-01/verify`；loopback log SHA-256 `d6fcb42ec51b3dd3c4c40fe5640792d3f18a0358408e01dc75d8583136206357`；full CTest log SHA-256 `5664a11c88ff3de56609894f71e18049256bb3b431a4bc40835b2df661220621`。

未运行深圳—上海或 GridFTP 对照实验，未刷新 GSI proxy；没有得出 100 Mbps/WAN 性能结论。后续实验应固定该提交和二进制，配置 `--data-session-reuse tree --file-parallelism 4`，先在深圳—上海按同一 128×1MiB 输入重复测量，并记录两方向阶段计时。
