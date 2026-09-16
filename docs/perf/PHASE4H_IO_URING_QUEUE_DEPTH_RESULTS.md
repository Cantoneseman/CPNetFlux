# Phase 4H io_uring Queue Depth Results

Phase 4H implemented an optional file-IO-only io_uring queue depth / batching prototype. It does not change network epoll, STOR/RETR framed data, checksum, manifest, resume, or final verify semantics. The default remains `file_io_backend=posix`.

## Implementation

- `FileIoConfig` now carries `queueDepth` and `batchSize`, defaulting to `1`.
- CLI options were added across file server/client/download client, GridFTP control server, and storage bench:
  - `--file-io-queue-depth <N>`
  - `--file-io-batch-size <N>`
- Legal range is `1..256`. If queue depth is specified and batch size is omitted, batch size follows queue depth.
- POSIX records these values for CSV comparability but does not change behavior.
- io_uring `readAtAll` / `writeAtAll` can split one contiguous request into multiple SQEs and submit up to `min(batch_size, queue_depth)` per round.
- `FileIoStats` and CSV logs now include submit/wait/completion/SQE/partial/retry counts and average bytes per SQE.

## Validation

Local default Debug build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

Result: `139/139` passed. In the default build, `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` is expected to be skipped because `CPNETFLUX_ENABLE_IO_URING` is off.

Local real io_uring Release build:

```bash
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DCPNETFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure
```

Result: `139/139` passed. `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` was `Passed`, not skipped.

Machine two validation after sync:

- Default Debug full CTest: `139/139` passed.
- `build-io-uring-real` Release full CTest: `139/139` passed.
- `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable`: `Passed`.

Public release gate:

```bash
rm -rf /tmp/cpnetflux-public
python3 tools/release/export_public_repo.py --output /tmp/cpnetflux-public --force
python3 tools/release/check_public_hygiene.py --path /tmp/cpnetflux-public --strict
```

Result: strict hygiene passed. Export summary: `copied_files=168 skipped_files=1 skipped_dirs=11 skipped_build_dirs=7`.

## CSV Outputs

Storage bench queue-depth smoke:

- Raw CSV: `tools/perf/results/20260517T102821Z_storage-bench.csv`
- Summary CSV: `tools/perf/results/20260517T102821Z_storage-bench-summary.csv`
- Scope: local, `64MiB`, write/read/rewrite, buffers `256KiB,1MiB`, backends `posix,io_uring`, queue depths `1,4,8,16`, iterations `3`.
- Result: `192` cases, `0` failures.

GridFTP-like private queue-depth smoke:

- Raw CSV: `tools/perf/results/20260517T103037Z_gridftp-private-matrix-smoke.csv`
- Summary CSV: `tools/perf/results/20260517T103037Z_gridftp-private-matrix-smoke-summary.csv`
- Scope: `64MiB`, STOR/RETR, `4` connections, `1MiB` chunk, `64KiB` network buffer, checksums `crc32c,none`, backends `posix,io_uring`, queue depths `1,4`, repeat `1`.
- Result: `16` cases, `0` failures.

GridFTP-like private 1GiB sample:

- Raw CSV: `tools/perf/results/20260517T103220Z_gridftp-private-matrix-smoke.csv`
- Summary CSV: `tools/perf/results/20260517T103220Z_gridftp-private-matrix-smoke-summary.csv`
- Scope: `1GiB`, STOR/RETR, `8` connections, `4MiB` chunk, `256KiB` network buffer, checksums `crc32c,none`, backends `posix,io_uring`, queue depths `1,4,8,16`, repeat `1`.
- Result: `32` cases, `0` failures.
- Note: this sample was run before fixing the private matrix download-client argument pass-through. STOR queue-depth rows are valid. RETR rows only represent effective `queue_depth=1` because the download client did not receive the queue/batch options.

Post-fix RETR queue-depth smoke:

- Raw CSV: `tools/perf/results/20260517T104113Z_gridftp-private-matrix-smoke.csv`
- Summary CSV: `tools/perf/results/20260517T104113Z_gridftp-private-matrix-smoke-summary.csv`
- Scope: `64MiB` RETR, checksum none, backend `io_uring`, `queue_depth=4`, repeat `1`.
- Result: `1` case, `0` failures. The CSV records `file_io_queue_depth=4` and `file_io_batch_size=4`, confirming argument pass-through.

## Median Findings

Storage bench smoke, `64MiB`, `256KiB` buffer:

