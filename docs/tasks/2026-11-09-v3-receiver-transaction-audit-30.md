# S2G Receiver Transaction Work Audit30

Date:2026-11-09. Started/completed:2026-11-10. Status: audit complete; performance branch paused.
Exact input:8a781d134b551d36385c4e6c6bd3e506768d2c1f.
Result:[Audit30](2026-11-10-v3-receiver-transaction-audit-30-result.md).
No runtime edits or new performance runs.52 related CTests and bounded local
publication-state controls checked. Concrete>=10% removable budget not established.
Next:[Publication31](2026-11-10-v3-publication-reconcile-31.md), a separate
correctness prerequisite for the reproduced link-before-committed-save gap.
Input:Budget29 measured source987746820559b4203a85fd3132589a2a23d0bbaf plus
result/route docs; final backup SHA is in Budget29 evidence closure.json.
R1 sole writer; no agents/worktrees. R2 exact-commit build/test/source peer.

## Question And Limit

### This Round Scope And Acceptance

Trace the fresh4KiB x10K download contract at the exact input. Reuse Budget28/29
evidence only for that same implementation/configuration; do not compare their
task medians. Produce a source-line count/ownership/crash-boundary ledger and
validate its save counts against existing raw observations and current related
CTest cases. A candidate needs both a removable mechanism and a conservative
>=10% complete-task budget. Otherwise close this one audit and pause the
per-file attribution branch, without adding another profiler or runtime knob.

Non-goals: removing required saves, changing ACK/format/durability/recovery,
root-check security changes, thread/window/queue expansion, microbatch/runtime
implementation, bypassing content checks/CRC-reuse threshold, system policy
changes, native RDMA or new throughput claims. No two-node payload is needed
unless a justified observer or candidate is actually developed.

Affected files initially: this task, its result, docs/ROADMAP.md, docs/README.md.
Audit closure also adds Publication31's focused repair handoff after reproducing
the existing publication gap; no runtime implementation is included in Audit30.
Any implementation/test edit must be selected and frozen after source inspection.
Read-only evidence and verification scripts stay under
/home/Su/experiments/CPNetFlux/2026-11-10-v3-receiver-transaction-audit-30.
Acceptance: trace ledger, raw-evidence consistency and related CTests pass,
record a go/pause decision, explicit commit/push/readback, exact-commit R2 sync,
and verified local source/evidence recovery mirror. Preserve unrelated files.

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
