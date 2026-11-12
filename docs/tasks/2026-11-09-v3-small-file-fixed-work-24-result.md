# S2 Small-File Fixed Work24 Result

Date:2026-11-09. Completed: owned-process attribution infrastructure and one
reference/profile pair. No new runtime throughput optimization or batching
acceptance. S1B large-file/default promotion remains held; default original,
cached explicit. No RDMA/network/default changes.

## Source And Reproduction

Input fa937c024206527ad40e9bab2022bfe39a4b6790; measured source
06899f6cc909277bb10d34de7c0f88a2c89b1896, pushed and read back, both peers
same commit/RelWithDebInfo/version0.1.0/binary SHA-256 before transfer.
R770-1 sole writer, R2 fixed-commit source/build/test peer. Added owned_process_profile.py,
run_small_file_fixed_work.py, two test scripts, CMake tests and one explicit
optional runner callback in S0 client_sample. No C++ runtime changes.

Command on R1: python3 tools/perf/run_small_file_fixed_work.py --action matrix
--evidence /home/Su/experiments/CPNetFlux/2026-11-09-v3-small-file-fixed-work-24/formal
--payload-root /dev/shm/cpnetflux-s0-fixed-work-24
--commit 06899f6cc909277bb10d34de7c0f88a2c89b1896.

Scope:4KiB x10K (40,960,000 payload bytes), R2->R1, new tmpfs target, four TCP
connections, explicit .2/.1 data bind, file-parallelism4/pending2, queued sender
and receiver/pwrite, CRC32C/verified_chunks, checkpoint16, commit_sync=none,
compression/TLS off. CPNETFLUX_SUMMARY_PATH=cached, phase summary on in both
arms, detailed range/payload/CPU timers off. Sources/SHA and collector setup,
shutdown/analysis are outside release-to-independent-exit T_ready. This pair's
clock and phase-summary contract differ from S1; no cross-round speedup claim.

## Full Task And Measured Work

| Metric | Reference | Profiled |
|---|---:|---:|
| T_ready seconds | 1.472798411 | 1.736296627 |
| Runtime elapsed before summary hash, seconds | 1.040207154 | 1.328441884 |
| Summary hash, seconds | 0.191778190 | 0.166926565 |
| Remaining startup/output/teardown interval, seconds | 0.240813067 | 0.240928178 |
| Tree state events / persisted generation | 20001 / 20001 | 20001 / 20001 |
| Full tree snapshots | 225 | 320 |
| Serialized tree bytes | 60,330,800 | 94,414,903 |
| Tree write interval sum, seconds | 0.422147 | 0.641858 |
| Tree writer state-lock wait sum, seconds | 0.119695 | 0.152344 |
| Member manifest interval sum, seconds | 0.633078352 | 0.593556982 |
| Member finalize interval sum, seconds | 0.418268843 | 0.357656439 |
| Sender result interval sum, seconds | 2.752063934 | 3.646251703 |

Observation overhead this pair:17.89099% task wall increase. This is n=1,
not an overhead distribution or gain/CV acceptance. Per-file sums overlap each
other and across four workers; finalize contains manifest work. They are not
additive wall or removable CPU budgets. The remaining interval includes basic
JSON output of10K timing entries and other work; it is not fully isolated.

## CPU And Wait Evidence

199Hz cpu-clock, DWARF4096 stacks, acknowledged enable before exec/worker
creation, monotonic perf timestamps. Receiver538 samples/2.7035 sampled CPU
seconds/4,136,440 raw bytes; sender339/1.7035/2,989,208. Perf exit0, no reported
lost events, both below32MiB. Sampled periods are estimates, not exact CPU totals.
Unknown leaf symbols: receiver1.86%, sender19.76%; unwind may be partial.

| Inclusive CPU component | Receiver | Sender |
|---|---:|---:|
| serializeTreeManifest | 23.05% | 0% observed |
| saveDownloadManifestAtomic | 14.68% | 0% observed |
| kernel_clone thread creation | 22.30% | 31.86% |
| perf_event_alloc / inherit_event observer work | 15.24% | 23.89% |
| writeTreeJsonSummary, includes hash/output | 12.64% | not applicable |

These categories overlap: profiler inheritance is inside thread creation.
Subtracting percentages does not reconstruct unprofiled CPU or wall. The generic
network category also matches enclosing call stacks, not pure TCP cost. Raw
stacks are retained; detailed-attribution.json isolates component names.

Thread snapshots show many futex/socket sleeps: receiver reference75.29% and
profile78.29% of thread observations blocked; sender profile84.79%, including
the idle listener and startup/tail. Counts are not wait durations or critical
path attribution. The sender result intervals support receiver-dependent
backpressure, but do not prove network latency alone is limiting. Source code
still waits for per-file results and creates queued payload threads per member.
The expensive perf inheritance prevents using clone samples to justify a pool.

## Interpretation And Next Candidate

The reliable new work count is225 complete tree snapshots/60.33MB for a39.06MiB
payload tree. Source inspection: serializeFiles copies/sorts the full vector,
builds temporary path/error hex strings and streams every record on every save.
TreeManifestWriter also copies the snapshot under the state mutex. This is a
specific CPU mechanism to test, not proof of whole-task savings.

Do not reopen threshold256/max-delay500 tuning: both prior coalescing candidates
failed, see small-file-microbatch-02. Do not implement protocol microbatch or a
thread pool from these samples. A>=20% removable T_ready budget is unproven.
Even treating all reference tree write0.422s as serialized, entirely removable
work gives only a28.66% optimistic ceiling; a50% cut gives14.33% before overlap.

Next: [Tree Serialization25](2026-11-09-v3-tree-serialization-25.md), one bounded
byte-compatible serializer feasibility comparison. It changes computation per
snapshot, not checkpoint frequency/schema/ACK/publish semantics. Local CPU gains
must not be promoted as transfer gains; runtime integration needs its own budget
and fixed-commit full-task evidence. Microbatch/adaptive and100G remain open.

## Verification, Failures And Cleanup

Both nodes19/19 relevant CTests passed at measured source (TreeHash10, driver4,
two new analysis/ownership tests, interrupted resume/corrupt checkpoint/conflict3).
Owned profiler selftests passed on both nodes and observed two threads created
after enable. Final driver tests cover9 checks across the two new scripts.
The final post-processing/export change has focused test and profiler selftest
evidence; do not treat the final documentation SHA as the measured revision.

An export-only R1 selftest failed because raw perf data was chowned before sudo
perf script, which rejects another owner's input. Corrected ordering parses
first, then exports the exact completed artifact to the invoking uid/gid;
selftest-export-r1-v2 passes. No transfer rerun or sample deletion. Initial
cross-host raw-data copy failure was permissions only; raw originals remained.

Both transfers completed10K members, independent source/target SHA tree matched,
CRC summaries matched,10K committed member manifests and one completed tree
manifest per sample, source final SHA unchanged. Min MemAvailable across owned
process observations29,676,036,096 bytes (27.638GiB), owned swap0 and host
pswpout delta0. Both routes correct, interface error/drop deltas0. These zero
deltas do not explain the old scope17 rx_missed_errors+94 incident.

All exact owned payload roots and selftest temporary roots removed; only small
evidence remains. Production PIDs/start identities preserved. Evidence on R1:
/home/Su/experiments/CPNetFlux/2026-11-09-v3-small-file-fixed-work-24,
formal/ is the only transfer pair, peer-capture/ retains R2 proof. Raw perf,
compressed summaries/logs, checksums, resource/route/counter receipts and tests
are retained outside Git; local mirror has a SHA manifest. Final commit/push and
both-node receipts are in closure.json.
