# S2G Receiver Transaction Work Audit30

Date:2026-11-09. Status: next task, not started.
Input:Budget29 measured source987746820559b4203a85fd3132589a2a23d0bbaf plus
result/route docs; final backup SHA is in Budget29 evidence closure.json.
R1 sole writer; no agents/worktrees. R2 exact-commit build/test/source peer.

## Question And Limit

Budget28 measured receive begin/commit unions around22%/24% and30000 member
checkpoint saves for10000 files. Budget29 proves queued-work window blocking,
but neither its35% union nor result-reader wait can be removed by assumption.
Sender producer joins are already after loop exit in this4KiB contract. Do not
expand thread count/window, infer a global publication lock or add cross-host
intervals. FileTransaction publicationMutex is a member of each transaction.

This is one bounded receiver transaction audit. Identify concrete redundant
work inside current semantics before any candidate. If no conservative>=10%
complete-task mechanism emerges, pause this per-file attribution branch, record
the failed budget and choose the next architectural question explicitly; do not
start another automatic broad profiler or parameter sweep.

## Work Order

1. Trace whole-file begin, initial manifest save, chunk recording, flushManifest,
   publish and markCommitted, final integrity audit and summary. Build a source-
   grounded count/ownership ledger: which path checks, session construction,
   temporary opens/writes/renames, metadata checks and content scans occur per
   file, and why. Distinguish member checkpoints from background tree snapshots.
2. Inspect the crash/visibility boundary of every save and publish operation.
   An initial/checkpoint/committed save cannot be removed merely because there
   are three. Preserve corrupt-manifest rejection, missing coverage, target
   conflicts, concurrent members, cancellation and idempotent resume. No format,
   durability, ACK or recovery weakening is authorized by this audit.
3. Select only one candidate mechanism supported by the ledger. Use existing
   optional ManifestSaveStats/member timers first. If finer observation is
   necessary, freeze exact edit files, substep boundaries and counters before
   coding, keep task-local/default-off/bounded aggregates and verify overhead.
   Existing whole begin/commit envelopes are not pure removable work.
4. Include final-audit/summary cost in the accounting: Budget29 summary hash
   itself costs168-211ms and elapsed_seconds omits it. Do not bypass the128MiB
   CRC-reuse policy or repeat an eliminated summary/serializer candidate without
   a distinct mechanism and new budget. Preserve real content reads and checks.
5. Add relevant behavioral/accounting tests before implementation. R1 commit/
   push/readback, R2 exact-commit build/tests and3 binary SHA checks precede
   two-node runs. At most3 observer pairs if needed,4KiB x10K, original serializer,
   explicit cached summary, phase/detail off, window2/queued/pwrite, no inherited
   perf. Observer overhead<=5%, each CV<=5%, no CV chasing or giant payload.
6. Only a specific mechanism with a conservative>=10% task budget permits a
   separate candidate and five-pair complete-task gate. Keep fixed/adaptive
   microbatch>=20%, tail/recovery/matched-large-file gates independent.

## Resource And Delivery Contract

MemAvailable>=12GiB, experiment-owned VmSwap0, host pswpout delta0, payload<=1GiB
per endpoint; explicit .1/.2 TCP binds/routes/counters, untouched production
identities. Audit output SHA and checkpoint CRC after T_ready; remove exact
owned output/source roots and stop isolated services. No system tuning or RDMA.

Affected implementation files must be frozen after source inspection, likely
persistent_tree_transfer, session/checkpoint optional observations, related unit/
smoke tests and bounded perf driver. Keep edits scoped to the selected mechanism.
Deliver count/ownership ledger, task/result/route decision, exact source/final
commits, commands/tests/failures, raw evidence, cleanup and GitHub/local mirror.
No performance gain or production readiness is accepted by this task document.
