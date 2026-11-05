# V3 Result Lifetime 01 Result

Date: 2026-11-05. Input: 28d22ab7fbd8c3d9ec82c50ef64b0a455bb0f46d.
Measured source: 78e1e4ab7a2e08eb0fd27f3958fd8536ac75b301.
Status: code gates and six fresh 64 GiB download protocol/integrity repetitions accepted.

## Scope And Conclusion

This stage repairs the V3 result/commit lifetime and termination faults found in the failed 64 GiB download baseline. It does not optimize receiver verification/checkpoint defaults, change system networking, implement a new pipeline, or establish production readiness. The original 48-sample baseline remains 42 PASS / 6 FAIL; the fixed-source repetitions below are a separate acceptance set.

## Implementation

Live, drained V3 work can enter CommitPending with one explicit finite result deadline. The persistent runtime selects min(now + 5 minutes, configured task deadline). The 30 second payload idle lease is unchanged. Expired/reclaimed/cancelled leases, outstanding IO, owner/epoch/attempt mismatches and repeated result-phase extension remain rejected. Existing queue callers without an explicit deadline retain their previous semantics.

The receiver now records DATA IO progress and enters CommitPending before range completion/full verification. A shared per-lane lifetime guard tracks outstanding result deadlines, cancels a blocked socket on expiry, and checks the deadline after local commit/callback. Blocking filesystem operations remain cooperative: socket cancellation cannot preempt an uninterruptible read/fsync or undo a file already published. The runtime reports failure instead of accepting late success.

TCP/TLS result reads use the result budget, with the original receive timeout restored before a subsequent resume handshake. RDMA CQ waits inspect atomic shutdown during bounded event poll slices. The client reads its directory control terminal concurrently with data execution; control EOF interrupts the data socket, while a successful terminal still requires data-protocol completion. After data stops, terminal wait is bounded by 60 seconds and any earlier task deadline.

Recovery preserves two distinctions: a generic remote 550 describing a data disconnect must not convert the data fault into a non-retryable error; successfully completed data with a lost terminal reply must retain its success so existing outer-batch recovery runs. The first implementation violated these distinctions; the retained failing gates led to fixes before acceptance.

## Validation

Evidence root: /home/Su/experiments/CPNetFlux/2026-11-05-v3-result-lifetime-01.

- Logical-clock regression initially failed 2/4 cases. Long 172 second result completion now succeeds under the explicit 5 minute budget. Exact expiry, repeated extension, expired/busy payload, cancellation and recovered epoch fencing pass.
- Deadline-wakeup regression initially failed 2/3 cases. The actual sender with a delayed receiver result now returns the deadline error, and completed results do not expire a healthy lane.
- Receiver callback observes CommitPending and its independent result deadline. Native RDMA idle receive is explicitly shutdown-tested on bind 192.168.100.1.
- The 31 second result regression first failed with the old 30 second socket timeout, then passed after the TCP result-read fix. A final fixture uses an actual loopback TCP connection; its focused log is acceptance/loopback-tcp-long-result.log.
- Control EOF fault injection pauses a temporary native RDMA server after FILE_BEGIN and closes only the control proxy. Client exits with the control-close cause in 0.164 seconds, before its 15 second task deadline. Evidence: control-loss-01/acceptance.json. The test is registered in CTest and uses an explicit RDMA bind.
- Terminal-reply recovery passed ten consecutive repetitions, 19.12 seconds. Recovery-specific acceptance passed 14/14 before the TCP follow-up.
- Full R770-1 CTest passed 503/503, no skips, 265.12 seconds. It includes a generated in-process test credential and explicit RDMA bind; no credential is stored. The final TCP fixture upgrade is additionally tested after this full run. Build and git diff --check pass.
- R770-2 focused CTest passed 76/76, no skips, 78.81 seconds, including the final actual-TCP fixture, native shutdown/control-EOF and recovery tests. Evidence: R770-2 acceptance/ctest-r770-2.log.
- Both hosts built source 78e1e4ab with identical CPNETFLUX CMake options and version 0.1.0. SHA-256 matches for server, tree upload/download clients, RDMA bench and unit binary. Evidence: acceptance/matching-builds.json. The measured source was committed/pushed to git@github.com:Cantoneseman/CPNetFlux.git on codex/V3-GRIDFTP-OPTIMIZED-RERUN-01; GitHub branch SHA was read back equal before peer synchronization and transfer.
- Intermediate full gates retained: two recovery failures (fixed), one /proc fd-enumeration race (fixed by skipping only an already-disappeared descriptor), and the terminal/data completion race (fixed). These logs are diagnostic evidence, not accepted runs.

