# V3 Receiver Profile 01

Status: in progress, 2026-11-06.
Input: a3ed7e037fbd4c1794b27efbb6f8309aaeb9983d.
Authority: r770-1:/home/Su/projects/CPNetFlux; single serial writer.

## Scope

Profile the real V3 receive path before selecting its next optimization. Add an
opt-in CPNETFLUX_RECEIVER_PROFILE=1 observation under phase timing: delivered DATA
frame capacities, logical vector allocations/copy, checksum-prefix shifting,
decompression, frame CRC and actual whole-file storage writeAtAll calls. Preserve
the existing algorithm, ownership, protocol, checksum, queues and defaults.

User steering makes source and destination memory-backed for subsequent routine
software experiments. tmpfs is not sufficient evidence by itself: warm/check
source residency before timing and check output residency and process I/O after.
Retain disk-backed experiments only as historical/deferred storage work.

## Non-Goals

No receive-buffer reuse/copy removal yet, no queue/pool resizing, no new transport,
no checksum/checkpoint policy changes, no perf permission/system changes, no
agents/worktrees/desktop, no large disk payloads or production service restart.
CPU and timer attribution do not establish a single exclusive throughput limit.

## Memory and Experiment Gates

- One lane, one 1 GiB source on R770-2, one fresh output on R770-1 at a time.
- Include ~2.04 GiB registered receive slots per endpoint plus send pool in the
  budget; tmpfs bytes and process RSS are distinct accounting dimensions.
- Start with >=12 GiB MemAvailable and >=2 GiB free tmpfs per node; monitor while
  processes run, abort owned experiment if MemAvailable <6 GiB or its RSS >4 GiB.
- Sample process RSS/HWM/locked pages, host memory/swap activity and tmpfs use.
  Polling is sampled evidence rather than a kernel-enforced hard memory cap.
- Observe in-memory source and output pages, no sender physical reads beyond
  bounded unrelated metadata, zero sampled owned-process VmSwap and no host
  swap-out during monitoring. Record global swap-in separately: unrelated desktop
  processes may page in and global counters cannot attribute that to payload. Failed
  workload gates are excluded and retained, not silently retried/overwritten.
- Profile on/off same fixed commit, three interleaved samples each; one full
  verification control. CRC, external SHA, terminals, QPs, routes and counters.
- Verify output SHA then remove each output; recheck source SHA before final
  cleanup. Preserve production PIDs 126766/164834 and original idle GSI QPs.

## Acceptance / Files

Independent wire-byte oracle validates diagnostic counters for CRC/none and
compressed/raw cases; observation off is null with identical file bytes. First
show a missing-observation regression on old code. Relevant CMake/CTest runs
serially to avoid concurrent RDMA pools. Commit/push explicit files on R770-1,
sync fixed commit to R770-2, build and match eight hashes/config/version before
native transfer. Finish results, roadmap, GitHub SHA readback and handoff.

Affected: include/cpnetflux/core/io/persistent_tree_transfer.h,
src/core/io/persistent_tree_transfer.cpp, tests/unit/framed_data_socket_test.cpp,
tools/perf receiver memory profile runner and focused script gates as needed,
docs/DECISIONS/2026-11-06-v3-small-cache-experiments.md, docs/ROADMAP.md,
this task and result. Raw evidence/scripts stay outside Git unless reusable.

Evidence: /home/Su/experiments/CPNetFlux/2026-11-06-v3-receiver-profile-01.
Temporary payload root: /dev/shm/cpnetflux-v3-receiver-profile-20261106.

## Initial Findings

readNext creates/resizes a fresh vector for every delivered frame. Receiver then
copies it into logical and erases four CRC prefix bytes, moving the remaining
payload. Existing checksum_seconds combines these with CRC/state validation.
Whole-file writeSeconds includes BufferedFileWriter work; route its existing
FileWriteObservation down to actual writes/flushes for separate attribution.
perf stat was attempted and denied by perf_event_paranoid=4; no setting changed.

Tools checked: shell and dedicated chat messaging exposed; chat messaging unused.
No codex CLI found on R770-1. No delegation or handoff requested/performed.

## Implementation Gate

Old code failed the independent wire-derived observation contract. New code
passed all 14 framed socket tests, including eight raw/compressed, CRC/none,
profile-on/off combinations with byte-identical published output. R770-1 serial
full CTest passed 542/542 in 258.61 seconds. Three memory-guard tests include real
pidfd termination of a matched child and survival of a neighbor/stale identity.
The negative 64 GiB available-memory threshold correctly returned failure with
an explicit violation and no processes signalled. No performance gain is claimed.
