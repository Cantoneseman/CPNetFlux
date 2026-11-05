# R770 RDMA Tail Protocol Trace 01 Result

Date: 2026-11-05
Status: diagnosis complete; 64 GiB end-to-end gate remains NO-GO.
Input: fd10e16b8cc9ea96dc78cce82b3c203c2fba6f68.
Measured trace source: d4103e85c0837bbe027ca95cbff00a8de3bb4cce, pushed to origin codex/V3-GRIDFTP-OPTIMIZED-RERUN-01 with matching remote SHA readback.

## Outcome

The remaining long-finalize failure crosses V3 work-lease semantics, not just the repaired RDMA CQ wait. Range 8 FILE_RESULT is successfully written by the receiver and read by the sender. Sender WorkExecutor::finalize then returns InvalidArgument, so that lane never sends DirectoryEnd and the client remains in readNext. The published 64 GiB file is correct, but this is a failed protocol task.

The source has a default 30 second lease deadline. Successful payload finishIo renews the idle deadline. awaitResult moves the lease to CommitPending without changing that deadline, while DynamicFileQueue::commit rejects now >= deadline. The last receiver range performs full-file readback before sending FILE_RESULT; this exceeds the sender's idle lease. Removing the old 30 second CQ timeout exposed this separate 30 second lease boundary.

## Measured Timeline

Sample: v3-download-64g-c4-06. Four connections, eight 8 GiB ranges of one 64 GiB file. Native RDMA SEND/RECV explicitly selected; control uses TCP. Client local bind 192.168.100.1, server 192.168.100.2, direct route via eno17095np0, control port 47070, data base 48520. Both sides use the measured source commit and matching binaries.

- Receiver range 8: FILE_END read -> RangeFileCoordinator::complete returns: 149.911969482 seconds. This includes the transaction's full-file verification/publication. Receiver read_bytes advanced by about 2.29 GB during a five-second observation, confirming useful readback progress.
- Receiver then completes its flush/commit boundary and callback, writes FILE_RESULT with success, and enters next_frame_read_begin on channel 1.
- Sender channel 1: FILE_END written, FILE_RESULT read wait 172.613888229 seconds, FILE_RESULT status Ok, lease_finalize_begin, lease_finalize_end local StatusCode 1 (InvalidArgument).
- Sender channel 1 has no callback or DirectoryEnd write afterward. Channels 0, 2 and 3 have successful DirectoryEnd write and receiver read events.
- Receiver output payload.bin has 68,719,476,736 bytes. External sha256sum returned 054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1, matching the source seed expansion.
- The remaining client control connection was CLOSE-WAIT while its worker waited for an RDMA frame. The file and HCA counters then stopped changing. The client was explicitly SIGTERM-stopped for diagnosis; driver result FAIL, exit -15. No complete task JSON was produced.
- RNR, out_of_buffer, req_transport_retries_exceeded and local_ack_timeout_err deltas are all zero on both nodes. Receive CQ error/flush counters are nonzero and remain in the raw evidence; they are not relabeled harmless.

Host monotonic clocks differ. Only intervals within one host are compared; cross-host causality uses frame identities and matching events.

## Deterministic Reproduction

An independent program links the measured libcpnetflux_core.a, constructs a valid eight-range queue, completes the first seven ranges, and uses a logical clock for range 8. It requires no network, disk payload or wall-clock sleep.

| Result wait | CommitPending deadline | finalize | Remaining outstanding |
|---|---:|---|---:|
| 25 seconds | 30 seconds | Ok | 0 |
| 172 seconds | 30 seconds | InvalidArgument | 1 |

The reproduction preserves existing expiry rules. It establishes the failure mechanism; it is not a regression test for a completed fix. Source: scripts/lease_finalize_repro.cpp; output: acceptance/lease-finalize-repro.jsonl.

## Change And Validation

Only opt-in lifecycle observability was added to the product. CPNETFLUX_V3_PROTOCOL_TRACE=1 logs channel/file/generation/range/attempt and operation boundaries; defaults, wire format, deadlines and transfer behavior remain unchanged. No payload, credential or file path is logged by this trace.

