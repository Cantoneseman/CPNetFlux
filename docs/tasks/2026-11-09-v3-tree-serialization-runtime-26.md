# S2C Tree Serialization Runtime26

Date:2026-11-09. Status: complete; full-task performance NO-GO, original default.
Outcome: [Runtime26 result](2026-11-09-v3-tree-serialization-runtime-26-result.md).
Input: pushed/read-back commit 0d01a933ddd6e43db10fa4713ce330dc99cad68d.

## Scope And Hypothesis

Serialization25 passes exact bytes and local CPU gates, but does not establish
transfer gains. Test whether reduced work per full tree snapshot improves cached
small-file T_ready. S1B default promotion remains held; no RDMA, queue/thread pool,
coalescing frequency/schema/protocol/member batching or system policy changes.
R1 sole writer; R2 fixed-commit build/test/source peer, no agents/worktrees.

Affected implementation files: include/cpnetflux/core/tree/tree_manifest.h,
src/core/tree/tree_manifest.cpp, src/core/io/tree_transfer_client.cpp,
tools/perf/tree_manifest_serialization_bench.cpp,
tools/perf/run_small_file_baseline.py, tools/perf/run_tree_serializer_ab.py,
tools/test/run_cpnetflux_tree_serializer_smoke.py,
tools/test/run_tree_serializer_ab_test.py, CMakeLists.txt. Task/result and current
route documents will record acceptance and handoff. Extend the current module APIs
conservatively; original default must remain byte-for-byte supported. Reuse the
independent Python oracle, complete225-snapshot proof and legacy comparator.

Freeze explicit CPNETFLUX_TREE_MANIFEST_SERIALIZER=original|buffered per task,
validate before connection and report requested/effective mode. Shared save/load
and programmatic callers retain original default. A changed selector must affect
tree manifest serialization only, not member manifests or summary hashes. Verify
which preflight/final saves are included; do not report partial selection as whole
task coverage. Never change selector or cache mutable records during a running
task. Cover default/invalid selection, both directions, CRC, finish barrier,
save failures, crash/corrupt resume, target conflict and repeated state changes.

## Frozen Full-Task Gate

After commit/push/readback and same config/version/three binary hashes on both
nodes, run five interleaved original/buffered pairs of4KiB x10K, one resident
tmpfs source and new target each time (~39MiB each),4TCP/pending2/queued pwrite,
CRC32C/verified_chunks/checkpoint16, commit_sync=none, TLS/compression off,
explicit cached summary, phase/detail timers off in both arms. Corrected
independent spawn-to-exit T_ready; preserve summary CRC and separate hash seconds.
Do not compare with scope24's phase-on/gated-exec pair or scope25 local CPU wall.

Acceptance: all complete SHA/CRC/member/tree checkpoints and V3 terminals,
MemAvailable>=12GiB/owned VmSwap0/host pswpout delta0/<=1GiB payload per end,
routes/counters and exact cleanup; median T_ready down>=10%, each arm CV<=5%,
descriptive p95 no worse and all five pairs reported. Retain detailed manifest
snapshot/byte/write work metrics even with detail timers off; their work counts
can change through feedback, but configured frequency must not change.

If pass, record limited opt-in small-file acceptance only; before any default
promotion also test a bounded verified_chunks negative control, both-direction
correctness/recovery and separate matched large-file regression under unchanged
resource gates. If fail, leave original default and do not retry/chase CV or tune
snapshot frequency. Keep module byte/CPU evidence distinct from throughput claims.
Microbatch/adaptive require their separate>=20% wall and recovery gates.

## Implementation Checkpoint

R1 implementation freezes the selector once at each tree client entry before
connection, threads it through initial/preflight/reconnect/background/final saves,
and reports requested/effective mode and all-client-save-attempt scope. Shared
programmatic APIs default to Original and never read environment variables.
Buffered builds a reserved string with to_chars/hex appends; strictly sorted
records borrow the snapshot, otherwise the legacy full-copy/std::sort path is
retained, including duplicate keys. Atomic write/rename and CRC remain shared.
The diagnostic candidate now calls this exact core implementation.

New CLI test first failed against input build as expected (missing selector JSON).
R1 build then passed new selector/oracle/A-B CTests3/3, related tests28/28,
buffered resume/coalesce/corrupt tests4/4, buffered conflict/changed/S0-driver3/3.
Coalesce test includes four SIGKILL/resume points, failed atomic save and completed
output corruption. Both-direction cross-serializer resume is covered by10 tiny
transfers plus two pre-connection invalid selectors. Sorted/reversed full-series
byte/parsed-field/input-immutability proof passed450/450 snapshots.
Evidence: /home/Su/experiments/CPNetFlux/2026-11-09-v3-tree-serialization-runtime-26.
The checkpoint preceded fixed-commit measurement. The final result records all
five pairs: T_ready median1.18330->1.21285s (2.50% slower); no retries or default
promotion. Increased snapshot/byte/lock work is retained as negative evidence.
