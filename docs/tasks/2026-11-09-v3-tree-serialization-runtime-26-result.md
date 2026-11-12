# S2C Tree Serialization Runtime26 Result

Date: 2026-11-09. Decision: full-task performance NO-GO; original remains default.
Scope: explicit buffered tree-manifest serializer, not microbatch or RDMA.
Input: 0d01a933ddd6e43db10fa4713ce330dc99cad68d.
Implementation: 505872ec72251720d1fa8ebe026049e51f64b602.
Measured source: 01529623d2cbd52dff35b02abab906ad9cf17306, pushed/read back.
Plan: [Runtime26](2026-11-09-v3-tree-serialization-runtime-26.md).

## Implementation And Correctness

CPNETFLUX_TREE_MANIFEST_SERIALIZER=original|buffered is parsed/frozen once at
each tree client entry before connection. Every client tree save uses that task
choice: initial, changed-file preflight, reconnect reset, background and finish.
JSON records requested/effective selection and all-client-save-attempt scope;
selection is not a claim that every attempt succeeds. Programmatic serializers
and saves default to Original without reading the environment. The diagnostic
candidate calls the same core Buffered implementation; there is no duplicate
benchmark-only implementation.

Buffered uses a reserved string, to_chars and direct hex appends. Strictly ordered
snapshots borrow records; unordered/equal-key snapshots retain the original copy
and std::sort comparator. Validation, CRC, disk format, snapshot frequency,
atomic rename, member manifests, V3 leases, integrity and recovery are unchanged.
This is an opt-in research implementation, not a recommended faster configuration.

R1: new selector/oracle/A-B CTests3/3; related original tests28/28; buffered
resume/coalesce/corrupt4/4; buffered conflict/changed/S0-driver3/3. Coalesce covers
four SIGKILL/resume points, completed-output corruption and atomic-save failure.
New CLI exercises10 tiny transfers including both-direction cross-mode resume,
plus2 invalid selectors rejected before connection. The independent oracle has8
checks; core series verification confirms450/450 full byte/parsed-field matches
and unchanged input across sorted/reversed225-snapshot sequences.

The later independent checkpoint CRC audit has a known CRC32C vector and a
corrupted-body rejection test (A-B test now6 checks). It initially failed with
missing audit functions, then passed. S0/A-B CTests2/2 passed on both nodes.
R2 fixed-commit acceptance: related27/27 and buffered coalesce/changed/conflict3/3.
No default promotion, network/THP/cache/sysctl changes or production-service restart.

## Frozen Complete-Task Comparison

Command: python3 tools/perf/run_tree_serializer_ab.py
--commit 01529623d2cbd52dff35b02abab906ad9cf17306
--evidence /home/Su/experiments/CPNetFlux/2026-11-09-v3-tree-serialization-runtime-26/formal
--payload-root /dev/shm/cpnetflux-s0-serializer26.

R770-2 resident tmpfs source -> new R770-1 tmpfs target,4096 bytes x10000 files
(39.0625MiB),4TCP/pending2/queued pwrite,CRC32C/verified_chunks/checkpoint16,
commit_sync=none,TLS/compression off,cached summary,phase/detail off. Five
interleaved pairs, alternating first arm. Both peers have the measured commit,
matching build configuration and all three transfer binary SHA-256 values.
Timing is independent spawn-to-exit T_ready, including summary/output/exit;
seed preparation, independent SHA/checkpoint audits and cleanup are outside it.
No matrix retries, excluded samples or snapshot-frequency tuning.

| Pair | Original seconds | Buffered seconds | Buffered wall change |
|---|---:|---:|---:|
| 1 | 1.174487764 | 1.212846200 | +3.266% |
| 2 | 1.183301595 | 1.201179254 | +1.511% |
| 3 | 1.192998619 | 1.237196023 | +3.705% |
| 4 | 1.151359927 | 1.200647604 | +4.281% |
| 5 | 1.184778288 | 1.230715879 | +3.877% |

