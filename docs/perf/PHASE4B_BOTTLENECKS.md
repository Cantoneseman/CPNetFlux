# Phase 4B Bottleneck Notes

Date: 2026-05-16

Phase 4B did not introduce io_uring. The goal was to instrument the current epoll + `pread/pwrite` framed STOR/RETR path, collect host/link baselines, and apply only low-risk reliability-preserving optimizations.

## Artifacts

- Host/link baseline CSV: `tools/perf/results/20260516T155847Z_host-baseline.csv`
- 64MiB STOR/RETR smoke CSV: `tools/perf/results/20260516T155955Z_gridftp-private-matrix-smoke.csv`
- 16MiB STOR/RETR resume CSV: `tools/perf/results/20260516T160009Z_gridftp-private-matrix-smoke.csv`
- 1GiB full final verify CSV: `tools/perf/results/20260516T160024Z_gridftp-private-matrix-smoke.csv`
- 1GiB RETR verified_chunks opt-in CSV: `tools/perf/results/20260516T160621Z_gridftp-private-matrix-smoke.csv`

## Host And Link Baseline

`iperf3` / `fio` were not assumed. This run used GridFlux memory sink for the network baseline and Python sequential IO fallback for disk.

| Category | Tool | Result |
|---|---:|---:|
| Network memory sink | `gridflux-server/client` | 19.1192 Gbps |
| Server sequential write | Python fallback | 1.033312 Gbps |
| Server sequential read | Python fallback | 0.940298 Gbps |
| Client sequential write | Python fallback | 1.031349 Gbps |
| Client sequential read | Python fallback | 29.511012 Gbps |
| Server CRC32C auto | hardware | 47.5799 Gbps |
| Client CRC32C auto | hardware | 47.6092 Gbps |

Interpretation: the private link and CRC32C backend are not the first-order explanation for the 1GiB drop. Large transfer behavior tracks the write/flush path much more closely.

## Stage Breakdown

64MiB CRC32C smoke stayed near Phase 4A levels:

| Direction | Connections | Throughput | Notes |
|---|---:|---:|---|
| STOR | 1 | 7.92551 Gbps | final verify ~0.021s |
| STOR | 4 | 7.93660 Gbps | final verify ~0.020s |
| RETR | 1 | 8.17687 Gbps | final verify ~0.020s |
| RETR | 4 | 7.94194 Gbps | final verify ~0.020s |

16MiB resume smoke passed:

| Direction | Throughput | Notes |
|---|---:|---|
| STOR resume | 6.99019 Gbps | partial + REST/GFID resume |
| RETR resume | 6.48152 Gbps | download manifest resume |

1GiB with `final_verify_policy=full`:

| Direction | Checksum | Throughput | Key Stage Times |
|---|---:|---:|---|
| STOR | crc32c | 1.44590 Gbps | write 4.856s, checksum 0.191s, final verify 0.324s |
| STOR | none | 1.48136 Gbps | write 5.353s, checksum ~0s, final verify ~0s |
| RETR | crc32c | 1.68012 Gbps | write 3.845s, checksum 0.696s, final verify 3.024s |
| RETR | none | 5.23149 Gbps | checksum/final verify ~0s |

Opt-in `final_verify_policy=verified_chunks`:

| Direction | Checksum | Throughput | Effective Policy |
|---|---:|---:|---|
| STOR | crc32c | 1.39362 Gbps | verified_chunks |
| RETR | crc32c | 3.36272 Gbps | verified_chunks |

## Optimization Applied

- Added `TransferPhaseStats` to emit stage-level seconds/bytes from STOR server, upload client, RETR sender, and download client.
- Added `tools/perf/run_private_host_baseline.py`.
- Aligned `DownloadSession` manifest batch flush with upload `TransferSession`: default every 16 verified chunks, forced on failure/resume precheck/commit.
- Added `FinalVerifyPolicy`.
  - Default remains `full`.
  - `verified_chunks` is opt-in and only takes effect when checksum is enabled, verified chunks cover the full transfer, missing ranges are empty, and manifest has been flushed.

## Bottleneck Judgment

- Do not jump to io_uring yet. The measured memory sink link is ~19Gbps and CRC32C hardware is ~47Gbps, while 1GiB framed file throughput is 1-5Gbps depending on write/final verify policy.
- STOR 1GiB is dominated by temp file write/flush behavior; checksum none barely improves STOR, so checksum is not the primary STOR bottleneck.
- RETR 1GiB with CRC32C full verify was heavily affected by final temp reread. Opt-in `verified_chunks` roughly doubled RETR throughput in this sample, from ~1.68Gbps to ~3.36Gbps, but manifest flush and write path remain visible.
- Phase 4B keeps `full` as default until more failure-mode testing and a broader matrix justify changing the default.

## Next Recommendation

1. Run a fio-backed host baseline after installing/confirming fio externally, then repeat representative 1GiB samples.
2. Investigate storage path behavior: temp file filesystem, fsync/writeback pressure, rename/commit timing, and Python test file generation/copy path.
3. Add optional batched manifest flush by time/bytes and measure `manifest_flush_seconds`.
4. Only design io_uring once syscall/epoll overhead is shown to dominate after storage/final-verify costs are isolated.
