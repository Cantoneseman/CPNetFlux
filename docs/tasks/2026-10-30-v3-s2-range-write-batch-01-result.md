# V3 S2 验收记录：Range 写入批量切片

- 阶段：S2（S0-S8 性能路线）
- 任务：2026-10-30-v3-s2-range-write-batch-01
- 基线提交：bb920fff68eb5e9def423cee19fdbe9d46e9eb8f
- 验收目录：深圳服务器 /root/projects/CPNetFlux

## 本轮解决的问题

S0 已经能看到每个 range 收到了多少帧、调用了多少次 writer、最终触发了多少次底层 writeAtAll，但代码还没有一个独立的 range 批量写入口来验证“多帧是否可以合并成更少的底层写入”。本轮增加了一个默认关闭的接收端实验入口，并把实际生效的缓冲大小导出到 range 观测结果中。

## 实现

- `PersistentTransferPolicy::rangeWriteBatchBytes` 支持内部测试或调用方显式设置。
- 环境变量 `CPNETFLUX_RANGE_WRITE_BATCH_BYTES` 支持 `off` 或 65536 到 16777216 的整数。
- range transaction 在没有显式 file-IO buffer、且没有选择 direct 策略时，将该值映射到现有 `BufferedFileWriter` 的 coalesced buffer。
- 显式 `PosixWriteStrategy::Direct` 优先，非法批量值不会开始写入。
- range observation 新增 `range_write_batch_bytes`，单位为 bytes；它是配置值，不是耗时，不应加到任何时间指标中。
- 线上协议仍是 64 KiB 上限 DATA 帧；逐帧校验、逐 chunk verified record、checkpoint 和恢复语义均未改变。
- 分析器白名单同步导出新字段，未改变凭据和 payload 的脱敏规则。

## 行为证据

新增测试覆盖：

1. 两个 128 KiB range 在批量 128 KiB 配置下保持内容正确；每个 range 的 3 个 DATA 帧仍分别计数，但底层 `writeAtAll` 为 1 次，导出值为 131072。
2. 4096 字节非法配置在开始写入前被拒绝，目标文件不会发布。
3. direct 策略覆盖非法环境值，导出的有效批量大小为 0。
4. Python range observation 导出器能读取新字段，并继续区分 enabled/missing/disabled/legacy_partial。

## 验收命令和结果

- `git diff --check`：通过。
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`：通过。
- `cmake --build build --parallel 4`：通过。
- 相关 CTest：3/3 通过。
- `python3 tools/perf/test_range_observation.py`：7/7 通过。
- 全量 CTest（排除缺少 `CPNETFLUX_TEST_TOKEN` 的两个既有 token smoke）：473/473 通过；io_uring 条件测试 1 项 skipped。
- 未设置 `CPNETFLUX_TEST_TOKEN` 的原始全量 CTest 中，只有 `cpnetflux_gridftp_control_token_smoke` 和 `cpnetflux_event_log_smoke` 失败；它们在排除后全套通过，失败原因是测试环境凭据缺失，不是本轮改动。

## 结论和边界

本轮证明了 range 接收端可以在不改变线上帧和 checkpoint 语义的情况下，把连续 DATA 写入交给已有 coalesced writer，并能通过 S0 观测核实调用数变化。行为测试没有证明真实跨服务器吞吐提升；这需要下一轮使用同一数据、同一链路、batch off/on 的双机实验包比较 `data_frame_count`、`writer_call_count`、`file_write_at_all_calls/bytes`、写入耗时和总吞吐。

下一步先进行 S2 受控 A/B 实验。如果批量只减少 syscall 但写入耗时或总吞吐没有改善，记录 no-go 并转入 S3 的锁/所有权分析；如果有稳定收益，再决定是否继续发送端内部批量。
