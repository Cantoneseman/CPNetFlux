# S2E Per-file Budget28

Date:2026-11-09. Status: observer implemented/tested; bounded attribution pending; no optimization accepted.
Input: d59f942e210d6e8596fcdc5bffc0608ee2dae952, pushed Budget27 result.
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

## Frozen Hypothesis And Edit Scope

Test local whole-file receiver thread constructor and post-worker-loop join tail
as a possible removable fixed cost. Join time before worker-loop exit still
contains payload work and is not a pool saving. Worker entry latency overlaps
constructor/payload intervals and is diagnostic only. Also record transaction
begin, commit/publication and FILE_RESULT write for choosing the next hypothesis.
This round does not measure sender producer creation or sender result-dependent
credit starvation; server phase logs are existing reference only. Sender-role
client summaries must explicitly say no local receiver profile was used.

Task-frozen CPNETFLUX_MEMBER_WORK_PROFILE=0|1, default0, validates before
connection. Per-channel receiver collectors, no shared recording lock, at most
65536 intervals per channel; aggregate only after workers stop, count interval
union once across lanes. Keep per-lane sums, nested manifest-save totals and
scope explicit. All sums/unions are diagnostic envelopes, not automatic gains.
No full-file timing JSON, raw range output or inherited perf event collection.

Files: new include/cpnetflux/core/io/member_work_profile.h and
src/core/io/member_work_profile.cpp; persistent_tree_transfer.h/.cpp;
src/core/io/tree_transfer_client.cpp; new tests/unit/member_work_profile_test.cpp;
tools/test/run_cpnetflux_tree_serializer_smoke.py;
tools/perf/run_small_file_baseline.py; new tools/perf/run_member_work_budget.py;
new tools/test/run_member_work_budget_test.py; CMakeLists.txt; this task/result
and docs/ROADMAP.md/docs/README.md. Reuse existing optional ManifestSaveStats;
profile scope is completed whole-file local receiver attempts, excludes ranges,
failed/interrupted members, sender and final batch integrity audit. Partial
coverage is reported and cannot pass the fresh full-task budget gate.

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

## Pre-measurement Verification

Default-off bounded per-channel collector now covers completed local whole-file
receives, six stage unions/sums, overlapping worker-entry latency separately,
and nested member manifest-save aggregates. Error/interrupted attempts stay out
of the completed scope; started/completed discrepancy and range counts expose
partial coverage. Sender role is explicitly not_used_sender. No runtime policy,
thread count, checkpoint cadence, wire format or recovery behavior changed.

Evidence root: /home/Su/experiments/CPNetFlux/2026-11-09-v3-per-file-budget-28.
Old binary failed new smoke assertion because member_work_profile was absent
(red-smoke.log). The first wrapper had a PowerShell exit-code quoting mistake;
the saved Python assertion was read back separately. Fixed here-doc wrappers
were used for acceptance. RelWithDebInfo build and20 related CTests passed;
four interrupted/corrupt/conflict/edge tests also passed with profile1.
R1 core.whitespace=cr-at-eol diff --check passes. Next: commit/push/readback,
same-commit R2 build/test/hash, then exactly three observer pairs, no retries.