| Metric | Original | Buffered |
|---|---:|---:|
| T_ready median seconds | 1.183301595 | 1.212846200 |
| T_ready population CV | 1.213% | 1.236% |
| Descriptive p95 seconds (five samples) | 1.191354553 | 1.235899994 |
| Median background/final snapshots | 220 | 304 |
| Median serialized bytes | 54,420,165 | 85,932,923 |
| Median summed tree write interval seconds | 0.410254 | 0.391609 |
| Median summed tree state lock wait seconds | 0.122453 | 0.197797 |
| Median filesystem summary seconds | 0.170526 | 0.202320 |

Median wall reduction is -2.4968%, i.e.2.50% slower; all five pairs are slower.
CV passes but the>=10% wall gain and no-worse descriptive p95 gates fail. Do not
compare these times with scope24's phase-on/gated timing or scope25 CPU time.

## Work Amplification And Interpretation

All runs have20001 state events and the same threshold32/max-delay100ms policy.
Actual snapshot counts differ: writer serialization runs outside the state lock;
while it is busy, subsequent events naturally coalesce. A faster save can return
earlier and capture more full snapshots. Here snapshot median rises38.18%, bytes
57.91%, and aggregate state lock wait61.53%, while aggregate tree write interval
falls only4.54%. Source inspection confirms each snapshot copies the full tree
under the state lock. This supports a work-amplification explanation for the
failed candidate; it does not prove each metric's causal share of T_ready.

These write/wait intervals overlap transfer and accumulate across callbacks;
neither they nor the summary change can be subtracted or added as independent
critical-path savings. Scope25's76.25%/59.86% local CPU reduction remains valid
for its fixed synthetic workload, but did not yield a complete-task gain here.
Do not retry thresholds/delays, chase CV, expand threads or combine unrelated knobs.

## Integrity, Resources And Cleanup

All10 tasks complete on V3; all100000 output files match independent SHA, final
tree CRC is crc32c:a3f149fc, and canonical tree SHA-256 is
b6bbe4bba0fbc7889bd3b75aecf82308495a08233016f3915346d767a63613fe.
All100010 member/tree checkpoint body CRCs are independently recomputed after
timing, plus completed states/ranges/chunks checked. Source SHA rechecked last.

R1 minimum MemAvailable30,020,628,480 bytes; client RSS peak50,839,552 bytes.
R2 minimum29,592,846,336 bytes; isolated server RSS peak295,686,144 bytes.
All sampled experiment processes have VmSwap0 and both hosts pswpout delta0.
Own source/output payload each39.0625MiB, well below1GiB cap. Explicit .1/.2
routes use eno17095np0; sampled rx/tx errors, drops, missed/CRC/length/over errors
all have zero delta on both interfaces. This does not close scope17's historical
+94 missed-error attribution. Production PID/start identities are unchanged.
All10 output slots, source and both marked payload roots are removed; /dev/shm
is empty on both nodes. Raw logs/receipts, source-final, cleanup and postflight
remain outside Git in the evidence directory; peer evidence is captured on R1.

## Disposition And Next Work

S2C implementation/correctness/measurement is complete, performance gate failed.
Original remains default; Buffered is retained only for explicit reproduction.
No negative-control/default-promotion/large-file regression matrix is justified
for this failed candidate. S1 cached-summary limited acceptance is unchanged;
S1B default promotion is still held by the earlier resource gate.

Before a further state-saving optimization, establish a conservative task-time
budget for full snapshot copying, ordering and publication without perturbing
cadence. A structural design must reduce work per state event and preserve crash
recovery/final barriers, rather than only speed an individual full rewrite. An
incremental representation is a possible research direction, not an approved
format change or an implemented feature. If that budget is insufficient, close
this branch of optimization and reassess independent per-file fixed costs before
authorizing fixed/adaptive microbatch (each still requires>=20% wall benefit).
Do not add another candidate from aggregate CPU percentages alone.

No new large-file Gbps result. The30-50/70-85/100G targets, matched large-file
regression, reverse/TLS/persistent/soak coverage and RDMA remain open. Final
commit/remote SHA, peer state and local SHA-verified mirror are in closure.json.
