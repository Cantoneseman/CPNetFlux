# CPNetFlux 性能基线工具

## Lab Beta 状态

Lab Beta retained evidence runs now live under `tools/perf/results/<UTC>_lab-beta-*`.
Beta 0 gate/public smoke, Beta 1 public protocol baseline, and Beta 2 public
control-reuse expansion have been accepted. Beta 3 now has three retained
evidence points: the original no-go run, the final-status failure triage, and the
bounded rerun after fix.

Beta 3 uses the Shenzhen-configured OSS bucket `science-compressor-datasets` as
the dataset source, caps staged source data at `<= 3GiB`, and keeps gzip/lz4/CPSS
strictly in a staging/restore layer. It does not run 10GiB/20GiB/100GiB/heavy
soak, CPSS training, SSH tunnels, private IP paths, or scp/rsync substitutes for
FTP/GridFTP/CPNetFlux transfer evidence.

Beta 3 evidence timeline:

- Original run:
  `tools/perf/results/20260720T051105Z_lab-beta-3-public-compression-staging-matrix/`,
  status `fail_correctness` / no-go. One required CPNetFlux lz4 worker row failed
  with `exit_code=1` (`recv: Resource temporarily unavailable`) even though the
  restored tree hash matched the source.
- Failure triage:
  `tools/perf/results/20260721T034707Z_lab-beta-3-failure-triage-cpnetflux-lz4-final-status/`,
  status `pass_after_fix`. It covered only the failed
  `oss_ai_training_mixed` / `cpnetflux_lz4_worker` path and passed the minimal
  public repro 3/3 after the final-status timeout fix.
- Rerun after fix:
  `tools/perf/results/20260721T093510Z_lab-beta-3-compression-staging-rerun-after-final-status-fix/`,
  status `partial_go_cpss_environment_blocked`. CPNetFlux, FTP, and GridFTP
  raw/gzip/lz4 transfer status and restore/tree hash all passed; CPSS remained
  environment-blocked.

Current mouthpiece: raw/gzip/lz4 Beta 3 staging correctness passed; CPSS is
`environment_blocked`; this is not `full_go`. Do not describe CPSS as passed, do
not expand this closeout into 10GiB/20GiB/100GiB/heavy soak, and do not enter
CPNetFlux C++ hot-path compression design from this evidence.

本目录记录 CPNetFlux 的可复现性能工具。`run_loopback_matrix.py` 和 `run_private_once.sh` 仍用于 memory-to-memory TCP sink；`run_file_loopback_matrix.py` 和 `run_file_private_once.sh` 用于文件传输基线；`run_gridftp_private_matrix.py` 用于 GridFTP-like framed STOR/RETR 私网矩阵。Phase 2B 起文件传输默认启用 CRC32C chunk checksum，并支持 `--checksum none` 做性能对照。Phase 2C 起 CRC32C 支持 `auto` / `software` / `hardware` backend，`auto` 在 x86 SSE4.2 可用时选择 hardware。Phase 4F 新增可选 file-IO-only `io_uring` prototype；Phase 4G 已在真实 liburing 环境下验证；Phase 4H 增加 queue depth / batching opt-in 维度；Phase 4J 增加 POSIX storage/writeback、manifest flush、checksum 和 final verify 阶段诊断。默认仍是 POSIX backend，网络仍是 epoll，STOR/RETR 文件数据仍只走 CPNetFlux framed data channel。

Phase 1 acceptance audit lives in `docs/perf/PHASE1_GLOBAL_SCHEDULER_MVP.md`.
Phase 2 feedback-driven scheduler validation lives in
`docs/perf/PHASE2_GLOBAL_SCHEDULER.md`. It compares `off`, `global/fixed`, and
`global/adaptive`; the adaptive target is bounded by `--connections`, and
remote 10M results are limited to correctness, resume, backpressure, queue,
and metrics explainability.
Phase 3 hot-path lossless compression scope, wire contract, raw fallback,
resume retry, and validation boundaries live in
`docs/perf/PHASE3_HOT_PATH_COMPRESSION.md`. Compression is opt-in to global
tree upload candidates; `--scheduler off` remains raw.

Phase 4N 起，alpha release gate 会生成 `tools/perf/results/<timestamp>_alpha-artifacts.json`，并用该 manifest 同步和校验远端 release artifacts。性能 CSV、summary CSV 和 CSV 引用的 sidecar logs 若被纳入 manifest，<redacted>一/<redacted>二必须 hash 一致。

Phase 5A 起，目录级 alpha smoke 通过 `cpnetflux-tree-upload-client` /
`cpnetflux-tree-download-client` 验证多文件 upload/download/resume。目录传输仍逐文件复用
CPNetFlux framed STOR/RETR；性能矩阵默认仍以单文件 STOR/RETR 为主要口径。
Phase 5C 起 tree private matrix 会给每次 tree CLI 调用传入 `--json-summary`，
raw CSV 会记录 JSON summary 路径和 completed/skipped/failed/changed、
bytes_total、bytes_transferred、tree_hash/error_message 等字段；stdout
key=value 仅作为 fallback。

