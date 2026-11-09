# S1 Summary Path22 Result

Date:2026-11-09. Status: opt-in cached path passes bounded S1 acceptance.
Default remains original; matched large-file/default-promotion gate is pending.
Input `4f62a137faa0b6d48c3bcc7529204c6ebbf7ef0f`.
Runtime/source commit `c8e1428f4c432f02a4d3ca58b8a531bd3dc71728`;
corrected measurement source `20aef478eda71764eb1876e8196518c1c60e87e1`.
Both pushed with GitHub SHA readback. Result-document commit follows separately.
Scope and exact commands: [task](2026-11-09-v3-summary-path-22.md).

## Delivered Mechanism

Moved the private filesystem tree CRC reader into existing core/tree/tree_hash.
The cached branch resolves/checks each regular entry's relative name during
enumeration, excludes artifacts as before, stores payload path/name pairs, sorts
pure relative strings, and reuses those names while reading the original content.
No filesystem calls in the cached comparator. Resolution is O(entries), rather
than being repeated in O(payloads log payloads) sort comparisons. This is a code
branch/work-count observation, not a measured syscall or DRAM-bandwidth count.
Both arms preserve CRC stream order: relative name, zero, decimal size, zero,
content bytes, zero. Same reads, CRC32C/backend, symlink/nonregular rejection and
V3 transfer/checkpoint/publish semantics. Terminal iterator errors now propagate
in both arms, covered by a permission-denied test. Stable owned trees only;
no new concurrent source-mutation guarantees are claimed.

`CPNETFLUX_SUMMARY_PATH=original|cached` is validated before client connection and
frozen per run. Unset defaults to original. Summary records requested/effective
mode; verified-chunks reuse reports effective not_used. No reuse threshold,
pending-window, DATA frame, queue, transport or global settings changed.

## Testing And Fixed Peers

Both nodes passed21 related CTest cases. These include10 tree-hash unit cases,
canonical reference checks across nested/ambiguous/UTF-8 names and software/auto
backends, empty files/tree, artifacts, symlink/FIFO/missing-root rejection and
directory iteration errors; real V3 upload/download/resume, invalid selector,
pending-window, manifest, recovery, target conflict and directory edge cases.
The new transfer smoke checks7 transfers and2 invalid selectors. Analysis driver
has5 checks; owned-path/aggregation/timer driver now has6 checks, including
post-exit observer exclusion and owned-child timeout reaping.
R1 additionally passed7 cached-mode recovery/receiver gates and the existing
CRC-auto/reuse/fallback smoke (78.34s). Initial smoke fixture failures were
duplicate/invalid CLI arguments and an incorrect no-empty-directory assumption;
they were corrected and logs retained. The invalid selector creates no payload
or checkpoint; CLI parsing may already have created an empty download directory.

Both nodes use version0.1.0, matching RelWithDebInfo feature configuration and
the same committed source before transfers. Matching binary SHA-256:

| Binary | SHA-256 |
|---|---|
| server | 7e8a6651afb1cca803e60308f56853938c47bbebd7f4620b7e4a0639ffc2cd1b |
| tree download | 78a6b8a989ce792cbdc1d1766b4b95f748b91aaabf26cbe37e4216c333de6845 |
| tree upload | cb96d9ff2ae0774f8d83f1b002eb22da36fdf842004c8378daef06dd82f7d633 |

## Timing Correction And Retained First Attempt

First40 samples in formal/ completed all integrity/resource/cleanup checks, but
acceptance failed:64KiB x1K original CV5.217%, reuse-control p95 worsened.
The synchronous collector recorded its loop end, including ss work after the
client exited. The two25-iteration64KiB control samples had19.99/21.13ms beyond
application elapsed+hash; the three24-iteration samples had3.45/5.40/4.18ms.
A deliberately delayed-observer regression reproduced the timing defect before
the fix (measured wall wrongly included the post-exit delay).

20aef47 adds an independent child wait thread and timestamps exit, separately
recording observer_wall_seconds and observer_seconds_after_exit. Regression and
analysis tests passed on both nodes; runtime binaries are unchanged from c8e1428.
Then the entire matrix was repeated once in formal-v2/, after matching fixed
commit/build receipts again. No sample deletion, threshold relaxation or further
CV/p95 reruns. Formal-v2 observed up to22.07ms collector work after exit, excluded
from T_ready. Prior S0/formal times remain historical collector-loop measurements;
do not calculate cross-contract gains from them.

