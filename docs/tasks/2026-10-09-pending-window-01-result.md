# DIR-V2-PENDING-WINDOW-01 结果

日期：2026-10-09。权威工作区：深圳 `/root/projects/CPNetFlux`；输入 HEAD：`5cd2fb69232a860d9dd0000b631a0c1123cc8324`；分支：`codex/DIR-V2-TRANSFER-ENGINE-01`。

## 实现

- 在持久目录数据会话上增加 `--data-pending-window 1..16`，默认值为 1。窗口大于 1 时使用 `XCPNETFLUX V2 WINDOW=N` 协商；旧的精确 `XCPNETFLUX V2` 仍按窗口 1 工作。
- 上传端使用一个数据连接、一个有界 pending 队列和独立 `FILE_RESULT` 读取线程，允许后续文件帧发送与前一文件结果等待重叠；队列高水位写入 summary。
- 结果消费严格校验 `fileId/generation/totalSize`，回调串行化；写失败、超时、断线和取消通过 shutdown 唤醒读取线程并回收。manifest、resume、hash 和默认 v1 行为未改。
- summary 保留/输出 `control_prepare`、`transfer_complete_wait`、`data_pending_window`、`data_pending_high_watermark`。
- 接收端保持逐文件提交，以保证现有 manifest/resume 语义；它报告协商窗口，但不虚报接收端并行度。

## 验证

- 目标构建成功：`cpnetflux_unit_tests`、server、tree upload/download client。
- 定向单元测试：12/12 通过，覆盖窗口参数、身份/generation、超时、断线、失败中间文件和 checkpoint/resume 相关既有行为。
- 新 128×1MiB loopback smoke：窗口 1/2/4 的 upload/download 均通过，均为 128 文件、134217728 B，tree hash 保持一致；每个窗口的 pending 高水位不超过窗口。
- 深圳—上海短测：128×1MiB，checksum none、worker control reuse、tree data-session reuse、单连接、无完整校验扩展；六个 case 均返回 0，阶段摘要如下。

| 方向 | window=1 | window=2 | window=4 |
|---|---:|---:|---:|
| 上传 client throughput (Gbps) | 0.103855 | 0.105119 | 0.104289 |
| 下载 client throughput (Gbps) | 0.100413 | 0.102681 | 0.104932 |
| 上传 wall (s) | 10.6722 | 10.5482 | 10.6306 |
| 下载 wall (s) | 11.0294 | 10.7909 | 10.5670 |

实验后独立读取深圳源目录和上海三个目标目录，均为 128 文件、134217728 B、tree hash `99b993fa742cb9cb66b83969d58a52b7344d8bf49892f6a8f09656ea305f6b69`。这些是单次短测，不能据此宣称相对 GridFTP 的收益或因果结论。

- 完整 CTest：246 项中 243 通过、1 个 io_uring 环境 skip、2 个失败。失败均为既有 token smoke 缺少受控环境变量 `CPNETFLUX_TEST_TOKEN`（`cpnetflux_gridftp_control_token_smoke` 及依赖它的 `cpnetflux_event_log_smoke`），不是本改动的窗口测试；未复制或打印 token。

## 交接

本版仍在开发主线中；回环和深圳—上海 128×1MiB 已完成，下一步应在同一输入、同一 GSI/GridFTP 参数下做一次旧版与窗口 1/2/4 的对照，再决定默认窗口。当前数据只显示窗口 2/4 在该单次 WAN 测试中略快，尚不足以继续扩大矩阵。