Phase 6B 起可使用 `--event-log <path>` 生成 JSONL 事件日志。性能脚本仍以
CSV 为主，event log 用于单 case 排障：auth/path/manifest/checksum/changed-file
等错误会映射到稳定 `error_code`。事件日志不得包含 token/password。

## Alpha Artifact Sync

```bash
python3 tools/release/sync_remote_artifacts.py \
  --manifest tools/perf/results/<timestamp>_alpha-artifacts.json \
  --remote <remote> \
  --local-root /root/projects/CPNetFlux \
  --remote-root <remote-root> \
  --verify-only \
  --json-output tools/perf/results/<timestamp>_artifact-verify.json
```

`--dry-run` 只打印缺失/不一致计划，`--sync` 只同步 manifest 中 required 且安全的缺失或不一致文件，然后再 verify。不使用 `--delete`，不会清理远端历史 build/private 目录。
JSON summary 中的 `pre_sync_*` 字段表示同步前状态，`post_sync_*` 字段表示同步后状态。

## 基础构建验证

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

## 环境采集

```bash
tools/perf/collect_env.sh --output tools/perf/results/env-local.md
```

输出为 Markdown，包含 OS/kernel、CPU、内存、IP、TCP sysctl，以及可用时的 `iperf3`、`fio`、`numactl`、`ethtool` 信息。

## Loopback Smoke Matrix

```bash
tools/perf/run_loopback_matrix.py --build-dir build --smoke --output-dir tools/perf/results
```

默认运行两组参数：`1 x 64KB` 和 `4 x 256KB`，默认总字节数为 `8388608`。输出 CSV 写入 `tools/perf/results/`。

## Loopback Full Matrix

```bash
tools/perf/run_loopback_matrix.py --build-dir build --bytes 1073741824 --output-dir tools/perf/results
```

默认扫描：

- connections：`1,4,8,16,32`
- buffer-size：`65536,262144,1048576,4194304`

## <redacted>二同步

远程同步需要人工确认后运行。脚本不会打印密码；如需密码登录，使用 `CPNETFLUX_SSH_PASSWORD` 环境变量。

```bash
export CPNETFLUX_SSH_PASSWORD='***'
tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/CPNetFlux --target /root/projects/CPNetFlux
```

同步后需要在<redacted>二构建：

```bash
ssh root@<redacted> 'cd /root/projects/CPNetFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'
```

## 私网单次测试

跨机测试需要人工确认<redacted>二已经同步并构建完成。默认从<redacted>一启动 server，并通过 SSH 在<redacted>二运行 client。

```bash
tools/perf/run_private_once.sh --remote root@<redacted> --server-host <redacted> --build-dir /root/projects/CPNetFlux/build --connections 8 --bytes 1073741824 --buffer-size 65536
```

输出 CSV 以及 server/client 原始日志写入 `tools/perf/results/`。

## 文件传输 Loopback Smoke Matrix

```bash
tools/perf/run_file_loopback_matrix.py \
  --build-dir build \
  --smoke \
  --bytes 67108864 \
  --connections 1,4,8 \
  --chunk-sizes 1048576,4194304 \
  --buffer-sizes 65536 \
  --checksum crc32c \
  --checksum-backend auto \
  --output-dir tools/perf/results
```

默认 smoke 覆盖 `connections=1,4,8`、`chunk_size=1MiB,4MiB`、`buffer_size=64KiB`。脚本为每个 case 生成源文件，运行 `cpnetflux-file-server` / `cpnetflux-file-client`，计算源/目标 sha256，并写 CSV。

如需和无 checksum 路径对照：

```bash
tools/perf/run_file_loopback_matrix.py \
  --build-dir build \
  --smoke \
  --bytes 67108864 \
  --connections 1,4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksum none \
  --output-dir tools/perf/results
```

## CRC32C Backend Microbenchmark

```bash
tools/benchmark/run_checksum_bench.py \
  --build-dir build \
  --bytes 67108864,268435456 \
  --output-dir tools/perf/results
```

脚本调用 `cpnetflux-checksum-bench`，默认扫描 `software,auto,hardware` backend，输出 CSV 到 `tools/perf/results/`。如果显式 `hardware` 在当前<redacted>不可用，会记录为 `skip`；`auto` 应回退到 software。单次命令也可直接运行：

```bash
./build/cpnetflux-checksum-bench --backend auto --bytes 67108864 --iterations 5
```

## Native Storage Benchmark