## Validated Full-task Results

Authoritative set: formal-v2/,20aef47,5 interleaved pairs per row,40 runs total.
R770-2 resident tmpfs source -> R770-1 fresh tmpfs output, explicitly bound TCP
.2/.1,4 file workers, queued sender/receiver, POSIX pwrite, CRC32C/auto,
verified_chunks/checkpoint16, fixed window2,1MiB chunks/64KiB buffers, TLS off,
commit_sync=none, detailed timers/profiles off. Each source/output<=1GiB.
T_ready includes full client lifetime through hash/summary exit. Source generation,
independent SHA, readiness and cleanup are outside. Five-sample p95 is descriptive.

| Case | Original median(s) | Cached median(s) | Wall reduction | CV original/cached | Gate |
|---|---:|---:|---:|---:|---|
| 4KiB x1K | 0.219313 | 0.121192 | 44.740% | 4.672%/2.250% | pass |
| 4KiB x10K | 2.610038 | 1.187286 | 54.511% | 0.827%/2.088% | pass |
| 64KiB x1K | 0.243869 | 0.142766 | 41.458% | 1.962%/0.934% | pass |
| 16KiB x10K reuse control | 1.168927 | 1.184739 | -1.353% | 2.703%/1.063% | pass |

All15 filesystem-read pairs improved, each arm CV<=5%, full-task median reduction
>=20%, descriptive p95 no worse, hash duration lower. P95 original/cached(s):
0.239235/0.126724,2.634645/1.230407,0.249992/0.144324.
Reuse-control effective mode was not_used in every run; median variation1.353%
was within its5% gate, CV passed and p95 decreased1.232706->1.200134s.
This is a bounded no-regression check, not proof of a population tail distribution.

For4KiB x10K, summary hash median1.59763->0.169842s (89.37% lower).
Pre-summary application elapsed remained1.00751/1.00531s. The gain is in summary
processing, not faster payload transport. The manifest writer interval sums
remained0.40642/0.414267s with218/216 snapshots. Those sums overlap worker/task
intervals; they do not directly give an eliminable wall or CPU budget.

## Integrity, Resources And Evidence

All40 corrected samples checked independent canonical per-member SHA, count and
bytes, CRC32C committed member manifests,40 completed tree manifests, V3/TCP
endpoints, persisted generation and output cleanup:220,000 payload members.
The retained first40 also passed those checks, for440,000 members in80 runs.
Both sources rechecked before cleanup. All owned roots/output slots and test
servers removed/ended; existing production PID/start identities preserved.

Corrected set sampled MemAvailable minimum27.270GiB, monitored transfer-process
RSS peak290.281MiB, owned VmSwap0 and host pswpout delta0. Across both sets,
minimum27.206GiB and peak317.141MiB. Interface error/drop/missed counter increments
were0 on both ends in recorded intervals. Endpoint summaries/routes and120 client
TCP snapshots across both sets are retained; global interface counts cannot
attribute individual connections or explain scope17's old+94 event.

Evidence root on R1:
`/home/Su/experiments/CPNetFlux/2026-11-09-v3-summary-path-22`.
formal/ is retained first attempt; formal-v2/ holds corrected matching-builds,
results/analysis, per-sample receipts, compressed originals and copied R2 peer/.
Tests, resource-integrity-audit.json, final closure and SHA256SUMS accompany it.
Windows `_codex_stage22` is a verified review/emergency copy, not development root.
No payload, credentials or large outputs in Git.

## Progress And Next Gate

S1 opt-in implementation and bounded small-file acceptance are complete. Enable
cached per task with CPNETFLUX_SUMMARY_PATH=cached; default remains original.
[Default Gate23](2026-11-09-v3-summary-default-gate-23.md) is next: matched
large-file regression before proposing shared-default promotion, then reattribute
remaining approximately1.0s pre-summary fixed work at4KiB x10K. Do not expand
queues or immediately assume protocol member batching pays for that interval.
Fixed member microbatch and adaptive remain unimplemented/unaccepted.

Large-file throughput remains the historical restricted approximately27.3Gbps;
no new large-file sample in this round,30/50/70-85/100G gates unmet. RDMA stays
paused, fresh-download auto still selects2, LF4/LF5 current candidates stay closed,
and large DATA/CRC or broader zero-copy paths retain their separate budget gates.
