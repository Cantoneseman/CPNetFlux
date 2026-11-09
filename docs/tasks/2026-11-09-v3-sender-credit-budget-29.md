# S2F Sender Producer And Credit Budget29

Date:2026-11-09. Status: next task, not started.
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