Phase 4C 新增 `cpnetflux-storage-bench`，它直接使用项目 `PosixFile` 路径测顺序写、读和 rewrite，不再用 Python IO 作为主要磁盘口径。Phase 4D 起 bench 输出每次 iteration raw 行和 aggregate 行，并记录 file IO call count、average bytes per call 与 file IO wait time。

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off,full \
  --file-io-advices off,sequential \
  --buffer-sizes 1048576 \
  --iterations 3 \
  --output-dir tools/perf/results
```

单次 C++ 命令也可直接运行：

```bash
./build/cpnetflux-storage-bench \
  --path /tmp/cpnetflux-storage-bench.bin \
  --mode all \
  --bytes 1073741824 \
  --buffer-size 1048576 \
  --iterations 1 \
  --preallocate off \
  --file-io-backend posix \
  --file-io-advice off \
  --posix-write-strategy auto \
  --file-io-buffer-size 0
```

`--preallocate full` 使用 `posix_fallocate`。如果系统或文件系统返回错误，命令会失败并记录错误，不会静默降级为 off。
`--file-io-advice` 支持 `off`、`sequential`、`noreuse`、`dontneed`、`sequential_dontneed`；非 off 会显式调用 `posix_fadvise`，调用失败则当前 case 失败。
Phase 4F 起 `--file-io-backend` 支持 `posix|io_uring`。`posix` 是默认值；`io_uring` 只有在构建时显式启用 `-DCPNETFLUX_ENABLE_IO_URING=ON` 且探测到 liburing 时可用。Phase 4G 中本机和<redacted>二均已安装 `liburing-dev` 并完成真实 io_uring 构建、CTest 与 POSIX/io_uring 对比；即便如此，默认 backend 仍保持 POSIX。Phase 4H 起 storage bench 和 private matrix 支持 `--file-io-queue-depths` 与 `--file-io-batch-sizes`；未显式传 batch size 时默认跟随 queue depth。queue/batch 只影响 `io_uring` backend，POSIX 路径仅记录参数用于公平 CSV 对照。
Phase 4K 起 `--posix-write-strategy auto|direct|coalesced` 用于 POSIX temp write/writeback 诊断。默认 `auto` 保持既有语义：`file_io_buffer_size=0` 直写，`>0` 使用 contiguous coalescing；`direct` 强制直写；`coalesced` 要求 `--file-io-buffer-size > 0`。

## Tree Private Matrix

Phase 5B/5C 的目录级私网矩阵使用现有 `cpnetflux-gridftp-server` 与
`cpnetflux-tree-upload-client` / `cpnetflux-tree-download-client`：

```bash
python3 tools/perf/run_gridftp_tree_private_matrix.py \
  --remote <remote> \
  --server-host <server-host> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --datasets small,mixed \
  --directions upload,download \
  --file-parallelisms 1,2,4 \
  --connections 2 \
  --checksums crc32c,none \
  --repeat 3 \
  --output-dir tools/perf/results
```

Phase 5C raw CSV includes `json_summary`, `completed_files`,
`skipped_files`, `failed_files`, `changed_files`, `bytes_total`,
`bytes_transferred`, `summary_tree_hash`, and `error_message`. Summary CSV
keeps min/median/max throughput and tree hash mismatch counts grouped by
dataset, direction, file parallelism, connection count, and checksum.

Phase 4F/4G no-liburing fallback 验证：

```bash
cmake -S . -B build-iouring-probe -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DCPNETFLUX_ENABLE_IO_URING=ON
cmake --build build-iouring-probe
ctest --test-dir build-iouring-probe --output-on-failure

./build-iouring-probe/cpnetflux-storage-bench \
  --path /tmp/cpnetflux-iouring-unavailable.bin \
  --mode write \
  --bytes 1048576 \
  --buffer-size 65536 \
  --iterations 1 \
  --preallocate off \
  --file-io-backend io_uring
```

最后一条命令在无 liburing 环境下应非零退出，并包含 `file IO backend unavailable: io_uring`。

安装 liburing 后，可用如下方式采集 POSIX / io_uring 对照：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-advices off \
  --buffer-sizes 262144,1048576 \
  --iterations 3 \
  --output-dir tools/perf/results
```

Phase 4H queue-depth storage bench：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --bytes 1073741824 \
  --modes write,read,rewrite \
  --preallocates off \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-advices off \
  --buffer-sizes 262144,1048576 \
  --iterations 3 \
  --output-dir tools/perf/results
```

Phase 4I heavy queue-depth gate storage bench（注意：`--side local` 不再探测远端；只有 `remote|both` 会 SSH）：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --bytes 1073741824 \
  --modes write,read,rewrite \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-batch-sizes 1,4,8,16 \
  --buffer-sizes 262144,1048576 \
  --preallocates off \
  --iterations 3 \
  --output-dir tools/perf/results
```

Phase 4G 真实 liburing storage bench 命令：

