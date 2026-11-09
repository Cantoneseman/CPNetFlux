# V3 Small File Baseline21

Date: 2026-11-09. Input `8c887f09aea2847e68fdd6fc494fd7b52667ca04`.
Status: development. R770-1 is the sole serial writer; R770-2 is a fixed-commit peer.

## Scope And Non-Goals

Execute S0 after bounded LF4/LF5 candidates failed their local gates. Add a reusable
small-file experiment driver, validation tests and a compact result. Do not modify
runtime/protocol/defaults, increase queues/threads, reopen old candidates, change
THP/mount/network/NUMA, run GridFTP or resume RDMA optimization.
No subagents, worktrees, desktop or production-service changes.

Files: this task/result; `tools/perf/run_small_file_baseline.py`,
`tools/test/run_small_file_baseline_test.py`, CMakeLists.txt;
ROADMAP, large-file-next-plan and route decision status. Preserve October14 docs.

## Configuration And Matrix

R770-2 source -> R770-1 fresh output on tmpfs; TCP explicitly bound to
192.168.100.2/.1, eno17095np0. Four connections/file workers, 1MiB chunks,
64KiB buffers, CRC32C/auto, V3 work leases, queued sender/receiver, POSIX direct,
read/write concurrency 0, depth2, payload budget64MiB, no compression/TLS/preallocate,
verified_chunks, checkpoint16, commit_sync=none. Normal pwrite receiver, not the
large-file-only mmap experiment. These are nondurable memory-file results.

- Baseline: explicit pending-window auto; seven size/count cases, three repeats:
  4/16/64/256KiB x1K and 4/16/64KiB x10K. 256KiB x10K exceeds1GiB and is excluded.
- Initial assumption (invalidated below): auto selects2 for1K and8 for dense10K. Verify actual policy
  and selection; download receiver high-watermark0 is not sender observed depth.
- At4KiB x10K add three interleaved fixed-window2 samples, using same commit/config.
  Window1 is not a performance baseline. This is contextual validation of existing
  auto, not a new optimization or production-default proposal.
- At4KiB x1K alternate three phase-timing on/off pairs. Receiver detail timers may
  be costly; sender basic file timing is already always emitted for V3 directories.
  Compare full task overhead, retain separate scopes; don't subtract overlapping sums.
- Baseline phase timing off, detailed range/profiles off on both endpoints. Server
  basic timings remain common. Use existing coarse stage fields before adding new
  runtime observation. No uncontrolled parameter sweep or microbatch implementation.

## Measurement, Correctness And Resources

T_ready is local client spawn through successful exit, including remote scan, V3
setup, payload, CRC/checkpoint/publish and summary output. Server process readiness,
source generation/warming, independent SHA and experiment cleanup are outside and
recorded separately. Clock sums from different files/threads are not task wall.
Three-repeat p95 is descriptive only, not a tail-latency acceptance claim.

Check tree members/bytes/individual SHA via canonical tree digest, no unexpected
payload names, completed/failed/changed counts, actual_transport=v3 and explicit TCP.
Verify committed member and tree manifests before removing each owned output slot.
Preserve summary plus compressed original evidence, compact file-phase aggregates
and representative timings; don't keep payload or giant uncompressed file arrays.

Both nodes must have same fixed committed source/configuration and matching server,
upload/download binary SHA and version before any transfer. Build and relevant
CMake/CTest gates first; push and read back the commit before sync to R2. R2 never
edits source. Existing dirty unrelated docs on R1 remain untouched.

Each source/output<=1GiB; MemAvailable>=12GiB, owned VmSwap0, host pswpout delta0,
supervised on both nodes. Stop owned process on breach; identify by exact run
root/PID/start ticks. Never stop production126766/1191040 or164834/1679976.
Use marked, newly created tmpfs roots; cleanup verifies marker and containment,
does not follow symlinks or accept filesystem roots. Preserve failed-run evidence.
Record routes, interface before/after counters and owned TCP snapshots. Global
interface deltas are not connection attribution; scope17 rx_missed_errors+94 stays open.

Evidence: `/home/Su/experiments/CPNetFlux/2026-11-09-v3-small-file-baseline-21`.
Acceptance: driver boundary/cleanup/aggregation tests, existing V3 tree correctness/
resume/pending-window tests, fixed peer receipts, revised 33 samples (21 auto +3 timing
+9 fixed-window8) with complete integrity/resource/cleanup outcomes. No throughput
gain gate for a baseline; any failed/unstable sample is retained and qualified.

After attribution, design member microbatch only if fixed work dominates. Fixed
and adaptive remain separate future candidates: T_ready median reduction>=20%,
p95/recovery no regression and large-file regression<=5%. Large-file ~27.3Gbps
baseline/100G shortfall are unchanged by small-file results.

## Commands And Handoff

Build the server, tree upload/download and unit-test targets with the existing
RelWithDebInfo, tests/RDMA/TLS/ISA-L/io_uring ON, desktop/cross-domain tests OFF.
Run `python3 tools/test/run_small_file_baseline_test.py` and:

```sh
ctest --test-dir build --output-on-failure -R '^cpnetflux_(small_file_baseline_driver_test|tree_pending_window_smoke|tree_receiver_policy_smoke|tree_manifest_coalesce_smoke|tree_control_reuse_smoke|v3_unified_executor_smoke|v3_interrupted_resume_smoke|v3_corrupt_partial_checkpoint_smoke|tree_edge_cases_smoke)$'
python3 tools/perf/run_small_file_baseline.py --action matrix \
  --evidence /home/Su/experiments/CPNetFlux/2026-11-09-v3-small-file-baseline-21 \
  --payload-root /dev/shm/cpnetflux-s0-20261109-21 --commit FIXED_SHA
```

Replace FIXED_SHA with the pushed source commit, compare both receipt actions
before matrix. A 32-file plumbing smoke may precede the matrix and is excluded
from performance samples. Driver exceptions preserve receipts; inspect owned
supervisor PID/start and marked children before explicit cleanup/retry. Never
reuse a failed sample name or silently discard its evidence. Final result records
source/result SHAs, remote readback, resource/integrity outcomes and next scope.

## S0 Revision1: Fresh Download Auto Lacks Source Statistics

The cb6c8bd attempt stopped on the first4KiB x10K sample: the client completed
10K files but the driver's expected-auto8 assertion failed before independent
integrity acceptance. Earlier samples/evidence are retained and excluded from
the revised formal matrix. Both owned roots were emptied and removed.
Source inspection: `runTreeDownloadClient` starts with an empty manifest;
`selectPersistentDispatchWindow` runs before remote scan/prepare, sees zero files
and selects2. The >=10K rule works only with an already populated manifest.
Do not call fresh-download auto8 tuned or silently force it in the runtime.

This revision changes only the driver/measurement scope: record existing auto2,
compare explicit8 at all three10K sizes, three interleaved pairs each. Total33
samples in evidence subdirectory `revision1`, all under a new fixed commit;
repeat the1K cases to keep the formal matrix on that commit. Runtime binaries
remain unchanged. The former fixed2 comparison is superseded because it would
compare the same selected window. Auto policy correction requires its own
design/correctness scope after baseline; pending window is not member microbatch.

Also inspect `summary_tree_hash_seconds/source`: early samples show JSON summary
performs a separate filesystem CRC scan below128MiB. It is included in T_ready;
the client's elapsed_seconds stops before that scan and cannot substitute for
T_ready. Attribute this measured work before choosing a protocol microbatch.