- R770-1 CMake build PASS; full CTest 491/491 PASS, no skips, 228.47 seconds, with an in-process temporary token and explicit RDMA bind .1.
- Existing two-range loopback with protocol trace enabled PASS: 38 parseable JSON events, both range results and DirectoryEnd verified.
- R770-2 same-source build PASS; 17 related tests PASS; explicit RDMA duplex smoke PASS, 6.77 seconds, bind .2.
- CMake configuration and SHA-256 of server, tree upload/download clients, RDMA bench and unit-test binary match both hosts. Receipt: acceptance/tail-trace-matching-builds.json.
- The standalone lease reproduction passes its under-deadline and expired-result assertions. An initial setup incorrectly supplied only one member of an eight-range partition and was rejected before claim; the retained source constructs the valid full partition.
- git diff --check PASS before source commit.

## Earlier Diagnostic Limitations

The fd10e16 c1/c4 attempts remain failures. Batch 1/4/16 comparisons are single-run diagnostics, not accepted throughput measurements. c4-03 and c4-05 published correct files but did not complete the protocol.

c4-04 startup strace changed execution and failed after 36,124,098,520 logical bytes in 144.653 seconds. Its roughly 38.2 GB HCA traffic is not logical completed-file throughput. Existing RDMA event snapshots can be stale; a 68 byte receive/consume difference alone does not establish the missing frame type.

The initial runpy harness overrode its returned dictionary rather than file_run.__globals__, causing a second server-start attempt and an overwritten PID receipt. This produced listener-conflict noise and required explicit stopping of the original test server. c4-06 corrected the injection, had no listener conflict, and its driver stopped the correct server. Prior runs are not promoted to clean experimental evidence.

## Next Implementation

1. Define an explicit V3 result/commit lifecycle bounded by the task deadline and cancellation. Separate active payload idle timeout from legitimate final verification/result waiting. Keep owner/epoch/attempt fencing and never revive a reclaimed lease on a late result.
2. Add deterministic clock tests for long active finalize, expiry, cancellation, late stale result, peer loss and recovery reconciliation before changing queue behavior. Cover sender and receiver lease paths; the receiver also finalizes after its local commit.
3. Ensure lease/state failure and TCP control peer-close wake the RDMA waiter and reach a bounded task failure. Current CQ waits are unbounded and do not independently notice a control FIN in this path. Preserve the first actionable error; do not report only a later WC flush.
4. Make receiver policy ownership explicit. The server's verified_chunks option did not change the download client's default full verification. Expose/record the actual receiver policy before comparing full versus verified_chunks, checkpoint batches or write batching.
5. Build and run related gates on R770-1, commit/push, sync the exact commit to R770-2, compare configuration/binaries, then require complete control/data terminal states and external SHA-256 for 64 GiB one/four-channel downloads, at least three repeats each. Resume performance attribution only after that gate passes.

Likely next files: work_execution.h, work_executor.h/.cpp, dynamic_file_queue.h/.cpp, persistent_tree_transfer.cpp, tree_transfer_client.cpp and related lease/transport tests. Update the architecture decision before altering lease semantics. Do not solve this by globally increasing the default idle lease or accepting every expired FILE_RESULT.

## Evidence And Handoff

Evidence root: /home/Su/experiments/CPNetFlux/2026-11-05-r770-rdma-e2e-fd10e16-01.

Primary receipts: acceptance/tail-trace-analysis.json, tail-trace-matching-builds.json, lease-finalize-repro.jsonl, protocol-trace-loopback.log and ctest-tail-trace-r770-1.log. Per-host logs, snapshots, metrics and payload/checkpoints remain under that root. The baseline driver is scripts/baseline.py; c4-06 used CPNETFLUX_RANGE_CHECKPOINT_BATCH=16, CPNETFLUX_RDMA_DIAGNOSTICS=1 and CPNETFLUX_V3_PROTOCOL_TRACE=1 on the receiver; both trace flags were set on the sender. Server-final-verify was verified_chunks; receiver default remained full.

All c4-06 task clients, samplers and temporary servers ended, and test ports no longer listen. Production PIDs 126766/164834 remained present. No historical payload or unrelated workspace was removed. R770-1 has about 21 GiB free after retaining this output; another fresh 64 GiB attempt requires an explicit space/reclamation plan first. The two pre-existing untracked 2026-10-14 task documents were not staged or changed. Windows project modifications were untouched.

The original 48-sample baseline remains 42 PASS / 6 FAIL. This stage does not provide a new valid file task rate, 100 Gbps result or production-readiness claim. Its deliverable is a tested, backed-up trace implementation and a reproduced lease failure mechanism.