```bash
cmake -S . -B build-io-uring-real -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DCPNETFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure

python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --bytes 1073741824 \
  --modes write,read,all \
  --preallocates off,full \
  --file-io-backends posix,io_uring \
  --file-io-advices off \
  --buffer-sizes 262144,1048576 \
  --iterations 3 \
  --output-dir tools/perf/results
```

## 文件传输私网单次测试

跨机文件测试需要人工确认<redacted>二已同步并构建。源文件生成在 client 所在<redacted>二，目标文件生成在 server 所在<redacted>一；脚本不会打印密码，如需密码登录使用 `CPNETFLUX_SSH_PASSWORD`。

```bash
export CPNETFLUX_SSH_PASSWORD='***'
tools/perf/run_file_private_once.sh \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --connections 4 \
  --bytes 268435456 \
  --chunk-size 1048576 \
  --buffer-size 65536 \
  --checksum crc32c \
  --checksum-backend auto \
  --output-dir tools/perf/results
```

输出 CSV 以及 server/client 原始日志写入 `tools/perf/results/`。结果只有在 server/client 均成功且 `source_sha256 == dest_sha256` 时才标记为 `pass`。

## 文件传输私网 checksum resume smoke

该脚本从<redacted>一启动 server，在<redacted>二生成 source，并先用 `--max-chunks` 制造 partial transfer，再双端 `--resume` 补传。它用于可靠性 smoke，不属于性能矩阵。

```bash
export CPNETFLUX_SSH_PASSWORD='***'
tools/test/run_file_checksum_private_once.sh \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --connections 4 \
  --bytes 67108864 \
  --chunk-size 1048576 \
  --buffer-size 65536 \
  --checksum crc32c \
  --checksum-backend auto
```

## GridFTP-like Framed 私网矩阵

Phase 4A/4B 使用 `cpnetflux-gridftp-server` 控制面发起 STOR/RETR，但文件数据仍走 CPNetFlux framed data channel。脚本从<redacted>一启动 control server，通过 SSH 在<redacted>二运行 CPNetFlux-aware framed client，并为每个 case 分配唯一端口、root、transfer id、日志和临时路径。

Phase 4B 新增 host/link baseline：

```bash
export CPNETFLUX_SSH_PASSWORD='***'
tools/perf/run_private_host_baseline.py \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --bytes 1073741824 \
  --output-dir tools/perf/results
```

该脚本优先使用 `iperf3` / `fio`；如果任一端缺工具，会自动标记 fallback 并使用 CPNetFlux memory sink 或 Python 顺序 IO 探针，不安装系统软件。

Smoke 模式默认覆盖：

- direction：`stor,retr`
- bytes：`64MiB,128MiB`
- connections：`1,4`
- chunk size：`1MiB`
- buffer size：`64KiB`
- checksum：`crc32c,none`

```bash
export CPNETFLUX_SSH_PASSWORD='***'
tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --output-dir tools/perf/results
```

代表性 1GiB 样本：

```bash
tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c \
  --final-verify-policy full \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --output-dir tools/perf/results
```

Full matrix 必须显式传 `--full`，默认覆盖：

- direction：`stor,retr,stor-resume,retr-resume`
- bytes：`256MiB,1GiB`
- connections：`1,2,4,8,16`
- chunk size：`1MiB,4MiB,16MiB`
- buffer size：`64KiB,256KiB,1MiB`
- checksum：`crc32c,none`

```bash
tools/perf/run_gridftp_private_matrix.py --full ...
```

Phase 4B 的诊断/优化参数：

- `--final-verify-policy full|verified_chunks`：默认 `full`，保持完整 temp 重读验证。`verified_chunks` 只在 checksum 启用、verified chunks 覆盖完整文件且 manifest 已成功 flush 时跳过最终重读，否则 C++ 路径会回退到 `full` 并输出 `final_verify_policy_effective=full`。
- `--manifest-flush-interval-chunks <N>`：默认 `16`，控制 upload/download manifest 批量 flush 间隔；失败、resume precheck 和 commit 前仍强制 flush。
- `--manifest-flush-policy every_n_chunks|final_only`：Phase 4J 起支持。默认 `every_n_chunks`；`final_only` 仅用于诊断，失败和 commit 前仍强制 flush，崩溃后最多重传未 flush 的 chunk，不允许误提交。
- `--commit-sync-policy none|fsync_file|fsync_file_and_dir`：Phase 4J 起支持。默认 `none`；fsync 模式仅用于测量 rename/commit 同步成本。
- `--host-baseline-csv <path>`：把对应 host/link baseline CSV 路径写入每个 matrix row，便于报告关联。

Phase 4C 的矩阵稳定性与存储参数：

