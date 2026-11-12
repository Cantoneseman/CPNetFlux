# S2D Tree State Budget27 Result

Date:2026-11-09. Completed: optional low-overhead attribution and3 observer pairs.
Decision: no incremental tree format/copy-relocation candidate from this budget.
Input:f5cd9f56f474594a9fd01b69001c64f438bfa553.
Measured source:0fa2cb767f43d83668eeea0059755124b08156d4, pushed/read back.
Plan:[Budget27](2026-11-09-v3-tree-state-budget-27.md).

## What Changed

Task-frozen CPNETFLUX_TREE_MANIFEST_PROFILE=0|1, default0, validates before
connection in both clients. Original serializer remains default; buffered was
not remeasured. Existing ManifestSaveStats/OptionalManifestTimer now optionally
measure tree serialization record preparation (copy/sort), format/body CRC and
temp write/rename. Snapshot copy and scope-exit destruction retain their original
state-lock ownership. No save cadence/schema/protocol/recovery change.

Bounded in-memory monotonic ranges capture snapshot copy/destruction holds and
callback waits. After producers stop and writer finishes, union/intersection
counts overlapping lanes once. Caps:65536 callback ranges/4096 hold ranges per
persistent attempt; truncation invalidates attribution. No raw range output,
per-file logging, inherited perf events or thread scheduling changes. Disabled
mode does not reserve range buffers or take optional timer readings.

JSON tree_manifest_profile explicitly scopes these counters to persistent writer
attempts, excluding initial and preflight saves. Final reconciliation and finish
barrier have separate non-overlapping parent intervals. Save/serialize/format
totals are nested and overlap transfer; they must not be added as wall savings.
Snapshot destruction timing includes iteration scope-exit cleanup.

## Tests And Fixed Build

Both nodes built same commit/RelWithDebInfo/options/version0.1.0 and matched all
three transfer binary SHA-256 before transfer. Server:
1e2f74f63f4293b5fa45cfaea064f87a0071d1034822dbe9143bde6fb5accc65;
download:f0a1a8811c5feb08d36812ed19858f9616b158ad3a8cc931f2b2df43f386687a;
upload:f9c25f6fd4e8d81bca9487bcbfd6869efa55b7e00c081b4e29fab86d4a44b8df.

Each node related CTest21/21 and profile-on recovery/coalesce/changed/conflict/
corrupt4/4 pass. New C++ checks cover interval union/intersection, zero/touch/
overlap/invalid ranges, bounded truncation, unchanged serialized bytes with
timers on/off, snapshot accounting and failed save visibility. Existing byte
oracle8 checks remains passing. CLI10 tiny transfers cover default0/explicit0/1,
both-direction cross-mode resume and invalid profile/serializer rejection before
connection. Selector test first failed against input build (missing profile JSON).
New driver tests5 checks cover incomplete pairs, truncation, invalid accounting,
observer overhead/variance and rejection of automatic design acceptance.

## Declared Observer Matrix

Command:python3 tools/perf/run_tree_state_budget.py
--commit 0fa2cb767f43d83668eeea0059755124b08156d4
--evidence /home/Su/experiments/CPNetFlux/2026-11-09-v3-tree-state-budget-27/formal
--payload-root /dev/shm/cpnetflux-s0-state27.

Only3 interleaved profile0/profile1 pairs, alternating order. R2 resident tmpfs
source->new R1 tmpfs target,4096 bytes x10000 files (39.0625MiB),4TCP/pending2/
queued pwrite,original serializer,cached summary,phase/detail off,CRC32C/
verified_chunks/checkpoint16,commit_sync=none,TLS/compression off. Independent
spawn-to-exit T_ready includes optional aggregation/output/exit. Source seeding,
SHA/checkpoint audits and exact cleanup are outside the clock. No retries,
excluded samples, threshold/delay tuning, buffered comparison or host changes.

| Pair | Profile0 T_ready s | Profile1 T_ready s | Direct sync envelope s | Fraction of profiled task |
|---|---:|---:|---:|---:|
| 1 | 1.178605295 | 1.202001438 | 0.041687219 | 3.468% |
| 2 | 1.182982721 | 1.209307809 | 0.040544790 | 3.353% |
| 3 | 1.175355462 | 1.171034223 | 0.041836000 | 3.573% |

