# Phase 4D File IO Notes

Date: 2026-05-17

Phase 4D keeps the existing epoll + framed STOR/RETR path. It does not introduce io_uring, raw FTP STOR/RETR, TLS/GSI, MLST/MLSD, or multi-file sync.

## Implemented

- Added a lightweight file IO layer:
  - `FileIoBackendKind { Posix }`
  - `FileIoAdvice { off, sequential, noreuse, dontneed, sequential_dontneed }`
  - `FileIoConfig`
  - `FileIoStats`
- Kept `PosixFile` compatible and added concrete POSIX helpers around `pread` / `pwrite` / `posix_fadvise`.
- Routed STOR temp writes, upload source reads, RETR source reads, and download temp writes through the file IO helper.
- Added explicit CLI options:
  - `--file-io-backend posix`
  - `--file-io-buffer-size <N>`
  - `--file-io-advice <mode>`
- Added per-transfer file IO metrics:
  - `stage_read_calls`
  - `stage_write_calls`
  - `stage_read_avg_bytes_per_call`
  - `stage_write_avg_bytes_per_call`
  - `file_io_wait_seconds`
  - `file_io_wait_bytes`
- Extended `gridflux-storage-bench`:
  - per-iteration raw lines
  - aggregate line
  - call count and average bytes per call
  - `--file-io-advice`
- Extended perf scripts:
  - `tools/benchmark/run_storage_bench.py` writes raw CSV and summary CSV.
  - `tools/perf/run_gridftp_private_matrix.py` records file IO config and call metrics.

## Defaults

- `file_io_backend=posix`
- `file_io_buffer_size=0`
- `file_io_advice=off`
- `preallocate=off`
- `final_verify_policy=full`

These defaults preserve Phase 4C behavior. `verified_chunks`, preallocation, buffering, and advice remain explicit diagnostic or optimization switches.

## Local Validation

Build and full CTest:

```text
cmake --build build
ctest --test-dir build --output-on-failure
```

Result:

```text
130/130 passed
```

Storage bench smoke:

```bash
./build/gridflux-storage-bench \
  --path /tmp/gridflux-storage-phase4d-smoke.bin \
  --mode all \
  --bytes 1048576 \
  --buffer-size 65536 \
  --iterations 2 \
  --preallocate off \
  --file-io-advice off
```

Observed output included per-iteration and aggregate rows. Example aggregate:

```text
storage_bench operation=write ... write_call_count=32 avg_write_bytes_per_call=65536 result=pass
storage_bench operation=read ... read_call_count=32 avg_read_bytes_per_call=65536 result=pass
```

Wrapper smoke:

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side local \
  --build-dir build \
  --bytes 1048576 \
  --modes write,read \
  --preallocates off \
  --file-io-advices off,sequential \
  --buffer-sizes 65536 \
  --iterations 2 \
  --output-dir tools/perf/results
```

Observed:

```text
tools/perf/results/20260516T180223Z_storage-bench.csv
tools/perf/results/20260516T180223Z_storage-bench-summary.csv
cases=12 failures=0
```

## Remote And Script Smoke

Machine two was synced with `tools/perf/sync_remote.sh`; the known historical directory
`build-private-verify-20260515T163633Z` was left untouched. Remote configure, build, and
full CTest passed:

```text
130/130 passed
```

A small private matrix smoke validated the new file IO matrix parameters and CSV fields across
STOR and RETR:

```text
tools/perf/results/20260516T181048Z_gridftp-private-matrix-smoke.csv
tools/perf/results/20260516T181048Z_gridftp-private-matrix-smoke-summary.csv
cases=4 failures=0
```

This 1 MiB smoke is only a wiring check. It is not used as a Phase 4D storage-performance
median conclusion.

## Private Sampling Command

Run in a performance window after syncing/building machine two:

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build \
  --remote-build-dir /root/projects/GridFlux/build \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off,full \
  --file-io-advices off,sequential \
  --buffer-sizes 65536,262144,1048576,4194304 \
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
  --final-verify-policies full,verified_chunks \
  --file-io-buffer-sizes 0,1048576 \
  --file-io-advices off,sequential \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

Report median throughput, median elapsed, `stage_write_calls`, `stage_read_calls`, and average bytes per call. Do not use best single run as the Phase 4D conclusion.

## Current Judgment

- Phase 4D prepares the file IO boundary for io_uring without implementing it.
- The immediate decision remains data-driven: if median file IO call metrics show small calls or high wait time dominate, continue POSIX storage tuning; if POSIX path still spends most time in syscall/wait despite large calls and stable storage behavior, start an io_uring backend design.
- `file_io_buffer_size=0` and `file_io_advice=off` stay default until repeat median data justifies changing defaults.

## Risk Boundary

- The POSIX backend is concrete and selected by enum/config; no virtual dispatch is added to the data path.
- Write coalescing only merges contiguous DATA for the same connection/chunk and flushes before `ChunkComplete` is recorded.
- `posix_fadvise` is explicit; if a non-off advice mode fails, the case fails.
- `verified_chunks` remains opt-in and is not enabled for checksum `none`, missing ranges, or failed manifest flush.