- `--repeat <N>`：每个参数点重复运行 N 次，raw CSV 写 `repeat_index`。
- `--preallocates off,full`：扫描 STOR server temp 和 RETR download temp 的 preallocation 策略。默认 `off`。
- `--final-verify-policies full,verified_chunks`：扫描 final verify policy 维度。`verified_chunks` 仍为 opt-in，不作为默认可靠性语义。
- `--storage-bench-csv <path>`：把 native storage bench CSV 关联到每个 matrix row。
- 每次运行额外生成 `*-summary.csv`，按参数分组统计 throughput/elapsed 的 min、median、max 和 pass/fail count。

Phase 4D/4F 的 file IO 参数：

- `--file-io-backends posix,io_uring`：扫描 file IO backend。默认只跑 `posix`。无 liburing 时显式 `io_uring` case 会记录为 fail，不会静默跳过。
- `--file-io-buffer-sizes 0,1048576`：扫描文件 IO buffer 策略。`0` 表示关闭新增 buffering，保持 Phase 4C 默认；大于 0 时接收端会合并同一连接、同一 chunk 内连续 DATA 后再落盘。
- `--file-io-advices off,sequential`：扫描 POSIX advice。默认 `off`；非 off 调用失败则 case 失败。
- `--file-io-queue-depths 1,4,8,16`：Phase 4H 起扫描 io_uring queue depth。默认 `1`。对 POSIX 只记录，不改变行为。
- `--file-io-batch-sizes 1,4`：Phase 4H 起扫描 batch size。未传时每个 case 的 batch size 跟随 queue depth；实际每轮 submit 上限为 `min(batch_size, queue_depth)`。

Phase 4E 重型采样命令：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off,full \
  --file-io-advices off,sequential,sequential_dontneed \
  --buffer-sizes 65536,262144,1048576,4194304 \
  --iterations 3 \
  --output-dir tools/perf/results
```

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c \
  --checksum-backend auto \
  --preallocates off,full \
  --final-verify-policies full,verified_chunks \
  --file-io-buffer-sizes 0,1048576,4194304 \
  --file-io-advices off,sequential,sequential_dontneed \
  --repeat 3 \
  --case-timeout 600 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --output-dir tools/perf/results
```

将上面矩阵命令分别替换为 `--directions stor --checksums none`、`--directions retr --checksums crc32c`、`--directions retr --checksums none`，分 4 批保存 raw/summary CSV。

Phase 4E 分析报告：

```bash
python3 tools/perf/analyze_phase4e.py \
  --storage-summary-csv <storage-summary.csv> \
  --matrix-summary-csv <stor-crc-summary.csv>,<stor-none-summary.csv>,<retr-crc-summary.csv>,<retr-none-summary.csv> \
  --matrix-raw-csv <stor-crc.csv>,<stor-none.csv>,<retr-crc.csv>,<retr-none.csv> \
  --output docs/perf/PHASE4E_IO_URING_GATE.md
```

Phase 4E 实测结论记录在 `docs/perf/PHASE4E_IO_URING_GATE.md`：默认仍保持 `file_io_buffer_size=0`、`file_io_advice=off`、`preallocate=off`、`final_verify_policy=full`，`verified_chunks` 继续 opt-in。

Phase 4G 真实 liburing 私网对比命令：

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix,io_uring \
  --file-io-buffer-sizes 0 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --output-dir tools/perf/results
```

Phase 4G 结果记录在 `docs/perf/PHASE4G_IO_URING_REAL_VALIDATION.md`。当前结论：真实 io_uring 路径可编译、可运行、可测试、可对比，但同步 submit-and-wait v1 未证明适合替代默认 POSIX；如继续，应进入 queue depth / batching 原型。

Phase 4H queue-depth 私网 sample：

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-buffer-sizes 0 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --output-dir tools/perf/results
```

Phase 4H 当前实测报告见 `docs/perf/PHASE4H_IO_URING_QUEUE_DEPTH_RESULTS.md`。默认仍保持 `file_io_backend=posix`。

Phase 4I fixed the RETR queue/batch pass-through limitation from the Phase 4H sample and reran heavy repeat=3 sampling:

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-buffer-sizes 262144 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --output-dir tools/perf/results
```

Phase 4I analysis:

```bash
python3 tools/perf/analyze_phase4i.py \
  --storage-summary-csv tools/perf/results/20260517T122107Z_storage-bench-summary.csv \
  --matrix-summary-csv tools/perf/results/20260517T141550Z_gridftp-private-matrix-smoke-summary.csv \
  --output docs/perf/PHASE4I_HEAVY_QUEUE_DEPTH_GATE.md
