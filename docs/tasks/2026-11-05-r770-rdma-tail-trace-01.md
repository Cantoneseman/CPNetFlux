# R770 RDMA Tail Protocol Trace 01

Date: 2026-11-05
Input commit: fd10e16b8cc9ea96dc78cce82b3c203c2fba6f68
Authoritative source: R770-1 /home/Su/projects/CPNetFlux

## Scope

- Add opt-in V3 lifecycle events for FILE_BEGIN, FILE_END, FILE_RESULT and DirectoryEnd on sender and receiver paths, with channel, file, generation, range and attempt identities.
- Cover receiver commit/callback and sender lease-finalize/callback boundaries to distinguish wire waits from local state processing.
- Enable events only when CPNETFLUX_V3_PROTOCOL_TRACE=1. Do not log payload, credentials, file paths or raw status messages. Event result_code is a local StatusCode, except successful file_result_read_end and file_result_write_begin, where it is the wire FrameStatusCode.
- Build and test on R770-1, commit/push, then build the exact commit on R770-2 and compare binary SHA-256 before another transfer.
- Run a 64 GiB four-channel diagnostic download, explicitly using RDMA .1/.2, and record trace, hardware counters, task terminal state and external file SHA-256 when published.

## Non-goals

- No transfer behavior, protocol format, deadline, checkpoint, queue-size or scheduling changes.
- No performance acceptance from traced attempts, partial payload or file publication alone.
- No cleanup of retained payloads, production services or unrelated workspaces.

## Acceptance

- R770-1 CMake build and related CTest pass.
- R770-2 uses the pushed source commit, identical build configuration and matching relevant binary SHA-256.
- Events identify whether range 8 FILE_RESULT is written, read, finalized and followed by DirectoryEnd.
- Record both endpoints, external integrity, counter deltas, evidence paths and any explicit diagnostic stop. An incomplete protocol task remains FAIL.

## Affected Files

- src/core/io/persistent_tree_transfer.cpp
- docs/tasks/2026-11-05-r770-rdma-tail-trace-01.md
- docs/tasks/2026-11-05-r770-rdma-tail-trace-01-result.md
- docs/ROADMAP.md (current result and immediate next task only)

## Initial Evidence

Evidence root: /home/Su/experiments/CPNetFlux/2026-11-05-r770-rdma-e2e-fd10e16-01

- c4-04, checkpoint batch 16 with startup strace: client failed after 36,124,098,520 accepted logical bytes in 144.653 seconds. Four 8 GiB ranges completed; partial next ranges account for the excess over 32 GiB. RNR/out-of-buffer deltas were zero. strace perturbs execution, so its timing is diagnostic only.
- c4-05, checkpoint batch 16 with both endpoints' RDMA diagnostics: full 64 GiB output published, external SHA-256 matched 054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1, but the client never completed. Server timings recorded seven ranges, excluding range 8. Three client QPs closed, and the fourth's last event was receive_consumer_wait. That event's receive/consume difference is a stale event snapshot, not proof of its current read position or a specific missing frame.
- c4-05 showed ready/free pressure in the application receive-slot pool and no RNR/out-of-buffer delta. Both endpoints' process I/O and CPU counters then stayed unchanged across repeated observations. The diagnostic client was explicitly stopped with SIGTERM after about eight minutes; its driver recorded FAIL, exit -15.
- Temporary servers and samplers from c4-04/c4-05 were stopped, including verification that the test ports were no longer listening. Production :2121 services remained present.

## Instrumentation Validation

- R770-1 cmake --build build -j 6: PASS.
- R770-1 full ctest --test-dir build --output-on-failure -j 1 with an in-process temporary CPNETFLUX_TEST_TOKEN and CPNETFLUX_TEST_RDMA_BIND=192.168.100.1: 491/491 PASS, no skips, 228.47 seconds.
- Existing two-range loopback with CPNETFLUX_V3_PROTOCOL_TRACE=1: PASS; 38 parseable JSON events, both FILE_RESULT ranges and DirectoryEnd observed.
- Logs: acceptance/ctest-tail-trace-r770-1.log and acceptance/protocol-trace-loopback.log under the evidence root.
- Result and exact source/backup identity will be recorded in the companion result document after the two-node diagnostic.
