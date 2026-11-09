# S1B Summary Default Recheck34

Date:2026-11-10. Status: scoped; resource prerequisites pending.
Input:6cfb1bb7c516b7677dadb8a255f5476525b36e3d.
R1 sole serial writer; R2 fixed-commit build/test/transfer peer. No delegation.

## Scope And Files

Execute Roadmap/Budget33's next independent S1B resource-prerequisite review.
Ownership32/33 provide a new zero-host-swap interval after Gate23's preparation
failure; this is changed environment evidence, not proof of8GiB safety. First
verify same fixed commit/configuration/binaries, relevant existing tests and a
continuous60-second resource window on BOTH nodes. Only after both windows pass,
permit one invocation of the existing run_summary_default_gate.py matrix.

If preparation or any guard fails, preserve diagnostics, stop, clean exact owned
payloads and retain original default; no second preparation/performance attempt.
If preparation succeeds, collect exactly five interleaved original/cached8GiB
full-task pairs using the existing gate without replacing/removing any sample.
This is regression/default acceptance, not a new large-file speed mechanism.

Files: this task/result, docs/ROADMAP.md, docs/README.md. Evidence-only resource
collector and launch records live outside Git at:
/home/Su/experiments/CPNetFlux/2026-11-10-v3-summary-default-recheck-34.
Conditional ONLY after the existing gate passes: minimal unset-selector fallback
in src/core/io/tree_transfer_client.cpp, its default assertion in
tools/test/run_cpnetflux_summary_path_smoke.py and current README/default docs.
Do not change existing benchmark mechanics just to obtain a pass.

Non-goals: receiver performance branch restart, RDMA, new profiler, frame/thread/
queue sweep, serialization/microbatch development, system/network/mount/THP/swap
policy, dropped CRC/lease/checkpoint/ACK/recovery guarantees or production restarts.
Preserve unrelated October14 task documents.

## Resource And Performance Acceptance

- Stable window: at least60 seconds, sampled every0.25s; MemAvailable>=12GiB,
  host pswpout delta0, monitor owned VmSwap0, exact production PID/start identities,
  /dev/shm free>9GiB on both nodes and no prior owned root. Record memory, swap,
  route, interface counters, space and elapsed samples. Host-used swap need not be
  zero; it is not the experiment-owned swap metric.
- Unchanged source:8GiB materialized tmpfs, repeated random8MiB block, external
  SHA/mincore outside T_ready. This is the previously approved bounded8GiB S1B
  exception, not64GiB. At most one source/output per endpoint; payload root is
  /dev/shm/cpnetflux-s0-summary-default-34. Preserve>=12GiB/zero swap during work.
- Existing contract: R2 .2 -> R1 .1, explicit binds, four TCP/eight1GiB ranges,
  queued sender/inline mmap receiver, CRC32C/verified_chunks, checkpoint16,
  TLS/compression off, commit_sync=none,64MiB/lane and256MiB total mapping bound.
  Counter deltas must be recorded; any new network errors block promotion.
- Five complete pairs, spawn-to-exit exit timestamp, cached median wall
  regression<=5%, each arm CV<=5%, full SHA,8192 exact verified chunks, V3
  terminals and verified_chunks/effective not_used. No retry for CV or time.
- Build and relevant CTest on both fixed peers before transfer. If the gate passes,
  first make the new default assertion fail, then change only unset fallback and
  verify default/explicit original/cached, invalid selectors and recovery gates.
  Commit/push/readback/sync/rebuild/three-hash comparison before any next transfer.
- Retain all failed guards/samples. Stop only owned experiment processes; clean
  exact marked roots, preserve production, retain source/evidence hashes and local
  mirror. Explicit commits only; GitHub backup requires push and SHA readback.

## Handoff

Budget33's big-file receiver candidate branch stays paused. Reference27.3Gbps,
30-50/70-85/100Gbps gaps, scope17 interface+94 and RDMA issues remain separate.
Successful S1B can only close summary-default regression acceptance; failure keeps
cached opt-in and closes this attempt without an automatic retry. Update the next
work boundary based on evidence, not another unbounded measurement campaign.