```

Phase 4I 结论记录在 `docs/perf/PHASE4I_HEAVY_QUEUE_DEPTH_GATE.md`：queue-depth/batching 收益不稳定，未贯穿 STOR/RETR 和 crc32c/none，默认仍保持 POSIX。

Phase 4J POSIX pipeline diagnosis:

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix \
  --file-io-buffer-sizes 262144 \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies every_n_chunks,final_only \
  --manifest-flush-interval-chunks-list 16,256 \
  --final-verify-policies full,verified_chunks \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

POSIX writeback add-on:

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums none \
  --file-io-backends posix \
  --file-io-buffer-sizes 0,262144,1048576,4194304 \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies final_only \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

Phase 4J analysis:

```bash
python3 tools/perf/analyze_phase4j.py \
  --matrix-summary-csv tools/perf/results/20260517T154238Z_gridftp-private-matrix-smoke-summary.csv \
  --matrix-summary-csv tools/perf/results/20260517T160524Z_gridftp-private-matrix-smoke-summary.csv \
  --output docs/perf/PHASE4J_POSIX_PIPELINE_DIAGNOSIS.md
```

Phase 4J 结论记录在 `docs/perf/PHASE4J_POSIX_PIPELINE_DIAGNOSIS.md`：STOR median 主要瓶颈是 temp write/writeback；RETR 主要由 receiver download write 与 sender network send 构成；checksum 不是 STOR 主瓶颈，`verified_chunks` 对部分 RETR 场景有 opt-in 收益但默认仍保持 full final verify。

Phase 4K POSIX writeback strategy matrix:

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --bytes 1073741824 \
  --modes write,read,rewrite \
  --file-io-backends posix \
  --posix-write-strategies auto,direct,coalesced \
  --file-io-buffer-sizes 0,262144,1048576 \
  --buffer-sizes 262144,1048576 \
  --preallocates off \
  --iterations 3 \
  --output-dir tools/perf/results
```

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix \
  --file-io-buffer-sizes 0,262144,1048576 \
  --posix-write-strategies auto,direct,coalesced \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies every_n_chunks \
  --manifest-flush-interval-chunks-list 16 \
  --commit-sync-policies none \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

Phase 4K analysis:

```bash
python3 tools/perf/analyze_phase4k.py \
  --storage-summary-csv tools/perf/results/20260517T164727Z_storage-bench-summary.csv \
  --matrix-summary-csv tools/perf/results/20260517T171606Z_gridftp-private-matrix-smoke-summary.csv \
  --output docs/perf/PHASE4K_POSIX_WRITEBACK_OPTIMIZATION.md
```

Phase 4K 结论记录在 `docs/perf/PHASE4K_POSIX_WRITEBACK_OPTIMIZATION.md`：没有策略同时稳定改善 STOR 与 RETR；默认继续 `posix_write_strategy=auto` 且 `file_io_buffer_size=0`，`direct` 和 `coalesced` 仅作为 opt-in 诊断策略。

Phase 4L stability and RETR breakdown matrix:

```bash
CPNETFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix \
  --file-io-buffer-sizes 0,262144 \
  --posix-write-strategies auto,coalesced \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies every_n_chunks,final_only \
  --manifest-flush-interval-chunks-list 16 \
  --commit-sync-policies none \
  --final-verify-policies full,verified_chunks \
  --repeat 5 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

The generator skips invalid `coalesced + file_io_buffer_size=0` combinations. Phase 4L adds per-case sidecar environment logs next to each server/client log:

- `*_env_before.log`
- `*_env_after.log`

Each sidecar records `free -m`, Dirty/Writeback/Cached from `/proc/meminfo`, `df -h` for the test path, and `iostat -xz 1 1` when available. If `iostat` is missing, the sidecar records `iostat=unavailable`.

Phase 4L analysis:

```bash
python3 tools/perf/analyze_phase4l.py \
  --matrix-summary-csv tools/perf/results/20260518T004459Z_gridftp-private-matrix-smoke-summary.csv \
  --output docs/perf/PHASE4L_STABILITY_AND_RETR_BREAKDOWN.md
```

Phase 4L 结论记录在 `docs/perf/PHASE4L_STABILITY_AND_RETR_BREAKDOWN.md`：repeat=5 1GiB private matrix `240/240` pass，summary `21/48` rows 的 throughput spread 超过 `20%`。STOR 仍主要由 temp write/writeback 主导；RETR 的主要瓶颈会在 sender network send 与 receiver download temp write 之间切换。由于波动较大且方向不一致，默认继续保持 POSIX backend、`posix_write_strategy=auto`、`file_io_buffer_size=0`、full final verify 和 every_n_chunks manifest flush；暂无强 opt-in 推荐。

Phase 5B tree private matrix:

```bash
CPNETFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_tree_private_matrix.py \
  --remote <remote> \
  --server-host <server-host> \
  --local-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --remote-build-dir /root/projects/CPNetFlux/build-io-uring-real \
  --directions upload,download \
  --datasets mixed \
  --file-parallelisms 1,2,4 \
  --connections 2 \
  --checksums crc32c,none \
  --repeat 3 \
  --output-dir tools/perf/results
