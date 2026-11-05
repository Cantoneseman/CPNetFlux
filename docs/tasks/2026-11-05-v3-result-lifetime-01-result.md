# V3 Result Lifetime 01 Result

Date: 2026-11-05. Input: 28d22ab7fbd8c3d9ec82c50ef64b0a455bb0f46d.
Status: implementation accepted locally; matching-peer and 64 GiB acceptance pending.

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
- Intermediate full gates retained: two recovery failures (fixed), one /proc fd-enumeration race (fixed by skipping only an already-disappeared descriptor), and the terminal/data completion race (fixed). These logs are diagnostic evidence, not accepted runs.

## Remaining Acceptance

Commit/push this implementation, read back remote SHA, synchronize the exact source to R770-2, build and compare configuration plus five binary hashes. Run related peer gates, archive one diagnostic payload only after a complete matching-build upload and external SHA-256, then require three fresh 64 GiB downloads each at one/four lanes, complete DirectoryEnd on both sides, full control completion, external SHA and interface/route evidence.

The original baseline remains 42 PASS / 6 FAIL. No new 64 GiB task rate, 100 Gbps achievement or production readiness is claimed at this local-code stage. Receiver final verification/checkpoint defaults and system networking are unchanged. Two pre-existing untracked 2026-10-14 task documents and Windows project changes are outside this stage.
