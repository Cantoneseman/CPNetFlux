# S2C Tree Serialization Runtime26

Date:2026-11-09. Status: next implementation/experiment plan, not executed.
Input: pushed Serialization25 result commit; record exact SHA at start.

## Scope And Hypothesis

Serialization25 passes exact bytes and local CPU gates, but does not establish
transfer gains. Test whether reduced work per full tree snapshot improves cached
small-file T_ready. S1B default promotion remains held; no RDMA, queue/thread pool,
coalescing frequency/schema/protocol/member batching or system policy changes.
R1 sole writer; R2 fixed-commit build/test/source peer, no agents/worktrees.

Inspect runtime ownership first and record exact files. Expected modules:
include/cpnetflux/core/tree/tree_manifest.h, src/core/tree/tree_manifest.cpp,
tree_manifest_writer integration/caller in tree_transfer_client.cpp, relevant
unit/CLI tests, diagnostic target (remove duplicate candidate after core reuse),
bounded A/B driver and task/result/route docs. Extend the current module APIs
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
