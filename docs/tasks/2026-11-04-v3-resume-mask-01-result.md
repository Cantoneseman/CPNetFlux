# V3 bounded resume-mask correction (2026-11-04)

## Verdict

The wire-size defect is corrected and covered by regression tests. The full 64 GiB file gate is still NO-GO: the repaired four-channel download now accepts 32 GiB instead of failing at zero payload, then fails with Work Request Flushed Error. This is not a successful 64 GiB performance result.

Input: `fb447d995a318ec7c04fce3dd3e1baf32371eb8e`. Measured source: `853f1187c7a03e8de2c4186704fd61f2b3a99c42`, branch `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`. The source commit was pushed to the verified GitHub remote and its SHA read back before peer synchronization.

## Change

- Adjacent missing chunks are merged only in the outgoing resume response. An 8192-entry contiguous mask becomes one interval: payload 131080 -> 24 bytes, within the unchanged 65536-byte frame limit.
- Internal chunk boundaries, CRC records, checkpoint format and atomic publication remain intact. The existing sender already splits DATA at chunk boundaries.
- Wire metadata allocation is bounded to 4095 intervals. Verified holes are preserved. More than 4095 disjoint intervals, malformed intervals or invalid range bounds produce a small InvalidFrame rejection and a local error.
- A receiver that cannot send a valid resume response returns immediately instead of waiting for DATA. Irreducibly fragmented resume still has an explicit limit; pagination is not implemented and the checkpoint is retained.

## Validation

- Old implementation: four new behavioral tests failed for the expected wire-limit/coverage/error-response reasons.
- New implementation: six new tests plus 12 existing range tests passed (18/18). Coverage includes 8192 adjacent entries, holes, 4095/4096 disjoint entries, malformed masks, an actual 8192-chunk CRC transfer, and a fragmented resumed checkpoint that exits promptly while preserving all 4096 verified records.
- Full CMake/CTest on R770-1: 489 executed passes, zero failures/skips, 228.77 s. R770-2: 489 executed passes, zero failures/skips, 229.74 s.
- Explicit RDMA duplex hardware smoke passed on both nodes (6.87/6.88 s). The first R1 gate omitted the required temporary token and had two auth/event-log failures plus an unconfigured hardware skip; the complete-environment rerun resolved them.
- Both nodes used the measured commit, matching RelWithDebInfo/RDMA/TLS/ISA-L/io_uring configuration and matching SHA-256 for server, upload/download clients, RDMA bench and unit-test binary. See `acceptance/matching-builds.json`.
- Commands: `cmake --build build -j 6`; `build/cpnetflux_unit_tests --gtest_filter=PersistentResumeMaskTest.*:PersistentTreeRangeTest.*`; `python3 <evidence>/scripts/gates.py r1` / `r2`.

## Fixed-build file rerun

One diagnostic attempt per size, four connections, one file, native RDMA SEND/RECV explicitly selected; control remains TCP. Client binds 192.168.100.1, server 192.168.100.2. Routes and eno17095np0/mlx5_0 counters were captured. The baseline's repeated-random source bytes, server-side 1 MiB download chunks and receiver policy were retained. Existing RDMA diagnostics were enabled, so these attempts do not replace a three-repeat performance matrix.

| Metric | 4 GiB download | 64 GiB download |
|---|---:|---:|
| Result | PASS, external SHA-256 matches | FAIL, no published file / no whole-file SHA success |
| Logical bytes accepted | 4294967296 | 34359738368 (32 GiB) |
| Completed files | 1 | 0 |
| External timing | driver 15.135 s; process 15.09 s | process 477.06 s |
| Successful task rate | 2.270 Gbps (driver clock) | N/A |
| Payload active-span estimate | 2.969 Gbps | N/A for failed file |
| Recorded range completions | 8 across channels 0/1/2/3 | 4 x 8 GiB across channels 0/1/2/3 |
| Recovery attempts | 0 | 0 |

Payload active-span uses the same sampled HCA activity-span method as the baseline; it is not a per-lane sum or a precise packet-level steady-state timer. The failed client's printed 1.15242 Gbps uses configured bytes despite partial completion and must not be treated as achieved throughput.

The four completed 64 GiB-attempt ranges belong to one file, not four independent memory tasks. Their wall-time sum is 1841.641 s within a 477.06 s process interval, implying at least 410.461 s of all-four overlap. Four worker QPs plus the existing production QP were observed.

## Newly exposed long-transfer evidence

The 64 GiB attempt did not reach full-file final verification; it failed after the first four 8 GiB ranges. Do not conflate it with the old single-channel case that received all 64 GiB and then spent about 151 s in final verification.

Receiver observations for the four completed ranges:

- 32768 checkpoint manifest flush calls; 22061268669 serialized metadata bytes.
- Entry lock hold: 453.277 s total; checkpoint manifest flush: 421.615 s; serialization: 341.780 s; lower-level file write calls: 24.843 s.
- These timers are nested, and the contract includes deferred per-entry accounting. Do not add them together. The 1349.105 s cumulative Entry-lock wait overlaps across lanes and is not task wall time.
- Sender diagnostics record four receive waits of about 30.029 s followed by QP close, while the receiver continues draining/processing the first ranges to about 460 s.
- Host-counter deltas: R2 sender RNR +399996; R1 receiver out_of_buffer +399996. HCA bytes are 36778563952 R2->R1, versus 32 GiB accepted logical bytes. Close/error counters remain in raw snapshots; they are not all classified as harmless teardown.
- R1 physical disk writes: 64045678592 bytes; R2 physical reads: 33318559744 bytes. These are host-wide device counters, not isolated filesystem ceilings.

This strongly prioritizes receiver checkpoint serialization/lock contention and result-wait behavior under backpressure. A controlled stall/deadline regression and repeated policy comparison are still needed for causal acceptance; increasing a timeout alone is not the final solution.

## Next Step

1. Isolate the FILE_END / FILE_RESULT wait against a receiver that is still draining or checkpointing for longer than 30 s; separately retain the old full-file final-verification case. Require bounded task deadline, cancel/peer-death behavior and no false failure while useful progress continues.
2. Make download endpoint ownership of chunk/checkpoint/final-verify policy explicit. Measure the existing 1/4/16 checkpoint batch controls one at a time, preserving verified-record recovery and atomic publication, before queue/CQ/pipeline changes.
3. After those correctness fixes, rerun both 64 GiB download cells at least three times with external SHA-256, then resume phase-1 attribution. The original 48-sample baseline remains 42 pass / 6 fail; it has not been relabeled as fully passed.

## Evidence and Cleanup

Evidence root: `/home/Su/experiments/CPNetFlux/2026-11-04-v3-resume-mask-01/`. Entry points: `acceptance/analysis.json`, `acceptance/matching-builds.json`, `acceptance/evidence-manifest.json`, CTest JUnit/logs, `data/targeted-results.json`, per-host logs/snapshots and cleanup receipts. The retained driver and copied baseline library make the scoped experiment inspectable.

All task clients, samplers and temporary servers ended. R1 download payload/checkpoint directories were removed by the driver; R2 removed 72 GiB of owned seed/source payloads. Production server PIDs 126766/164834 remained present; production roots and unrelated projects were untouched. R1 free space returned to about 379 GiB and R2 to about 328 GiB. The two pre-existing untracked task documents remain untouched.

GitHub source backup: `853f1187c7a03e8de2c4186704fd61f2b3a99c42`. The subsequent documentation closeout commit contains this result and current-route status; raw logs/payloads are not Git contents.