| Operation | Backend | Queue depth | Median Gbps |
|---|---:|---:|---:|
| read | posix | 1 | 69.529100 |
| read | io_uring | 1 | 52.034400 |
| read | io_uring | 4 | 54.313900 |
| read | io_uring | 8 | 53.660900 |
| read | io_uring | 16 | 51.903900 |
| rewrite | posix | 1 | 82.726100 |
| rewrite | io_uring | 1 | 43.748900 |
| rewrite | io_uring | 4 | 37.926600 |
| rewrite | io_uring | 8 | 34.249300 |
| rewrite | io_uring | 16 | 30.879300 |
| write | posix | 1 | 5.944280 |
| write | io_uring | 1 | 0.947563 |
| write | io_uring | 4 | 1.169910 |
| write | io_uring | 8 | 0.928862 |
| write | io_uring | 16 | 0.950267 |

GridFTP-like private `64MiB` smoke:

| Direction | Checksum | Backend | Queue depth | Median Gbps |
|---|---|---:|---:|---:|
| STOR | crc32c | posix | 1 | 8.981660 |
| STOR | crc32c | io_uring | 1 | 6.507210 |
| STOR | crc32c | io_uring | 4 | 6.123160 |
| STOR | none | posix | 1 | 14.790800 |
| STOR | none | io_uring | 1 | 8.991240 |
| STOR | none | io_uring | 4 | 8.599330 |
| RETR | crc32c | posix | 1 | 9.745140 |
| RETR | crc32c | io_uring | 1 | 8.028120 |
| RETR | none | posix | 1 | 14.195650 |
| RETR | none | io_uring | 1 | 9.752680 |

GridFTP-like private `1GiB` sample, repeat `1`:

| Direction | Checksum | Backend | Queue depth | Gbps |
|---|---|---:|---:|---:|
| STOR | crc32c | posix | 1 | 0.701795 |
| STOR | crc32c | posix | 4 | 1.407130 |
| STOR | crc32c | posix | 8 | 1.198270 |
| STOR | crc32c | posix | 16 | 1.386200 |
| STOR | crc32c | io_uring | 1 | 1.408600 |
| STOR | crc32c | io_uring | 4 | 1.001700 |
| STOR | crc32c | io_uring | 8 | 1.249060 |
| STOR | crc32c | io_uring | 16 | 1.301210 |
| STOR | none | posix | 1 | 1.417540 |
| STOR | none | posix | 4 | 1.461400 |
| STOR | none | posix | 8 | 1.481780 |
| STOR | none | posix | 16 | 1.478420 |
| STOR | none | io_uring | 1 | 1.505840 |
| STOR | none | io_uring | 4 | 1.481110 |
| STOR | none | io_uring | 8 | 1.369380 |
| STOR | none | io_uring | 16 | 1.426310 |

The 1GiB RETR pre-fix sample is not used for queue-depth conclusions because the download client did not receive queue/batch options before `tools/perf/run_gridftp_private_matrix.py` was fixed. The post-fix 64MiB RETR qd=4 smoke passed and produced correct CSV fields.

Phase 4I follow-up: the storage bench wrapper was also fixed so `--side local` no longer probes or cleans remote paths. Phase 4I then reran the corrected 1GiB repeat=3 private matrix with RETR queue/batch pass-through active. Final queue-depth gate conclusions are in `docs/perf/PHASE4I_HEAVY_QUEUE_DEPTH_GATE.md`; this Phase 4H 1GiB RETR sample remains historical context only.

## Conclusions

- Correctness gate passed: both local and machine two full CTest pass, and real io_uring smoke is `Passed`.
- Public export strict hygiene remains green.
- Queue depth / batching is correctly exposed through storage bench, GridFTP control server, upload client, download client, raw CSV, and summary CSV.
- In the current Phase 4H smoke data, queue depth does not show a stable performance win over POSIX. Storage bench especially favors POSIX for read/rewrite and write at these sizes.
- STOR 1GiB repeat=1 shows some io_uring qd=1 parity or improvement in selected checksum cases, but the sample is not enough to justify a default change.
- POSIX remains the default. io_uring remains opt-in.

## Next Recommendation

Proceed to Phase 4I only if the goal is a narrower io_uring investigation:

- rerun the full requested heavy sampling with `1GiB`, repeat `3`, and the fixed RETR queue-depth pass-through;
- consider increasing per-SQE size or using a real persistent ring/context instead of creating a ring per `readAtAll` / `writeAtAll` call;
- keep network epoll unchanged and keep `file_io_backend=posix` as default.

If Phase 4I is not pursued, return to POSIX storage/writeback tuning and longer repeat sampling. Do not default-enable `io_uring`, `verified_chunks`, `preallocate=full`, file IO buffer, or file IO advice based on Phase 4H data.
