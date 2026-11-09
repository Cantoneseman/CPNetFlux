# S1B Summary Default Gate23

Date:2026-11-09. Status: follow-up plan only, not executed.
Input experiment/source20aef478eda71764eb1876e8196518c1c60e87e1;
start from pushed Summary Path22 result commit and record its SHA before edits.
S1 opt-in passed three filesystem-read cases and a reuse negative control;
shared default remains original. See [result](2026-11-09-v3-summary-path-22-result.md).

## Scope, Non-Goals And Files

Close the outstanding matched large-file/default-promotion gate. This is a
regression check of summary selection, not a new large-file throughput mechanism.
R770-1 sole writer, R2 fixed-commit build/source peer. No agents/worktrees,
network/THP/NUMA changes, RDMA, compression, queue tuning or production changes.
No pending-window correction or member aggregation in this gate.

First inspect the existing scope17 fixture and current CLI restrictions to set
exact reproducible commands; do not rely on old external helper assumptions.
Expected files: this task/result, a bounded regression driver or the existing
experiment helper, relevant selector/default tests and route docs. Only if the
gate passes, a minimal client selector fallback change may propose default cached.
Record precise additional files before implementation. No new protocol fields.

## Regression Before Promotion

Use the existing accepted8GiB resident-source/new-target four-TCP memory-file
scope: queued sender/inline experimental mmap receiver, CRC32C/verified_chunks,
checkpoint16, TLS off and commit_sync=none. This is the explicit8GiB exception
to the small-file1GiB matrix bound, not64GiB or disk-persistence testing.
At most one source/output per endpoint; MemAvailable>=12GiB, owned VmSwap0,
pswpout delta0. Warm/source SHA outside, fresh output each time, full T_ready
using the corrected independent exit timestamp. Clean owned data per sample.

At least five interleaved original/cached pairs on the same pushed commit and
matching config/version/binaries, explicit .2/.1 bind and route/counter evidence.
Both arms expected verified_chunks/effective not_used; validate that fact.
Gate: corrected full-task median regression<=5%, arm CV<=5%, complete independent
SHA/CRC/manifests/V3 terminals/resources and no recovery regression. Report
descriptive p95 and all samples; no sample deletion/repeated CV chasing.
Historical27.3Gbps is context only, never the unmatched denominator.

If passed, change only the CLI selector's unset fallback, update default behavior
tests/docs, then commit/push/sync/rebuild/hash-check and run a default-versus-
explicit-cached correctness smoke. Keep explicit original as the control.
Recheck failure/recovery and small-file summary behavior. If this gate fails,
retain the opt-in candidate and old default, record the reason and stop promotion.

## Subsequent Development

After closing the default decision, take one bounded CPU/fixed-work attribution
of the optimized4KiB x10K path (pre-summary still about1.0s). Keep receiver detail
timers off in baseline; their measured overhead was18%. Separate manifest writer
CPU/serialization, metadata/open/publish work and sender ACK waits. Existing sums
overlap and cannot by themselves justify member microbatch's>=20% wall budget.

Then design fixed-member batches only with a new mechanism/budget and per-member
identity/CRC, failure isolation, resume and atomic publish. Adaptive follows an
independently accepted fixed mechanism. Larger TCP zero-copy/RDMA ownership
designs and big DATA/CRC remain separate gated directions, RDMA still paused.
All future gains use matched same-contract full tasks;100G remains unachieved.