```

The tree matrix starts `cpnetflux-gridftp-server` locally and runs
`cpnetflux-tree-upload-client` / `cpnetflux-tree-download-client` on the remote
machine. It records raw and summary CSV. Summary rows include
`throughput_gbps_min/median/max`, `repeat_count`, `fail_count`, and
`tree_hash_mismatch_count`. Use:

```bash
python3 tools/perf/analyze_phase5b.py \
  --matrix-summary-csv tools/perf/results/<timestamp>_gridftp-tree-private-matrix-summary.csv \
  --output docs/perf/PHASE5B_TREE_DATASET_MATRIX.md
```

Tree matrix CSV fields:

```text
timestamp,dataset,direction,resume,repeat_index,file_count,total_bytes,file_parallelism,connections,checksum_algorithm,checksum_backend,elapsed_seconds,throughput_gbps,source_tree_hash,dest_tree_hash,result,server_log,client_log,control_port,data_port_base,local_root,remote_source,remote_dest,error
```

Directory transfer throughput is logical dataset bytes divided by elapsed time.
Tree hash uses sorted relative paths, file size, and file content hashes.

## Public Release Hygiene

本地 `AGENTS.md` 是私有协作上下文，不得公开发布。公开发布前使用 export gate：

```bash
python3 tools/release/check_public_hygiene.py --path .
python3 tools/release/export_public_repo.py --output /tmp/cpnetflux-public --force
python3 tools/release/check_public_hygiene.py --path /tmp/cpnetflux-public --strict
test ! -f /tmp/cpnetflux-public/AGENTS.md
test -f /tmp/cpnetflux-public/AGENTS.example.md
```

私有工作区的 hygiene check 可能因本地 `AGENTS.md` 和历史私网拓扑记录失败；这是预期的发布闸门。公开目录必须由 `export_public_repo.py` 生成并通过 strict check。

## Alpha Release Gate

Phase 4M 将已有构建、CTest、smoke、public hygiene 和 private baseline 编排为 alpha release gate。文档见 `docs/release/README.md` 与 `docs/release/ALPHA_READINESS.md`。

Quick gate：

```bash
python3 tools/release/run_alpha_release_gate.py \
  --quick \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/CPNetFlux \
  --results-dir tools/perf/results
```

Full gate 会在 quick 基础上跑 1GiB repeat=3 STOR/RETR private baseline，并输出 Markdown 与 JSON：

```bash
CPNETFLUX_SSH_PASSWORD='***' python3 tools/release/run_alpha_release_gate.py \
  --full \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/CPNetFlux \
  --server-host <server-host> \
  --results-dir tools/perf/results
```

远端 artifact 同步检查：

```bash
python3 tools/release/check_remote_artifact_sync.py \
  --remote <remote> \
  --local-root /root/projects/CPNetFlux \
  --remote-root /root/projects/CPNetFlux \
  --path INDEX.md \
  --path docs/ROADMAP.md \
  --path docs/PROJECT_STATE.md \
  --path docs/release/ALPHA_RELEASE_GATE.md \
  --path docs/release/ALPHA_READINESS.md \
  --csv <phase4m-private-raw.csv> \
  --csv <phase4m-private-summary.csv>
```

该检查只报告缺失或 hash mismatch，不会删除远端文件。CSV 中引用的 `*_log` 与 env sidecar log 也会被校验。

Phase 4C repeat=3 代表性矩阵：

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --preallocates off,full \
  --final-verify-policies full,verified_chunks \
  --file-io-buffer-sizes 0,1048576 \
  --file-io-advices off,sequential \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --output-dir tools/perf/results
```

失败时脚本保留日志与临时路径并在 CSV 写入 `result=fail` 和错误摘要；成功 case 默认清理临时数据。脚本结束会检查本机与<redacted>二是否有遗留 `cpnetflux-gridftp-server` / `cpnetflux-file-*` 进程。

## CSV 字段

```text
timestamp,hostname,mode,host,port,connections,buffer_size,bytes,client_elapsed_seconds,client_throughput_gbps,server_elapsed_seconds,server_throughput_gbps,client_bytes,server_bytes,result
```

文件传输 CSV 字段：

```text
timestamp,mode,host,port,connections,chunk_size,buffer_size,bytes,checksum_enabled,checksum_algorithm,checksum_backend,skipped_bytes,resent_bytes,verified_bytes,manifest_flush_policy,manifest_flush_count,elapsed,throughput_gbps,source_sha256,dest_sha256,result
```

实际文件传输 CSV 还会附加 client/server 独立耗时、吞吐和日志路径，便于后续分析。CSV 内的 `source_sha256` / `dest_sha256` 是测试验收手段；内部恢复事实源是 manifest v2 `verified_chunks` 和 chunk checksum。

GridFTP-like 私网矩阵 CSV 字段：

