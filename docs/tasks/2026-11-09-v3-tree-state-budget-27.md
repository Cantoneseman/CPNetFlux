# S2D Tree State Budget27

Date:2026-11-09. Input:f5cd9f56f474594a9fd01b69001c64f438bfa553.
Status: complete; observer gate passes, direct sync budget below10%; no incremental candidate.
Result:[Budget27 result](2026-11-09-v3-tree-state-budget-27-result.md).
R1 sole writer; R2 fixed-commit peer. No agents/worktrees/desktop.

## Scope And Non-Goals

Measure whether removing repeated full-tree snapshot work has a conservative
complete-task benefit budget after Runtime26 failed. Add explicit task-frozen
CPNETFLUX_TREE_MANIFEST_PROFILE=0|1 (default0), no new serializer candidate.
Only original serializer is measured. Do not rerun buffered, change cadence,
format, ACK, member checkpoints, CRC, V3/recovery/atomic publication, summary
defaults, queue/thread pools, RDMA or system policy. No incremental journal yet.

Source inspection finds full snapshot copying and destruction under the state
mutex, while serialization/writing happens outside it. Separate these costs,
record-copy/sort/format/body CRC, temp write/rename, final reconciliation and
finish barrier. Record bounded monotonic callback-wait and snapshot-lock ranges;
union/intersection avoids summing overlapping four-lane waits. Keep every
existing duration's scope explicit. The direct synchronisation envelope is not
a hard bound on CPU/cache contention or all hypothetical architecture changes.

## Affected Files

include/cpnetflux/core/tree/tree_manifest.h, tree_manifest_writer.h;
src/core/tree/tree_manifest.cpp, tree_manifest_writer.cpp;
src/core/io/tree_transfer_client.cpp; tests/unit/tree_manifest_writer_test.cpp;
tools/test/run_cpnetflux_tree_serializer_smoke.py;
tools/perf/run_small_file_baseline.py; tools/perf/run_tree_state_budget.py;
tools/test/run_tree_state_budget_test.py; CMakeLists.txt; this task, result and
docs/ROADMAP.md/docs/README.md. Reuse existing optional ManifestSaveStats timers
and bounded S0 ownership/measurement/integrity/cleanup helpers.

## Acceptance And Measurement

Tests first: disabled/enabled/invalid selector, unchanged output bytes and CRC,
union/intersection including overlap/touch/empty/invalid intervals, writer final
barrier and failure metrics. Related CMake/CTest plus existing byte oracle and
V3 recovery/coalesce gates. Profile caps65536 callback waits/4096 hold ranges;
truncation invalidates attribution. No hot-path logging or perf inheritance.

After commit/push/readback, both peers same commit/config/build/three binary
SHA-256, run only three interleaved off/on measurement pairs,4KiB x10K resident
tmpfs source/new target,4TCP/pending2/queued pwrite,cached summary,phase/detail
off,original serializer. Source39.0625MiB, owned payload<=1GiB per endpoint,
MemAvailable>=12GiB,owned VmSwap0,host pswpout delta0; explicit .1/.2 bind and
routes/interface counters. Independent target/source SHA and10001 checkpoint
body CRCs per task after independent spawn-to-exit T_ready; exact cleanup.

This is an attribution/observer-overhead matrix, not five-pair performance
acceptance. Require median profile overhead<=5%, each arm CV<=5%, untruncated
intervals and unchanged integrity/resource gates to transfer a budget cautiously.
If observer gate fails, report inconclusive; do not rerun/chase CV. Background
service sums and CPU proportions alone cannot authorize the next optimization.
If direct synchronisation budget is below10% T_ready, do not implement an
incremental format on that evidence; reassess other per-file fixed costs. If
budget is material, document the precise removable mechanism and recovery
contract before a separate byte-compatible candidate or format design.

Evidence:/home/Su/experiments/CPNetFlux/2026-11-09-v3-tree-state-budget-27.
Record measured/final commits, commands, all samples, tests, counter/resources,
GitHub SHA readback, peer/local mirror verification and next handoff in result.

## Implementation Checkpoint

R1 built successfully. New selector test first failed on missing profile JSON
against the input build, then passed. Related CTests21/21 (including3 new C++
profile/interval checks, existing independent byte oracle and driver checks);
profile-on coalesce/changed/conflict/corrupt CTests4/4 pass. Scope-exit cleanup
timing retains original lock ownership; no destructor relocation candidate.
Save substeps reuse checkpoint::ManifestSaveStats and OptionalManifestTimer.
Only the background writer updates its profile; callback intervals are captured
under the state mutex and merged after producer stop/finish. No hot-path output.
Runtime profile0/default reserves no interval buffers or optional timer readings.
Fixed-commit3-pair result: median observer overhead1.985%, CV0.265%/1.389%;
direct sync envelope3.35%-3.57%, all callbacks plus final tail4.49%-4.74%.
No tree format/copy-relocation implementation; next is per-file budget reassessment.
