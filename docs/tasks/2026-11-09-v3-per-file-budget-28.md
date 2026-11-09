# S2E Per-file Budget28

Date:2026-11-09. Status: next task, not executed or measured.
Input: pushed Budget27 final result commit; record exact SHA at start.
R1 sole writer; no agents/worktrees. R2 same fixed-commit build/source peer.

## Question And Boundaries

Budget27 finds direct tree-state synchronization only3.35%-3.57% of profiled
T_ready; no incremental tree format/destructor candidate is justified. Reassess
other per-file costs before another optimization. Runtime26 buffered remains
closed. Original serializer/profile0 defaults, explicit cached summary; S1B/RDMA
held. No new microbatch/thread-pool, protocol/schema/cadence/ACK change, integrity
shortcut or system policy tuning in this attribution task.

Target src/core/io/persistent_tree_transfer.cpp per-member producer/receiver-writer
setup/join and FILE_RESULT dependency; existing per-session save/finalize metrics
in src/core/session/download_session.cpp and checkpoint persistence. Source
contains per-member std::thread producers/receiver writers, but code presence is
not a task-time budget. Scope24 clone samples include perf inheritance and cannot
justify pooling. Use aggregate observations without inheriting perf events or
emitting10K detailed records. Inspect ownership before recording exact edit scope.

## Execution And Acceptance

1. Trace one member lifetime and existing timers, clearly distinguish parent
   setup, payload work, checkpoint/publish, result wait and join. Identify nested
   intervals and concurrent lanes; never add their sums as wall savings.
2. Choose one concrete hypothesis with an observable count and directly blocked
   interval. Reuse existing task-local optional metrics where possible. Tests
   must reject incomplete/overlapping/mis-scoped accounting and preserve bytes,
   CRC, failures, cancel, target conflict and resume ownership.
3. After tests, commit/push/readback, sync/build both peers and compare config/
   version/three binary SHA-256. At most3 observer off/on pairs,4KiB x10K,
   same timing/queued/pwrite/summary/window/integrity contract as Budget27.
   Observer overhead<=5% and each CV<=5%; if failed, report inconclusive, no retry.
4. MemAvailable>=12GiB, owned VmSwap0, host pswpout delta0, <=1GiB own payload per
   endpoint, explicit .1/.2 routes/counters, independent source/output SHA and
   checkpoint CRCs, exact cleanup. Keep all raw samples/tests outside Git.
5. Only a specific removable mechanism with a conservative>=10% complete-task
   budget warrants a separate candidate design and five-pair runtime gate.
   Fixed/adaptive microbatch each still needs>=20% wall benefit plus recovery,
   tail and matched large-file gates. A missing budget closes the hypothesis,
   not a reason to expand threads or tune more knobs.

Deliver result/route update, commands, measured/final commits, all failures,
resources/cleanup, GitHub SHA readback and peer/local mirror handoff. No new
performance or development completion is claimed by this next-task document.
