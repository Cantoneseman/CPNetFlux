# S0 Small-file Baseline21 Result

Date: 2026-11-09. Status: S0 complete; runtime/defaults unchanged.
Input `8c887f09aea2847e68fdd6fc494fd7b52667ca04`.
Driver `cb6c8bdd9bf689b31cc6dc1a6c715889b231ff3b`; corrected formal driver
`522fb68d7f65e08fc1c3359331d23ea702aee2c4`. Both pushed, GitHub SHA read back.
The result-document commit follows these fixed experiment commits.
Scope and commands: [task](2026-11-09-v3-small-file-baseline-21.md).

Later measurement correction: [Summary Path22](2026-11-09-v3-summary-path-22-result.md)
found that this driver timestamped the observer-loop end, potentially including
ss work after child exit. The numbers below are retained historical measurements,
not the corrected independent-exit contract. Do not calculate gains against S1's
formal-v2 using these rows. Integrity/resource evidence is unaffected.

## Completed Work And Evidence

Added `tools/perf/run_small_file_baseline.py`, filesystem ownership/cleanup and
aggregation tests, and a CTest entry. No transfer implementation changes.
The driver compares committed source/build/binary receipts, generates bounded
deterministic files, measures client spawn through exit, checks independent SHA
and committed manifests, supervises resources and removes exact owned payloads.
Original summaries/server logs are compressed; phase aggregates retain scope.

Both nodes built server/tree-upload/tree-download/unit-test targets with identical
RelWithDebInfo configuration, tests/RDMA/TLS/ISA-L/io_uring ON and desktop/
cross-domain tests OFF. Version0.1.0 and all three binary hashes matched:

| Binary | SHA-256 |
|---|---|
| server | 7e8a6651afb1cca803e60308f56853938c47bbebd7f4620b7e4a0639ffc2cd1b |
| tree download | 0e29ea956180e25c57abec23f299c6f65e7434a83b4beab0a8bd9161ee2e9611 |
| tree upload | 70c0eaf7c120ff7fd7cd157a4faa7d7fb409caf5c1d09668d8bd42ec739d79ba |

Both nodes passed9 CTest gates: driver, receiver-policy, pending-window,
manifest-coalesce, control-reuse, V3 unified-executor, interrupted-resume,
corrupt-partial-checkpoint, tree-edge-cases. The driver suite has4 checks.
After the driver correction, driver/pending-window2 gates passed again per node.
A32-file plumbing smoke passed SHA/manifests and was excluded from results.

Formal evidence on R770-1:
`/home/Su/experiments/CPNetFlux/2026-11-09-v3-small-file-baseline-21/revision1`.
`matching-builds.json`, `results.json`, `summary.json`, `analysis.json`,
`samples/*.receipt.json`, compressed originals and copied R2 `peer/` are retained.
Parent directory preserves the initial attempt, pilot and test logs. No payload
or large experiment output enters Git. Windows `_codex_stage21` is a review copy.

## Measurement Contract And Corrected Assumption

R770-2 resident tmpfs source -> R770-1 fresh tmpfs output, TCP on .2/.1 with
explicit local bind,4 file workers, queued sender/receiver, POSIX direct pwrite,
1MiB chunk/64KiB buffer, CRC32C/auto, verified_chunks/checkpoint16, TLS off,
commit_sync=none. No RDMA or disk-persistence claim. Each source/output<=1GiB.

T_ready includes client setup, transfer, checkpoint/publish, JSON summary hash and
output. Independent SHA, source generation/readiness and cleanup are outside.
Resources are sampled about every10ms; task-local monitoring is common to all
samples. Sampled CPU is a lower bound, not task CPU attribution. Client
`elapsed_seconds` ends before summary hashing/output and is not T_ready.

The initial attempt stopped at4KiB x10K: application completed10K files, but the
driver expected auto8, while runtime selected2. Its independent integrity gate
did not complete. Earlier samples and this failure are retained, excluded from
the revised matrix. Both failed-attempt roots were removed before continuing.

Source root cause: fresh `runTreeDownloadClient` supplies an empty manifest to
`selectPersistentDispatchWindow` before remote prepare populates file statistics.
Thus fresh-download auto selects2 even for10K small files. The documented
>=10K/average<=64KiB rule needs an already populated manifest. This is a policy
coverage gap, not a reason to claim existing download auto8 or change defaults.
Formal matrix records auto2 and adds explicit8 at all three10K sizes:33 samples
(21 baseline,3 timing-on,9 fixed8), all on522fb68 with matching binaries.

## Full-task Results