```text
timestamp,mode,direction,bytes,connections,chunk_size,buffer_size,checksum_algorithm,checksum_backend,preallocate,file_io_backend,file_io_buffer_size,posix_write_strategy,posix_write_strategy_effective,file_io_queue_depth,file_io_batch_size,file_io_advice,repeat_index,elapsed,throughput_gbps,skipped_bytes,resent_bytes,verified_bytes,manifest_flush_count,manifest_flush_policy,manifest_flush_interval_chunks,commit_sync_policy,final_verify_policy,final_verify_policy_effective,stage_*,write_call_count,write_syscall_count,write_retry_count,write_short_count,write_zero_count,write_total_bytes,write_avg_bytes_per_call,write_avg_bytes_per_syscall,data_receive_seconds,temp_write_seconds,source_read_seconds,network_send_seconds,download_temp_write_seconds,sender_*,receiver_*,host_baseline_csv,storage_bench_csv,source_sha256,dest_sha256,result,server_log,client_log,server_env_before_log,server_env_after_log,client_env_before_log,client_env_after_log,server_hostname,client_hostname,server_kernel,client_kernel,server_cpu_flags,client_cpu_flags,server_fs_type,client_fs_type,server_free_bytes,client_free_bytes
```

实际 CSV 还会附加 `transfer_id`、端口、临时路径和错误摘要。`throughput_gbps` / `elapsed` 优先使用接收端语义：STOR 取 server receiver，RETR 取 download client receiver。RETR sender 的 `verified_bytes` 表示 sender 已确认/发送的 verified range 字节；download client 的 `verified_bytes` 表示接收端本地校验完成字节。Phase 4J 起 CSV 同时保留 `sender_*` / `receiver_*` 双侧阶段字段，便于同一 RETR row 同时分析 sender read/send 与 receiver write/final verify。Phase 4L 起 summary CSV 对数值字段追加 `*_spread_pct` 和近似 `*_p95`，并输出 `unstable_spread_gt_20pct`、`unstable_minmax_outlier`、`stage_throughput_mismatch` 与 `repeat_count`。

Phase 4B host baseline CSV 字段：

```text
timestamp,side,category,tool,bytes,elapsed_seconds,throughput_gbps,checksum_backend,hostname,kernel,cpu_flags,fs_type,free_bytes,log,result,error
```

Checksum benchmark CSV 字段：

```text
timestamp,hostname,algorithm,backend,bytes,iterations,buffer_size,elapsed_seconds,throughput_gbps,checksum,result,log
```

Storage benchmark CSV 字段：

```text
timestamp,side,operation,bytes,iterations,buffer_size,preallocate,file_io_backend,file_io_buffer_size,posix_write_strategy,posix_write_strategy_effective,file_io_queue_depth,file_io_batch_size,file_io_advice,iteration,aggregate,elapsed_seconds,throughput_gbps,read_call_count,write_call_count,avg_read_bytes_per_call,avg_write_bytes_per_call,file_io_wait_seconds,write_syscall_count,write_retry_count,write_short_count,write_zero_count,write_total_bytes,write_avg_bytes_per_syscall,io_uring_submit_count,io_uring_wait_count,io_uring_completion_count,io_uring_sqe_count,io_uring_partial_completion_count,io_uring_retry_count,io_uring_avg_bytes_per_sqe,hostname,kernel,fs_type,free_bytes,path,log,result,error
```

Storage bench wrapper 同时生成 `*-summary.csv`，按 side/operation/bytes/buffer/preallocate/file_io_backend/file_io_buffer_size/file_io_queue_depth/file_io_batch_size/file_io_advice/posix_write_strategy/effective strategy 分组统计 throughput/elapsed 的 min、median、max。Phase 4I 起 summary 还聚合 io_uring submit/wait/completion/SQE/partial/retry/avg bytes per SQE，以及 POSIX write syscall 计数/重试/短写/零写/平均 syscall 字节数的 min、median、max。

## Phase 6D Data TLS Smoke

Phase 6D data TLS is a security smoke path, not a performance matrix. It wraps
only STOR/RETR framed file data sockets; LIST/NLST listing data remains
plaintext metadata.

Loopback:

```bash
python3 tools/test/run_gridftp_data_tls_smoke.py --build-dir build
```

Private:

```bash
python3 tools/test/run_gridftp_data_tls_private_once.py \
  --remote <remote> \
  --server-host <server-host> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir <remote-root>/build \
  --output-dir tools/perf/results
```

The smoke verifies STOR/RETR data TLS, plaintext data-client failure, tree
upload/download over data TLS, and LIST/NLST compatibility with the existing
plaintext listing data channel.

## Beta Matrix Draft

`tools/experiments/beta_matrix/` holds the local planning and dry-run layer for
the CPNetFlux Beta integration work. It reads the FDT HPC manifest, builds the
method matrix, and writes the fixed report set listed in
`docs/perf/BETA_MATRIX.md`. It does not run the cloud experiment itself.