## Native RDMA File Acceptance

All cases use V3, explicitly selected native RDMA SEND/RECV, CRC32C, no compression, no recovery retries, RDMA-NIC binds 192.168.100.1 / 192.168.100.2, and route/interface/HCA evidence. Each download transfers one 64 GiB file from R770-2 to R770-1. One lane uses a whole-file work item; four lanes execute eight 8 GiB ranges for the same file. Both hosts initialize the required QPs and complete every data DirectoryEnd plus client control termination.

Task rate is 64 * 2^30 * 8 / client-process wall seconds / 1e9. The timer includes connection/setup, data, local full verification/publication and protocol/control completion, plus process/summary overhead. Independent external SHA-256 is performed after this timer. The raw client internal elapsed time is also retained and is not substituted into this table. Population CV is calculated across each three-rate group.

Downloads use the existing local receiver defaults final_verify=full and commit_sync=none. The diagnostic range checkpoint batch is 16; it does not change the non-range manifest path or become an accepted production default. The temporary server's fsync_file_and_dir option controls its upload receiver; it does not make client downloads durable. Do not compare upload/download figures as equal durability policies.

| Download sample | Task wall seconds | Task Gbps | External SHA / protocol |
|---|---:|---:|---|
| c1-01 | 301.621321 | 1.822669 | PASS |
| c1-02 | 299.858117 | 1.833386 | PASS |
| c1-03 | 300.185949 | 1.831384 | PASS |
| c4-01 | 356.718499 | 1.541147 | PASS |
| c4-02 | 357.748314 | 1.536711 | PASS |
| c4-03 | 359.731149 | 1.528241 | PASS |

One-lane median is 1.831384 Gbps, population CV 0.254366%. Four-lane median is 1.536711 Gbps, population CV 0.348727%. The six-case protocol/integrity gate passes. Four lanes are about 16.1% slower in task rate under this policy. The longest sender result wait is 174.730043 seconds (c4-02), now followed by successful lease finalization and complete termination. Every c4 sample records eight successful lease finalizations; c1's non-range asynchronous path does not emit that per-range event, so its count of zero must not be interpreted as a failed or unexecuted commit.

