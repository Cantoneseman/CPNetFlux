# V3 Payload Reuse 01

Status: in progress, 2026-11-06. Input 6025f54efeb4fe87d69b32b15fcd9513fa431921.
Single R770-1 writer through SSH; R770-2 receives fixed commits only.

## Scope

Reuse sender payload vectors after writeFrameBuffered has consumed/copied every
span. Return the completed consumer buffer during its next pop; let push hand a
cached vector back to the producer under the existing queue mutex. Preserve the
depth-2/131064-byte queued payload budget. Account separately for producer,
consumer and cached ownership; production uses at most depth+2 fixed frame
buffers (default 262128 bytes of vector capacity), excluding compression/RDMA.

## Non-Goals

No frame/protocol, CRC/checkpoint/default policy, RDMA queue/registration or
receiver change. No 64 GiB runs or storage/100G readiness claims. No agents,
worktrees, desktop, system tuning or cleanup of historical data/CPSS.

## Acceptance

- A real allocation-budget regression fails on old sender, not merely a new API
  compile failure. Validate committed content and CRC and retain red evidence.
- Test byte/count cache limits, successful ownership handback, producer and
  consumer waits, close-drain, cancellation and repeated patterned transfers.
- Cached vector capacity <= queue budget plus one frame, cached count <= depth+1;
  oversize/closed/cancelled returns are discarded. Reserve fixed frame capacity
  before reads; no borrowed pointer survives writeFrameBuffered return.
- Record successful new-buffer and reused-buffer acquisitions with separate
  capacity total/budget observations; count only actual read iterations.
- Relevant CMake/CTest including native local RDMA, faults/resume/TLS, then fixed
  commit/push, peer build/tests, matching eight hashes/config/version before runs.
- New same-source 1 GiB tmpfs baseline/candidate, three samples each plus one full
  candidate control. Record source residency/physical reads, routes/QPs, CRC/SHA,
  all terminals, task/file phase and summed producer+lane CPU. No claim from one
  local timer. One output at a time, SHA then remove; remove final owned source.
- Commit explicit files and push verified CPNetFlux origin, remote SHA readback,
  final peer synchronization and external evidence/handoff. Preserve two old
  untracked October task docs and production PIDs 126766/164834.

## Files

include/cpnetflux/core/io/payload_pipeline.h, src/core/io/payload_pipeline.cpp,
src/core/io/persistent_tree_transfer.cpp,
include/cpnetflux/core/io/persistent_tree_transfer.h,
tests/unit/persistent_data_session_test.cpp, tests/unit/framed_data_socket_test.cpp,
CMakeLists.txt, this task/result and docs/ROADMAP.md.

## Evidence

/home/Su/experiments/CPNetFlux/2026-11-06-v3-payload-reuse-01

Baseline setup/driver reuse Cache Path 01, with a new source and separate roots.
Raw logs, temporary token-free validation wrapper, cleanup and backup receipts
stay outside Git. Windows scratch transports patches only; authority is R770-1.

The first concurrent stress caught cumulative fifth allocations: when the
consumer returns a buffer and waits on an empty queue, up to depth+1 buffers can
be free while the producer holds one. A cache limited to queued bytes discarded
useful buffers. Cache allowance is corrected to depth+1 frames (196596 default
bytes), still within the same depth+2 total owned buffers. Initial failure logs
are retained; this budget correction precedes further implementation.

## Progress Before Fixed-Commit Measurement

- Old sender allocation regression: 64 full-frame allocations, failed <=4 gate.
- Corrected allocation regression: 20 consecutive passes.
- Queue/ownership/cancellation tests: 10 tests, 30 consecutive passes.
- R770-1 CMake build and full CTest: 542/542 passed in 207.92 seconds.
- Fresh input-commit tmpfs baseline: three successful 1 GiB native RDMA runs.
- Next: commit/push this implementation, peer build/gates, three candidate runs
  and one full-verification control, cleanup and final result/backup receipts.
