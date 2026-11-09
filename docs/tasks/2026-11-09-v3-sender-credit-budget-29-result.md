# S2F Sender Producer And Credit Budget29 Result

Date:2026-11-09. Status: bounded attribution complete; no optimization accepted.
Input2daeae8b002da590d44da8bfc036015a194976a2.
Measured source987746820559b4203a85fd3132589a2a23d0bbaf, committed/pushed/read
back before R2 synchronization. Final docs/backup SHA lives in evidence closure.
R1 sole writer, no agents/worktrees; R2 exact-commit build/test/transfer peer.

## Decision

For4KiB members the producer has finished before the sender calls join: measured
join-before-worker-exit time is0 in all3 profiled tasks. Producer join tail sums
only3.50-3.85ms per10K files. Thread constructor remains measurable, but its
union with the tail is12.45%-12.65% of T_ready; maximum single-lane sum is only
3.83%-3.89%. This is an envelope, not a conservative10% task saving. Scheduling
before worker entry overlaps constructor and first-payload waits; no claim that
all thread reuse opportunities are exhausted follows from these numbers.

9719-9744 pending-window waits per task occur with work proven continuously
ready. Their cross-lane union is34.85%-35.66% of T_ready. This means at least
one lane is blocked, NOT that all four lanes or the entire task are idle. It
includes remote commit/result service and local wakeup/scheduling; readResult
waits overlap useful work. Increasing the window or changing ACK behavior does
not automatically eliminate receiver work; prior S0 explicit8 results remain
insufficient and are not reopened here. No pooling/window/ACK candidate passes.

Next:S2G receiver transaction work audit30, one bounded round targeting necessary
versus redundant begin/checkpoint/publish work. No further broad thread/queue
sweep. If no concrete mechanism with a conservative10% task budget emerges,
pause this per-file attribution branch and record its boundary before choosing
a different architectural direction. Do not enter another automatic profile loop.

## Implementation And Scope

- CPNETFLUX_SENDER_WORK_PROFILE=0|1 defaults0 and validates/freezes before client
  connection or server listen. Receiver-role CLI summary says not_used_receiver.
  This internal diagnostic does not change protocol capabilities or CLI policy.
- Writer and result-reader own separate collectors; merge only after reader
  join. Each channel retains at most65536 intervals. Producer lifecycle, actual
  worker-loop work, join split, four window-wait classes and result read/drain
  remain separate. Existing source read/CRC timers are reused as nested sums.
- Fresh queue readiness snapshot ignores exhausted credit, counts admitted and
  unadmitted metadata, and permits monotone proof only without affinity,
  scheduler, any historical requeue, stopped claims, cancellation or stale/drained
  owner. Positive remaining work after a monotone wait proves it existed during
  that wait. Empty-at-resume does not prove the entire wait had no ready work.
- No shared recording mutex in per-member instrumentation. Server takes one
  post-transfer publication lock per channel and calculates global lane unions
  only after all send calls/result-readers stop. Batch ID, channel/attempt counts,
  started/sent/completed members, failed results, excluded ranges and truncation
  expose partial coverage. Duplicate attempt records cannot pass fresh scope.
- Raw intervals are not emitted. Per-batch aggregate is emitted in both modes
  (disabled in0); existing detailed server phase logging is kept in both arms.
  Publication, interval aggregation, JSON and runtime teardown remain inside T_ready.
  No transfer semantics, integrity, leases, concurrency, cadence or system policy
  changed. No CPU/cache contention or cross-machine clock subtraction is claimed.

## Tests And Reproduction

Evidence:/home/Su/experiments/CPNetFlux/2026-11-09-v3-sender-credit-budget-29.
Old binary fails the new summary assertion (red-smoke.log). Initial build failed
because a new stale-owner test named generation instead of epoch.value; corrected
after reading WorkerContext and retained first-build-failure.log. No experiment
failed or was retried. Both RelWithDebInfo builds pass,58 related CTests per
node pass, and4 additional profile1 recovery tests per node pass. Diff whitespace
check passes with the existing mixed-CRLF convention.

Tests include both requeue paths, exhausted credits, unadmitted ready metadata,
affinity/stale/drained/cancel states, queue concurrency/batch ownership, bounded
and overlapping accounting, join split, incomplete/failure coverage, invalid
server/client selectors, bidirectional transfer/resume, empty/small data,
interruption/corrupt partial checkpoint/target conflict and sender-inline behavior.
Driver tests reject incomplete or duplicate attempts, wrong roles, inflated
unions, unknown readiness, swap, observer overhead and instability.

```sh
cmake --build build -j8
ctest --test-dir build --output-on-failure -R 'SenderWorkProfile|MemberWorkProfile|DynamicFileQueueTest|PersistentDirectoryBatchTest|cpnetflux_(sender_credit_budget_test|member_work_budget_test|tree_serializer_smoke|tree_state_budget_test|tree_serializer_ab_test|small_file_baseline_driver_test|summary_path_ab_test|tree_resume_smoke|tree_pending_window_smoke|tree_manifest_coalesce_smoke|whole_file_observability_smoke|v3_interrupted_resume_smoke|v3_corrupt_partial_checkpoint_smoke|v3_target_conflict_idempotency_smoke|tree_edge_cases_smoke|tree_manifest_corrupt_smoke|sender_inline_smoke|sender_execution_invalid_smoke)'
CPNETFLUX_SENDER_WORK_PROFILE=1 ctest --test-dir build --output-on-failure -R 'cpnetflux_(v3_interrupted_resume_smoke|v3_corrupt_partial_checkpoint_smoke|v3_target_conflict_idempotency_smoke|tree_edge_cases_smoke)$'
python3 tools/perf/run_sender_credit_budget.py --evidence /home/Su/experiments/CPNetFlux/2026-11-09-v3-sender-credit-budget-29 --payload-root /dev/shm/cpnetflux-s0-sender29 --commit 987746820559b4203a85fd3132589a2a23d0bbaf
git -c core.whitespace=cr-at-eol diff --check
```