Each output must match SHA-256 054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1. Fresh successful acceptance payloads are removed only after this check and retention of checkpoint evidence; failure outputs are retained. Per-case commands, binary/source identifiers, receiver policies, bytes, protocol results and QP initialization are stored in metrics/*.verified.json. Detailed counters and phase observations are in acceptance/summary.json.

Before downloads, an existing diagnostic 64 GiB payload was relocated to R770-2 through a matching-source four-lane native upload. It completed in 337.240751 seconds (1.630158 Gbps), with eight successful result/lease finalizations, four DirectoryEnds, full receiver verification, fsync_file_and_dir and matching external SHA. Its final result wait reached 151.389483 seconds. Only then were its R770-1 data copy and temporary hardlink unlinked; original manifest/logs and a location/hash receipt remain. Retained data: R770-2/archive/archive-c4-03-upload-64g-c4/payload/payload.bin under the evidence root. Receipt: acceptance/archive-c4-03.json. This storage relocation is not one of the six download repetitions.

## Performance Findings And Limits

- The accepted c1 group remains about 1.83 Gbps; c4 is about 1.54 Gbps. This is a correctness repair, not evidence of a throughput improvement or a 100 Gbps file path.
- c1-01 spends 157.770 seconds in finalization/full verification; its sender waits 159.774 seconds for FILE_RESULT. With this measured storage/verification policy, that final phase alone implies a sample-derived task-rate ceiling of roughly 3.5 Gbps even if earlier work took zero time. This is not a hardware bandwidth benchmark.
- c4-01 finalization takes 148.550 seconds in the last finishing range (range 7). Every 8 GiB range issues 139264 DATA writes and 8192 chunk flushes. First-wave transaction-lock waits are 27.51-33.01 seconds per range; second-wave waits are 78.57-80.92 seconds. Actual file_write_at_all_seconds is 6.19-6.31 per range; second-wave checkpoint time is 19.14-21.02, including manifest serialization of 14.66-15.77 seconds. These are overlapping cumulative thread timings, not additive task phases. They identify shared transaction locking and checkpoint serialization as concrete follow-up targets; they do not isolate a causal speedup yet.
- Client peak RSS from /usr/bin/time is 2310256 KiB for c1-01 and 8759752 KiB for c4-01. Receiver resources still grow substantially with lane count; this stage does not reduce the RDMA pool.
- In all six downloads and the archive upload, RNR, out_of_buffer, transport-retry-exceeded and local-ACK-timeout deltas are zero on both hosts. resp_cqe_error and resp_cqe_flush_error increments are retained and unclassified. Download CQ error/flush deltas span 65/64 to 900/896 depending on host/sample; the archive has larger increments. Successful files do not explain these errors or establish long-term stability.
- This run uses anonymous control with TLS disabled for an isolated diagnostic service. It is not a production security/deployment qualification, a full rerun of the 48-cell baseline, a symmetric upload/download performance study, or a multiport test.

## Next Implementation

1. Expose and report the actual receiving role's final verification, manifest/checkpoint and durability policies in TreeTransferOptions, client CLI and JSON. Preserve defaults while adding controls. A server option cannot configure the download client's local receiver.
2. Compare full versus verified_chunks at equal durability, same fixed source/build and same 64 GiB input, at least three repetitions. Keep per-frame checksum and complete verified chunk coverage, preserve checksum=none full-readback behavior, and revalidate resumed data. Require corruption/resume/publication regressions and independent external SHA. State the different failure coverage: verified_chunks does not provide full's post-write readback check. Record both task rate and the integrity/durability policy; do not get a performance number by silently dropping validation.
3. Separately profile and change manifest serialization/checkpoint cadence, transaction-lock hold and write batching. Change one principal factor per comparison and state the bounded recovery window. The range checkpoint environment variable is not a universal manifest optimization.
4. Use those results to decide the producer/network/consumer concurrency and bounded buffer design, then tune RQ/CQ budgets and copies. Investigate the CQ error/flush deltas independently. Microbatch and scheduling remain later work; link/direct-bench results do not predict file task throughput.

## Cleanup And Handoff

The serial matrix driver exited 0. Both hosts have no active experiment client/server/sampler and no temporary control listener on ports 47201-47207; RDMA resource listings retain only the base GSI QP. Production services PIDs 126766 / 164834 remain running. R770-1 free space is about 83.4 GiB and R770-2 about 195.3 GiB after acceptance cleanup. The archive payload remains on R770-2. Evidence: acceptance/final-state-R770-1.json and final-state-R770-2.json; these receipts are refreshed after documentation synchronization to record the final commit while requiring unchanged measured binaries/configuration.

The two pre-existing untracked 2026-10-14 task documents, Windows project sources and unrelated CPSS(DCC) data are preserved. The task scope, source, commands and boundaries are in 2026-11-05-v3-result-lifetime-01.md. Experimental scripts and persistent handoff are under the evidence root. acceptance/evidence-scripts.sha256 identifies experiment.py, summarize.py and the frozen baseline helper. This report, docs/ROADMAP.md and the original performance plan's execution record form the documentation closeout; the measured source remains 78e1e4ab. The final documentation backup SHA/readback is recorded in HANDOFF.md and acceptance/github-backup.txt under the evidence root.
