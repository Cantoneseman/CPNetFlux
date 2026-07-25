# Phase 4C Storage Notes

Date: 2026-05-16

Phase 4C keeps the existing epoll + `pread/pwrite` framed STOR/RETR path. It does not introduce io_uring, raw FTP STOR/RETR, TLS/GSI, MLST/MLSD, or multi-file sync.

## Implemented

- Added native `gridflux-storage-bench`.
  - Uses `PosixFile::readAtAll` / `writeAtAll`.
  - Supports `write`, `read`, `rewrite`, `all`.
  - Supports `--preallocate off|full`.
  - Emits CSV-friendly `key=value` lines.
- Added `tools/benchmark/run_storage_bench.py`.
  - Supports local, remote, and both sides.
  - Writes CSV and raw logs under `tools/perf/results/`.
- Added temp preallocation option.
  - `gridflux-file-server --preallocate off|full`.
  - `gridflux-gridftp-server --preallocate off|full` for STOR temp files.
  - `gridflux-file-download-client --preallocate off|full` for RETR download temp files.
  - Default remains `off`.
- Extended `tools/perf/run_gridftp_private_matrix.py`.
  - `--repeat N`.
  - `--preallocates off,full`.
  - `--final-verify-policies full,verified_chunks`.
  - `--storage-bench-csv <path>`.
  - Raw CSV includes `repeat_index` and `preallocate`.
  - Summary CSV reports min/median/max throughput and elapsed time.
- Hardened final verify eligibility tests.
  - checksum `none` cannot use `verified_chunks`.
  - missing ranges cannot use `verified_chunks`.
  - manifest flush failure cannot enter `verified_chunks` commit.

## Local Smoke

Build and full CTest:

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

Result:

```text
126/126 passed
```

Native storage bench smoke:

```bash
./build/gridflux-storage-bench \
  --path /tmp/gridflux-storage-bench-smoke.bin \
  --mode all \
  --bytes 1048576 \
  --buffer-size 65536 \
  --iterations 1 \
  --preallocate off
```

Output summary:

```text
write pass
read pass
rewrite pass
```

Wrapper smoke CSV:

```text
tools/perf/results/20260516T163703Z_storage-bench.csv
```

## Required Private Sampling

The following commands are the Phase 4C acceptance sampling set. The storage bench has been run once; the repeat matrix is intentionally larger and should be run in a performance test window.

Native 1GiB storage bench, both machines:

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build \
  --remote-build-dir /root/projects/GridFlux/build \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off,full \
  --buffer-sizes 1048576 \
  --iterations 1 \
  --output-dir tools/perf/results
```

Observed CSV:

```text
tools/perf/results/20260516T164109Z_storage-bench.csv
```

Observed summary:

| side | operation | preallocate | throughput_gbps |
|---|---:|---:|---:|
| local | write | off | 1.57415 |
| local | read | off | 0.723392 |
| local | write | full | 1.26286 |
| local | read | full | 76.8183 |
| remote | write | off | 3.6981 |
| remote | read | off | 22.7765 |
| remote | write | full | 1.01162 |
| remote | read | full | 79.7133 |
 
Interpretation: the native benchmark continues to point at write/landing behavior as the first storage bottleneck. `preallocate=full` did not improve write throughput in this sample, so it remains opt-in.

Private repeat matrix:

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
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

Observed CSV:

```text
tools/perf/results/20260516T164246Z_gridftp-private-matrix-smoke.csv
tools/perf/results/20260516T164246Z_gridftp-private-matrix-smoke-summary.csv
```

Observed result: 48/48 pass.

Median summary:

| direction | checksum | preallocate | requested final verify | effective | median Gbps |
|---|---|---|---|---|---:|
| STOR | crc32c | off | full | full | 1.405850 |
| STOR | crc32c | off | verified_chunks | verified_chunks | 1.531670 |
| STOR | crc32c | full | full | full | 1.325350 |
| STOR | crc32c | full | verified_chunks | verified_chunks | 1.467300 |
| STOR | none | off | full | full | 1.511650 |
| STOR | none | off | verified_chunks | full | 1.473280 |
| STOR | none | full | full | full | 1.354420 |
| STOR | none | full | verified_chunks | full | 1.472230 |
| RETR | crc32c | off | full | full | 3.322600 |
| RETR | crc32c | off | verified_chunks | verified_chunks | 4.250160 |
| RETR | crc32c | full | full | full | 3.706560 |
| RETR | crc32c | full | verified_chunks | verified_chunks | 3.968780 |
| RETR | none | off | full | full | 4.689760 |
| RETR | none | off | verified_chunks | full | 3.291530 |
| RETR | none | full | full | full | 4.088020 |
| RETR | none | full | verified_chunks | full | 3.581370 |

Interpretation:

- STOR remains write-path limited. `checksum=none` and `crc32c` are close, and preallocation did not consistently improve the median.
- RETR benefits from opt-in `verified_chunks` only when checksum is enabled; checksum `none` correctly falls back to full final verify.
- `preallocate=full` is not a default candidate from this sample.

Resume regressions:

```bash
ctest --test-dir build -R "gridftp|resume|checksum|download|Manifest" --output-on-failure
```

## Current Decision

- Keep `final_verify_policy=full` as default.
- Keep `preallocate=off` as default.
- Use repeat median, not best single run, for Phase 4C conclusions.
- Do not start io_uring implementation from Phase 4C data alone. Current median data still points first to storage write/landing behavior and final verify policy.

## Risk Boundary

- `--preallocate full` is explicit. If `posix_fallocate` fails, the case fails.
- `verified_chunks` is still opt-in. It is not enabled for checksum `none`, incomplete coverage, missing ranges, or failed manifest flush.
- Final sha256 in perf scripts remains an acceptance check, not an internal recovery fact source.