Three repetitions per row; medians below. CV is population CV; all formal groups
were0.40%-2.90%. Three-sample interpolated p95 is descriptive, not tail acceptance.
Gbps is logical payload/T_ready. Auto selected2 in every fresh-download row.

| File size/count | T_ready (s) | Gbps | Summary hash (s) | Hash source |
|---|---:|---:|---:|---|
| 4KiB x1K | 0.225437 | 0.145 | 0.113437 | filesystem_read |
| 16KiB x1K | 0.229099 | 0.572 | 0.119783 | filesystem_read |
| 64KiB x1K | 0.247042 | 2.122 | 0.125246 | filesystem_read |
| 256KiB x1K | 0.167084 | 12.552 | 0.019373 | verified_chunks |
| 4KiB x10K | 2.590571 | 0.126 | 1.579890 | filesystem_read |
| 16KiB x10K | 1.197031 | 1.095 | 0.163411 | verified_chunks |
| 64KiB x10K | 1.335977 | 3.924 | 0.183775 | verified_chunks |

Fixed8 contextual comparison versus auto2, median T_ready reduction:
4KiB x10K **-2.107%**,16KiB **+2.003%**,64KiB **+2.310%**.
Paired signs were mixed at every size. No optimization gate is passed; do not
promote8 or present old window1 gains as this round's results.

At4KiB x1K, receiver phase-timing-on median0.266304s versus0.225437s off:
**+18.128% overhead**. Keep detailed client timers out of baseline. Server basic
directory timings are always emitted by existing runtime and common to both arms;
their overhead is not separately isolated here.

## Attribution And Next Development Decision

For filesystem_read groups, median hash/T_ready ratios are50.45%-61.22%.
4KiB x10K pre-summary elapsed is1.00248s, summary hash1.57989s, full2.59057s.
The nonmonotonic cross-size results are consistent with the existing128MiB hash
reuse threshold:256KiB x1K uses reuse,64KiB x1K does not. This is not a paired
threshold experiment and does not justify enabling reuse unconditionally.

`computeTreeVerificationHash` currently invokes `filesystem::relative` in its
sort comparator, repeatedly, after the traversal already resolved relative paths.
The standard filesystem operation may resolve paths/query metadata; sorting
multiplies it by O(N log N) comparisons. Exact syscall/CPU counts have not been
measured. The measured summary interval gives a credible budget for one bounded
candidate: cache each validated relative name once, sort those immutable names,
and retain the same content reads and canonical CRC stream. Do not remove
verification, bypass malformed paths, or conflate this with CRC reuse.

Manifest costs remain relevant: auto10K rows recorded median219-265 snapshots,
55.3-71.5MB serialized and0.408-0.533s writer interval sums.4KiB x1K detailed
receiver intervals summed about0.0578s manifest and0.0441s finalize. Sender
file_result intervals dominate sender file wall sums. These intervals overlap
across files/workers and include receiver/ACK waits; none is CPU time or an
independently subtractable critical-path budget. S0 has not isolated individual
open/commit syscalls and does not prove member microbatch's full-task benefit.

Next is **S1 summary relative-path caching**, specified in
[Summary Path22](2026-11-09-v3-summary-path-22.md). Validate exact hash/behavior
first, then same-commit five-pair full-task A/B with a baseline/candidate selector.
Keep auto2/fixed8 behavior frozen to isolate the mechanism. Resolve auto policy
semantics separately, only with missing remote-statistics design and tests.
After S1, remeasure remaining fixed work before fixed-member/adaptive microbatch.

## Integrity, Resources And Remaining Limits

All33 formal samples passed canonical independent SHA/member-count/bytes checks,
V3 TCP endpoint checks, committed CRC32C member manifests and completed tree
records:195,000 members/manifests and33 tree manifests checked. Download-receiver
pending-high-watermark0 is not observed sender pending depth.
Memory minimum26.691GiB, owned process RSS peak340.906MiB, owned VmSwap0 and
pswpout delta0. Source/output payloads and marked roots removed on both nodes.
Existing production PID/start pairs126766/1191040 and164834/1679976 unchanged;
their preexisting swapped pages are outside the owned-process gate.

Routes, endpoint summaries,60 client TCP snapshots and both interfaces' counters
are retained. Recorded interval rx_missed/error/drop deltas were0 on both ends.
Global interface counters are not per-connection attribution; scope17's old+94
remains unexplained. No network settings, production service or RDMA changes.

S0 supplies a measured baseline and a narrower next mechanism; it is not a new
throughput optimization. The large-file result remains about27.3Gbps in its
existing restricted memory/TCP scope;30-50/70-85/100Gbps gates remain unmet.