Profile0/1 medians1.178605295/1.202001438s, population CV0.265%/1.389%.
Median observer overhead1.985%, within declared5%; each CV<5%. Gate passes,
all ranges untruncated. This is a small observer-validation matrix, not a
five-pair performance acceptance or a cross-round throughput improvement.

## Actual Work And Blocking Budget

| Profile1 component (median across3 tasks) | Seconds |
|---|---:|
| Snapshot copy under state lock | 0.0255064 |
| Snapshot scope-exit destruction under state lock | 0.0143790 |
| Background save total (nested) | 0.4164500 |
| Serialization total (inside save) | 0.3984390 |
| Record copy/sort (inside serialization) | 0.0678884 |
| Record format (inside serialization) | 0.1986760 |
| Body CRC (inside serialization) | 0.0007331 |
| Temp write (inside save) | 0.0110329 |
| Rename (inside save) | 0.0040850 |
| Callback wait union, lanes counted once | 0.0464470 |
| Callback waits intersecting snapshot holds, union | 0.0326935 |
| Final reconciliation, including lock/sort/dirty | 0.0006632 |
| Finish barrier | 0.0070150 |

Snapshot-copy counts625585/615778/620836 records; each original serializer copies
the same records again for ordering. Snapshots210/205/208 and serialized bytes
51.97/51.15/51.57MB. All tasks have20001 state events, threshold32/delay100ms.
Profile0 snapshot median211; profile1 median208. Profile0/1 median summed writer
interval0.412231/0.416498s and summary hash0.168990/0.167454s. Counts may change
with event/writer timing; no policy was adjusted to force count equality.

Direct sync envelope = union(callback waits intersecting copy/destruction holds)
+ all final reconciliation + all finish barrier. Producers have stopped before
the latter two intervals; they do not overlap callback ranges. Its median is
3.468%, maximum3.573% of profiled T_ready. Even generously including every
callback wait, not just overlap with snapshots, gives only4.490%-4.744%.

These envelopes deliberately include work that may be mandatory or unremovable;
overlap is not proof of each lane's global critical path. They exclude indirect
CPU/cache contention, scheduler delays and arbitrary architecture changes and
are not universal speedup bounds. They do show that the measured synchronous
copy/destruction/final tail does not establish a>=10% conservative benefit budget.
The~0.416s background sum is not a0.416s removable critical path. No>=20%
microbatch budget follows from it either. Runtime26's failed full-task candidate
and this small direct-blocking envelope do not justify an incremental format or
another destructor/copy micro-optimization matrix now.

## Integrity, Resource And Cleanup Evidence

All6 transfers finish on V3 with all60000 payload files independently SHA-checked.
All60006 member/tree body CRC audits pass; final tree CRC crc32c:a3f149fc,
canonical SHA b6bbe4bba0fbc7889bd3b75aecf82308495a08233016f3915346d767a63613fe.
Source final SHA matches. Member/tree committed states/ranges/chunks retained.

R1 min MemAvailable30,059,737,088 bytes, owned RSS peak49,651,712 bytes;
R2 min29,607,071,744 bytes, isolated server RSS peak284,155,904 bytes. Experiment
processes VmSwap0 and both hosts pswpout delta0. Source/output each39.0625MiB,
below1GiB cap. Explicit .1/.2 routes use eno17095np0; both interfaces have zero
delta in recorded errors/drops/missed/CRC/length/over counters. Scope17's older
+94 incident remains unresolved. Production PID/start identities are unchanged.
All6 slots, source and marked roots removed; /dev/shm empty on both nodes.
This round's fault tests clean their own payloads; no old evidence was deleted.

## Disposition And Next Work

S2D budget assessment complete; incremental tree-state implementation NOT started.
Original serialization/default0 profile stay in place. S2C NO-GO and S1 limited
cached-summary acceptance are unchanged; S1B default/large-file gate still held.
Next:[Per-file Budget28](2026-11-09-v3-per-file-budget-28.md): reassess per-member
execution setup, checkpoint/publication and FILE_RESULT dependency with bounded
aggregate observation. Do not infer thread-pool value from scope24's perf
inheritance artifact, disable integrity, or implement batching before its budget.

No new large-file Gbps result;30-50/70-85/100G and RDMA remain open. Raw evidence
on R1 under the task directory, peer-capture includes R2 tests/server receipts.
Final GitHub SHA readback, peer state and SHA-verified local mirror are recorded
in closure.json. Windows mirror:_codex_stage27.
