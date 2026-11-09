# S2F Sender Producer And Credit Budget29

Date:2026-11-09. Status: implementation/attribution started; no candidate accepted.
Frozen checkout input:2daeae8b002da590d44da8bfc036015a194976a2.
Input:Budget28 measured source42a2a9d9eca07d8c553691262590fc8b4fd9d26e plus its
result/route documentation; final backup SHA is in Budget28 evidence closure.
R1 sole writer, no agents/worktrees; R2 fixed-commit build/test/source peer only.

## Question And Non-goals

Budget28 receiver constructor/post-loop join union19.15%-19.56% is a diagnostic
envelope, not a conservative10% task budget. Max per-lane sum is only6.46%-6.79%
of T_ready; required cleanup and concurrent lanes prevent an automatic pool GO.
Complete the sender half of the dependency: per-file producer management versus
real prepare/read/CRC work, and whether ready work is blocked by FILE_RESULT
credit. Existing result-read wait overlaps useful work and cannot be a saving.

No pool, inline default, incremental manifest, checkpoint/ACK/protocol/schema
change, microbatch or RDMA development in this attribution. Original serializer
and profile0 defaults remain; cached summary stays explicit. S1B held. No new
large-file seed, system memory/network changes or expanded tuning matrix.

## Work Order

1. Inspect sendPersistentTree producer, resultReader, pending mutex/condition,
   ready queue and close/cancel/join ordering; distinguish result wait while
   payload proceeds from lane credit wait with ready work. Trace existing server
   per-batch stats export and task-local environment freezing, then freeze exact
   edit files before implementation. Expected owners:persistent_tree_transfer,
   bounded profile helper, client/server aggregate export and perf driver/tests.
2. Prefer optional per-channel collectors. Count producer constructor and split
   join into real producer work and post-loop tail. Record actual lane credit
   waits only when the sender cannot progress and eligible ready work exists;
   expose empty/closed ready queue and terminal/cancel waits separately.
3. Bound intervals/memory and expose complete versus failed/retried scope. Emit
   aggregate unions/sums/counts, not10K raw member records. No shared hot-path
   recording mutex or inherited perf. Reset receiver/sender scope explicitly per
   V3 batch. Do not compare raw steady clocks across machines or add their unions.
4. Add meaningful accounting/overhead/failure tests; verify default-off, invalid
   mode, empty input, short payload, corrupt CRC, disconnect/cancel, window1/2,
   target conflict and resume. Preserve member/buffer/lease ownership.
5. Commit/push/readback on R1, sync exact commit to R2, same-config build/tests,
   compare3 transfer binary SHA-256 before any two-node transfer.
6. At most3 interleaved observer off/on pairs4KiB x10K under Budget28 contract.
   Require absolute median overhead<=5% and each arm CV<=5%; no retries to tune
   CV. Retain constant existing server diagnostics. Audit content SHA/checkpoint
   CRC outside T_ready, routes/NIC counters, production identity and cleanup.

## Acceptance And Decision

### Frozen Edit Scope And Observation Contract

Implement CPNETFLUX_SENDER_WORK_PROFILE=0|1, default0, validated/frozen at
client entry and server startup before listen/connect. Server option is internal,
not a new wire/CLI capability. Per-connection sender writer and result-reader
own separate bounded collectors, merged after result-reader join; max65536
intervals per channel. Record producer constructor, worker loop, join-work and
post-loop tail, result-read/drain, and classified pending-window waits. Reuse
existing read/CRC timers. Counts expose started, sent and acknowledged members,
range exclusions, failures and truncation. No raw intervals emitted.

For dynamic fresh queues, a read-only snapshot after a window wait reports
whether eligible metadata remains, ignoring exhausted credit. Whole-wait ready
proof is restricted to queues whose eligible ready population only decreases:
no affinity/scheduler, requeue ever, stopped claims, cancel or stale/drained owner.
An end snapshot with remaining work proves work existed throughout this monotone
interval. Empty-at-resume does not prove it was empty throughout; classify as
unproven/empty-at-resume, never subtract it as a gain. Snapshot uses the existing
queue lock only when profiling an actual window wait; no recording lock in the
per-member hot path. Static known-next-file waits are separately supported.

Server aggregates profiles only once all channel send calls have stopped, using
one post-transfer publication lock per channel. Emit batch/attempt/channel scope;
duplicate attempt records or partial/error coverage cannot pass fresh attribution.
Off emits disabled aggregate only, with existing detailed server logging retained
equally. Do not align remote steady clocks with receiver intervals.

Affected files:new sender_work_profile.h/.cpp and unit test; existing
persistent_tree_transfer.h/.cpp, dynamic_file_queue.h/.cpp and its unit test;
tree_transfer_client.cpp; protocol/control/control_options.h and control_server.cpp;
tools/test/run_cpnetflux_tree_serializer_smoke.py; tools/perf/run_small_file_baseline.py;
new tools/perf/run_sender_credit_budget.py and tools/test/run_sender_credit_budget_test.py;
CMakeLists.txt; this task/result and docs/ROADMAP.md/docs/README.md.
Exactly3 off/on pairs, one isolated server per sample so startup-frozen observation
mode changes without production service edits; startup outside T_ready in both
arms.39.0625MiB source reused and audited, each destination removed after audit.

### Gates

MemAvailable>=12GiB, experiment-owned swap0, host pswpout delta0, own payload
<=1GiB per endpoint; explicit .1/.2 binds, no persistent production service use.
Reject incomplete/overlapping/role-confused budgets. A wait is not automatically
removable; name the mechanism, remaining work, new handoff/queue costs and how
concurrency affects end-to-end benefit. Receiver unions alone do not authorize
pooling. Only a conservative>=10% complete-task budget warrants a separate
candidate design and five-pair runtime acceptance; otherwise close that narrow
hypothesis and select the next measured per-file transaction mechanism.

Deliver task/result/route update with exact edit scope, input/measured/final
commits, commands/tests/failures, raw evidence, all3 pairs, resource/cleanup and
GitHub/local mirror receipts. Fixed/adaptive microbatch independently retain
>=20% median wall benefit, tail/recovery and matched-large-file<=5% regression
gates. This task does not claim large-file throughput improvement or100G readiness.

## Pre-measurement Verification

Observer and driver implemented. Old binary fails the new sender_work_profile
summary assertion (red-smoke.log). First build caught a test using generation
instead of WorkerContext.epoch.value; corrected and saved first-build-failure.log.
R1 RelWithDebInfo build passes,58 related CTests pass, and4 additional profile1
interrupted/corrupt/conflict/edge tests pass. Ready tests cover exhausted credit,
unadmitted metadata, stale/drained owner, cancel, affinity and both requeue paths.
Evidence:/home/Su/experiments/CPNetFlux/2026-11-09-v3-sender-credit-budget-29.
Next:commit/push/readback, exact R2 build/test/binary receipt, exactly3 pairs.
