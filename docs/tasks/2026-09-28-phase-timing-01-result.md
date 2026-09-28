# DIR-PERF-TIMING-01 结果

- 输入基线：`ce5c5a9f2ceea08bd0fc0097867133cef27552cd`
- 实际源码基线：`6f9773ea41dd981b75a042aa14bf759d6d2d8b2d`，固定输入为祖先；`git diff -w --ignore-space-at-eol ce5c5a9..HEAD -- src include tools` 无输出。
- worktree：`/tmp/cpnetflux-runs/DIR-PERF-TIMING-01/implementation`
- 分支：`codex/DIR-PERF-TIMING-01`
- live root `/root/projects/CPNetFlux` 未修改；其现有 dirty 文档/源码状态未作为输入写入本 worktree。

## 改动

- `include/cpnetflux/config/tree_transfer_options.h`：新增 `phaseTiming=false`，默认关闭。
- `src/config/tree_transfer_options.cpp`：新增 `--phase-timing on|off`，缺值、未知值沿现有 invalid-argument 路径；usage 已列出该选项。
- `src/core/io/tree_transfer_client.cpp`：
  - A：在实际 `ensureControlReady` 边界计时，覆盖 preflight 和每文件 `controlForFile` 的成功/失败调用，记录次数与累计纳秒。
  - B：在 `updateRecord`、`updateRecordForTransfer` 的 scheduler mutex 保护状态更新和 `saveManifest` 区间计时，包含等待锁的时间；记录次数与累计纳秒。
  - C：在 upload/download 的 `runFileTransferClient`/`runFileDownloadClient` 及 upload raw-retry 调用边界计时；在成功数据调用后的 `waitTransferComplete` 区间单独计时。
  - 使用同一进程 `steady_clock`，内部用 `uint64` 纳秒原子计数，JSON 转秒；不进行跨主机相减。只在开关打开时向既有 JSON summary 追加 `phase_timing_schema=phase_timing_v1`、`phase_timing_units=seconds`、各 phase 的 `*_count`/`*_seconds`；关闭时不改变现有 JSON 字段/顺序。
- `tests/unit/tree_transfer_options_test.cpp`：覆盖默认 off、`on` 和非法值。

## 验证

已执行：

- `git merge-base --is-ancestor ce5c5a9f2ceea08bd0fc0097867133cef27552cd HEAD`：退出 `0`。
- `git diff -w --ignore-space-at-eol ce5c5a9f2ceea08bd0fc0097867133cef27552cd HEAD -- src include tools`：无源码差异（退出 `0`）。
- `cmake -S /tmp/cpnetflux-runs/DIR-PERF-TIMING-01/implementation -B /tmp/cpnetflux-runs/DIR-PERF-TIMING-01/build -G Ninja -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF -DCPNETFLUX_ENABLE_TLS=ON`：退出 `0`。
- `cmake --build /tmp/cpnetflux-runs/DIR-PERF-TIMING-01/build --parallel 2`：退出 `0`，完整 `110/110`；修改后增量构建退出 `0`。
- `ctest --test-dir /tmp/cpnetflux-runs/DIR-PERF-TIMING-01/build --output-on-failure -R 'TreeTransferOptionsTest'`：退出 `0`，`8/8` 通过。
- `git diff --check`：退出 `0`。

未执行：完整 CTest、tree upload/download smoke、JSON on/off 传输等价性、hash/manifest 对照、A/B/C 实际网络计时、跨域实验。深圳隔离环境存在长期运行的 GridFTP 进程；本任务禁止启动网络实验，故不以未执行项宣称 telemetry 或性能结果。

## 证据与限制

阶段累计是并发 worker 的 sum，不能直接当 wall；JSON 中没有跨主机时间差。B 是 manifest record 状态更新与保存边界，不包含初始扫描创建 manifest 的 run 前阶段。C 将传输调用与其后的控制完成等待拆开；失败调用仍计入对应调用次数和耗时，未发生的阶段不生成额外记录。旧 stdout summary、旧 event log、协议、scheduler、compression、checksum、resume 和 manifest 格式未改动。

需要后续验证：开启 `--phase-timing on --json-summary <path>` 与关闭开关的同一 loopback fixture 对比退出码、hash、manifest、wire/frame 计数及 JSON 字段；再由质量角色审查累计字段的并发解释和性能测量方案。此回执不宣称阶段观测已通过完整 QA，也不宣称任何瓶颈或优化收益。

## 提交

- 实际提交与 push SHA 在提交完成后回填；仅提交本任务四个源码/测试文件和本结果文档。
