# Milestone38: Bounded queued receiver for experimental mmap download

Date: 2026-11-10. Input commit: `70c3d65182c45ccd079bb9a2c3c7b7f757f8ac14`.
R770-1 is the sole writer; R770-2 accepts only a fixed commit.
Status: queued mmap candidate was tested on five 1 GiB pairs, rejected for a 13.65% median throughput loss and higher receiver CPU, then reverted. See `2026-11-10-v3-receiver-pipeline-38-result.md`.

## Scope

Allow the existing V3 receiver payload queue to hand CRC-verified 256 KiB DATA
frames from the socket/CRC lane to the existing mapped writer in the explicitly
experimental mmap download path. Keep queue depth fixed at 2 and preserve the
existing total payload (64 MiB) and mapped-window (256 MiB) budgets. The existing
inline mapped path is the control arm; queued mapped receive is the candidate.

## Non-goals

- No DATA frame, protocol negotiation, CRC algorithm, default behavior, or RDMA change.
- No larger queue/depth, lane-count sweep, extra buffering, NIC/system setting, or 8 GiB run.
- No removal or reordering of CRC, lease, checkpoint, recovery, publication, or final integrity checks.
- No claim that 1 GiB acceptance closes the blocked 8 GiB M1 or establishes 100 Gbps.

## Acceptance

1. The mmap experiment accepts queued receive only with four TCP lanes, plain TCP,
   POSIX direct mmap writes, depth 2, no receiver writer pool/concurrency, no write
   batching, no preallocation, and non-durable commit; invalid shapes fail before
   data transfer. Inline behavior remains supported.
2. Existing mmap fault and truncate/resume smoke covers queued handoff as well as
   inline. Failed or partial frames are never certified; the writer is joined before
   completion, recovery, or storage release.
3. R1/R2 build and relevant unit/CTest gates pass on the exact pushed commit. Three
   runtime program SHA-256 values match before the experiment.
4. Run at most five interleaved 1 GiB pairs, inline versus queued, with the existing
   fixed tmpfs/TCP/CRC32C/verified_chunks contract and all source-residency, swap,
   integrity, cleanup, route, and interface evidence gates. No instrumentation in
   timed runs. Keep every sample.
5. Retain only if candidate has at least 4/5 non-regressing pairs, median task rate
   is not below inline beyond observed noise, CV is no worse than the 5% route gate,
   and receive-missed counters show no material candidate-side increase. Otherwise
   revert the candidate and report the negative result.

## Files

- `src/core/io/persistent_tree_transfer.cpp`
- `tools/test/run_mapped_download_config_smoke.py`
- `tools/test/run_mapped_download_fault_smoke.py`
- `tools/perf/run_small_file_baseline.py`
- `tools/perf/run_large_data_ab.py`
- `docs/ROADMAP.md`, `docs/README.md`, `README.md`
- This task and its result document.

## Resource and evidence boundaries

Use `/home/Su/experiments/CPNetFlux/2026-11-10-v3-receiver-pipeline-38` and a
new marked `/dev/shm/cpnetflux-s0-receiver-pipeline-38` root. Do not touch existing
experiment roots. Require MemAvailable >= 12 GiB, owned swap 0, host pswpout delta
0, resident 1 GiB source, and at least 3 GiB tmpfs free before starting. Stop and
clean exact marked roots on any failure.
