# S2E Per-file Budget28 Result

Date:2026-11-09. Status: bounded receiver attribution complete; no performance
candidate accepted. Input d59f942e210d6e8596fcdc5bffc0608ee2dae952.
Measured source42a2a9d9eca07d8c553691262590fc8b4fd9d26e, pushed to the verified
Cantoneseman/CPNetFlux origin and read back before R2 synchronization.
Final documentation/backup SHA and mirror checks live in evidence closure.json.

## Decision

Receiver thread management has a material diagnostic envelope, unlike the small
tree-state direct synchronization envelope in Budget27. Its union covers
19.15%-19.56% of T_ready, but this does NOT establish a conservative10% task
saving from pooling. Each lane accumulates only72-81ms of constructor plus
post-loop join tail, with the maximum lane sum6.46%-6.79% of T_ready. Neither
the cross-lane union nor this maximum lane sum is the complete task dependency
graph. Required buffer cleanup remains inside the join tail. No thread-pool,
inline-policy change, checkpoint/schema change or microbatch is integrated.

Next:S2F sender producer/result-credit attribution29, to complete the missing
side of the per-member dependency before choosing a removable mechanism and
discounted budget. No receiver-only saving or sender idle/wait saving is assumed.
The current observation is not evidence that all thread reuse is unhelpful.

## Implementation

- CPNETFLUX_MEMBER_WORK_PROFILE=0|1, default0, frozen/validated at client entry
  before connection. No environment lookup or clock read is added per member
  when disabled. No interval collection or detailed member JSON when disabled.
- Each receiver channel owns a bounded65536-interval collector without shared
  recording locks. Six stages: constructor, join before worker-loop exit,
  post-loop join tail, transaction begin, commit/publication, result frame write.
  Worker-entry latency is a separate overlapping diagnostic. Worker timestamps
  are consumed after join; current buffer/work-lease ownership is preserved.
- Aggregate stage sums, interval unions across lanes, per-lane sums and nested
  ManifestSaveStats are emitted after channel workers stop. Raw intervals are
  not emitted. Sorting/aggregation and JSON are included in T_ready.
- Scope: successful completed local whole-file receive attempts. Started and
  completed counts expose interrupted/failed attempts; range exclusions and
  truncation invalidate fresh-full-task attribution. Final integrity audit is
  outside the member scope. Upload summaries explicitly say not_used_sender.
- Wire format, CRC, verified chunks, checkpoint cadence, thread count, pending
  window, commit ordering and atomic publication retain their original behavior.

## Tests And Acceptance Commands

Evidence:/home/Su/experiments/CPNetFlux/2026-11-09-v3-per-file-budget-28.
No raw payload, credentials, binaries or large logs are committed.

Old binary failed the new summary contract assertion (red-smoke.log). Its first
shell wrapper also had an exit-code quoting error; the Python assertion was read
back separately. Subsequent acceptance used literal here-doc SSH wrappers.
Both RelWithDebInfo builds pass, both nodes20 related CTests pass, and each node
additionally passes4 profile1 interrupted/corrupt/conflict/edge CTests.
Coverage includes overlap union, join split, default-off recording, invalid and
bounded intervals, partial scope, nested saves, role/selector validation,
bidirectional transfers and cross-serializer resume. The driver rejects missing
pairs, wrong role/coverage, inflated unions, observer overhead and instability;
a material envelope never automatically passes an optimization candidate.

```sh
cmake --build build -j8
ctest --test-dir build --output-on-failure -R 'MemberWorkProfile|cpnetflux_(member_work_budget_test|tree_serializer_smoke|tree_state_budget_test|tree_serializer_ab_test|small_file_baseline_driver_test|summary_path_ab_test|tree_resume_smoke|tree_pending_window_smoke|tree_manifest_coalesce_smoke|whole_file_observability_smoke|v3_interrupted_resume_smoke|v3_corrupt_partial_checkpoint_smoke|v3_target_conflict_idempotency_smoke|tree_edge_cases_smoke|tree_manifest_corrupt_smoke)'
CPNETFLUX_MEMBER_WORK_PROFILE=1 ctest --test-dir build --output-on-failure -R 'cpnetflux_(v3_interrupted_resume_smoke|v3_corrupt_partial_checkpoint_smoke|v3_target_conflict_idempotency_smoke|tree_edge_cases_smoke)$'
python3 tools/perf/run_member_work_budget.py --evidence /home/Su/experiments/CPNetFlux/2026-11-09-v3-per-file-budget-28 --payload-root /dev/shm/cpnetflux-s0-member28 --commit 42a2a9d9eca07d8c553691262590fc8b4fd9d26e
git -c core.whitespace=cr-at-eol diff --check
```

The three transfer binaries match SHA-256 on both fixed-commit peers:

| Binary | SHA-256 |
|---|---|
| server | 7e183e2771d4dc6a03cccedc69dce920940b8588bcb7b46fbb3bc2cb1fb1a30b |
| tree-download | 9f013111e7ae9ded6a3382b4412f23234b6ecfd690c69fb925fe4547ff5e921f |
| tree-upload | b8bb06c92481b1a74066c605aa9c35c9e4ac90fa860173e943756d164dd90a7c |

## Three Observer Pairs

4KiB x10K (39.0625MiB), R2 resident tmpfs source to fresh R1 tmpfs destination,
four explicitly bound TCP channels over eno17095np0, dynamic file scheduling,
pending2, queued sender/receiver, pwrite, read0/write0/depth2/batch1,64MiB payload
budget, CRC32C/verified_chunks/checkpoint16/commit_sync=none, TLS/compression off.
Original tree serializer, explicit cached summary, tree profile0, phase/detail
off on the client. Existing server phase logging is identical in both arms.
T_ready is spawn-to-exit captured by a waiter, before independent output audits.
No perf events or event inheritance. Exactly three interleaved pairs; no retry.

| Pair | Observer0 T_ready (s) | Observer1 T_ready (s) |
|---|---:|---:|
| 1 | 1.191364452 | 1.192516363 |
| 2 | 1.191362069 | 1.197236304 |
| 3 | 1.170007965 | 1.167175883 |
| Median | 1.191362069 | 1.192516363 |
| Population CV | 0.8501% | 1.1133% |

Median observer overhead0.09689%; <=5% overhead and both CV<=5% pass. This is
observer validation, not an optimization comparison, and is not a large-file
throughput result. Cross-round medians are not used to claim improvement.

| Receiver stage | Cross-lane union range | Fraction of profiled T_ready |
|---|---:|---:|
| Thread constructor | 147.761-150.940ms | 12.39%-12.71% |
| Join before loop exit; includes pending payload | 162.085-168.932ms | 13.54%-14.47% |
| Post-loop join tail; includes required cleanup | 116.428-125.097ms | 9.72%-10.57% |
| Union of constructor and post-loop tail | 228.301-229.251ms | 19.15%-19.56% |
| Transaction begin | 260.134-264.611ms | 21.81%-22.56% |
| Commit/publication | 280.576-289.860ms | 23.53%-24.45% |
| Actual FILE_RESULT write | 29.945-31.910ms | 2.54%-2.67% |

The stages overlap across channels. The thread union is NOT constructor union
plus tail union; join-payload time and entry latency are not added. Transaction
begin/commit contain member save time and mandatory filesystem/recovery work.
FILE_RESULT write is not sender read wait or pending-window credit starvation.
No subtraction of these totals from task time is valid without a dependency
model and a concrete removable mechanism.

Every profiled task covers all10000 members, all60000 stage intervals,
zero excluded ranges/truncation. Each has30000 member manifest saves, all
published without failure,23490000 serialized bytes. Save sum449-468ms,
serialization122-125ms, temp write181-189ms, rename142-149ms. These are nested
per-lane service sums, not removable wall time. Changing serialization alone
does not remove these30K required save attempts; no cadence change is accepted.

## Integrity, Resources And Cleanup

All6 tasks pass V3 terminal state,60000 independent output SHA-256 checks and
60006 independently recomputed checkpoint body CRCs. Each has10000 committed
member manifests and one completed tree manifest. Final source SHA unchanged.
Both routes explicitly resolve .1/.2 through eno17095np0; all captured
error/drop/miss counter deltas are0 on both nodes. This does not resolve the
historical scope17 rx_missed_errors+94 incident.

Minimum MemAvailable: R1 30070784000 bytes, R2 29575933952 bytes, both above12GiB.
Experiment-owned VmSwap peak0, host pswpout deltas0; receiver RSS<=51105792 bytes,
server RSS<=313901056 bytes. Source payload39.0625MiB and each fresh destination
are well below1GiB per endpoint. Production processes retain PID/start identity
126766/1191040 and164834/1679976, unchanged CPU/RSS/swap snapshots; their existing
swap is not experiment-owned. No system memory/network policies changed.

Each output slot, peer source and both marked payload roots removed; isolated
server stopped by its existing supervisor and peer finish confirms removal.
R1's two unrelated worker-state01b documents are preserved. R2 has no source edits.

## Remaining Work

S2F29 measures sender producer constructor/join versus real payload preparation,
then actual credit starvation with ready work. Maintain bounded aggregate
observation and the same three-pair overhead/resource contract. Do not align
two machines' steady clocks or add sender/receiver unions. Only a concrete
mechanism with a conservative>=10% task budget permits a separate candidate and
five-pair runtime gate. Fixed/adaptive microbatch each still needs>=20% benefit
and independent recovery/tail/matched-large-file gates. S1B default promotion,
large-file30-50/70-85/100Gbps, historical NIC incident and RDMA remain open.