Same config/commit and all3 binary SHA-256 values match across both nodes:

| Binary | SHA-256 |
|---|---|
| server | 590b15a434535e13d4c0899e4d871788b9f43170611fc85ae31682753fef50df |
| tree-download | 8bb2e1f2ddbdb187b2054b72801f123d5d99bc7363f0e15a5463e7893f8b006b |
| tree-upload | 6ca9851c3c113c5289510225bb088ddc2a8fb198eae8b797fd8ccd6cf24c8714 |

## Exactly Three Observer Pairs

39.0625MiB source,4KiB x10K, R2 resident tmpfs to fresh R1 tmpfs; four explicitly
bound TCP channels through eno17095np0. Queued sender/receiver, pwrite,
read0/write0/depth2/batch1,64MiB budget, dynamic scheduling/pending2,
CRC32C/verified_chunks/checkpoint16/commit_sync=none, TLS/compression off.
Original serializer, explicit cached summary, receiver/tree/sender CLI profiles
and client phase/detail off. Only isolated server sender observation is toggled.
One server per sample freezes startup mode; startup is outside T_ready for both
arms. Source is reused/audited. Task time is independent spawn-to-exit, including
summary hashing; independent SHA/checkpoint audit is after exit. No perf events.

| Pair | Observer0 T_ready(s) | Observer1 T_ready(s) |
|---|---:|---:|
| 1 | 1.206486245 | 1.209188939 |
| 2 | 1.187859439 | 1.226416666 |
| 3 | 1.179979603 | 1.214220971 |
| Median | 1.187859439 | 1.214220971 |
| Population CV | 0.9328% | 0.5945% |

Median observer overhead2.21925%; <=5% and both CV<=5% pass. It is not an
optimization gain. Client summary hashing costs168-211ms across these samples;
reported elapsed_seconds excludes that tail, so it is not used as T_ready.
Do not compare medians against other rounds or derive large-file performance.

| Sender observation | Per-task result (profile1) |
|---|---:|
| Producer constructor union | 148.47-152.63ms |
| Actual producer loop union; includes prepare/push | 57.35-59.78ms |
| Join before producer loop exit | 0ms |
| Producer join-tail sum / union | 3.50-3.85ms / 3.45-3.79ms |
| Constructor + tail union / T_ready | 12.45%-12.65% |
| Max lane constructor + tail sum / T_ready | 3.83%-3.89% |
| Proven-ready window waits | 9719-9744 calls |
| Proven-ready window-wait union / T_ready | 426.46-432.99ms / 34.85%-35.66% |
| Empty-at-resume waits | 4 calls; union0.21-0.29ms |
| Unproven/terminal waits | 0 calls |
| Result-read union | 449.18-455.87ms |
| Final result-reader drain union | 0.34-0.37ms |

Each profiled task covers all10000 started/sent/successfully acknowledged members,
zero ranges/failures/truncation,4 channels and exactly4 attempt records. Source
read sum23.82-26.15ms, CRC sum3.27-5.92ms, worker-entry latency sum289-298ms.
Those are overlapping/nested diagnostics, not pool savings. These4KiB files
fit one payload frame and the two-slot pipeline; conclusions do not apply to
large-file producer joins. Do not add sender/receiver thread envelopes or assume
credit wait is all-lane idle. A removable mechanism and discounted dependency
budget remain prerequisites for a performance candidate.

## Integrity, Resources And Cleanup

All6 tasks pass V3 terminal states,60000 independent output file SHA checks,
60006 independent checkpoint body CRC checks,10000 committed member manifests
and one completed tree manifest per task. Final source SHA remains
b6bbe4bba0fbc7889bd3b75aecf82308495a08233016f3915346d767a63613fe.

Min MemAvailable R1 30200438784 bytes, R2 29450940416 bytes, above12GiB.
Owned VmSwap peak0 and host pswpout deltas0 for all samples. Receiver RSS peak
50376704 bytes; server RSS peak267153408 bytes. Payload per endpoint<=39.0625MiB,
well below1GiB. Both routes explicitly use .1/.2 through eno17095np0; all captured
error/drop/miss counter deltas0. Production PID/start identities and CPU/RSS/swap
snapshots unchanged:126766/1191040 and164834/1679976. Existing production swap is
not experiment-owned. No network/cache/THP/swap policies changed.

All6 destinations removed after audit, all6 isolated servers stopped, source
and both marked payload roots removed; source-cleanup/peer-finish confirm it.
R1's two unrelated worker-state01b files preserved. Evidence remains outside Git.

## Progress Boundary

S2E/F receiver/sender lifecycle attribution is complete for this fresh-small-file
contract. No conservative>=10% thread candidate or>=20% microbatch budget is
accepted. S2G30 is one bounded receiver transaction audit, not authorization to
change save ordering or weaken checks. FileTransaction publicationMutex is per
transaction, not a global serialization lock; no global-lock claim is supported.
S1B default promotion, large-file30-50/70-85/100Gbps and historical NIC+94 remain
open; RDMA remains paused. More aggregate counters are not throughput progress.
