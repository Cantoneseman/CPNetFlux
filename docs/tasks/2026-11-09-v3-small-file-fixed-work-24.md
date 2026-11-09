# S2 Small-File Fixed Work24

Date:2026-11-09. Status: implementation in progress, no profile results yet.
Input: fa937c024206527ad40e9bab2022bfe39a4b6790, pushed Gate23 result.
S1B default promotion is held by the preparation resource gate, not superseded.

## Scope And Acceptance

R770-1 sole writer; R2 fixed-commit peer. Attribute remaining work of explicit
CPNETFLUX_SUMMARY_PATH=cached on4KiB x10K (about39MiB per tree) before designing
member aggregation. S1 measured pre-summary about1.0s; overlapping file interval
sums are not CPU or critical-path budgets. No new protocol/queue/default changes.
Do not retry the8GiB preparation or reinterpret Gate23 as passed in this task.

Inspect current manifest writer, local executor/open/rename/checkpoint code and
sender ACK waits. Specify exact files before adding a bounded collector/driver
and tests. Existing process_probe_17.py has hard-coded roots and must not be
imported/monkeypatched into this scope. sudo-n perf capability was checked on R2;
check both peers and an owned PID/start/FIFO handshake before workload sampling.
Do not sample production, use system-wide perf or change perf_event_paranoid.

Exact files: new tools/perf/owned_process_profile.py (owned FIFO gate, perf and
bounded thread snapshots), tools/perf/run_small_file_fixed_work.py (two-run
orchestration/analysis), tools/test/run_owned_process_profile_test.py,
tools/test/run_small_file_fixed_work_test.py, CMakeLists.txt and
tools/perf/run_small_file_baseline.py (explicit optional runner callback only).
Task/result and docs/ROADMAP.md, docs/README.md carry status/handoff. No C++
runtime, protocol/default or system setting changes. R2 only syncs commits.
Acceptance: driver unit tests, collector self-test with two workers created
after ready/enable, matched builds, then one reference and one profiled transfer.
Both arms use gated exec and phase-timing on; release-to-independent-exit defines
this pair's T_ready, not the historical S1 spawn-to-exit denominator. Setup and
profiling shutdown/hash checks are outside that clock. Perf199Hz cpu-clock,
DWARF4096 user stacks, monotonic timestamps and<=32MiB/process. Sampled CPU stack
weights and blocked-thread snapshot fractions are not removable wall-time budgets.
No exact per-phase monotonic markers exist in the runtime; elapsed_seconds is
captured before summary hashing. Use it and hash_seconds as intervals only, not
an exact perf segmentation boundary. Preserve raw sample output for re-analysis.

One unprofiled cached reference and at most one paired profiled run, same pushed
commit/config/binaries, four TCP connections/pending2/queued pwrite, phase summary
on only if needed for timestamps, detailed range/payload timers off. Profile and
reference must share the same phase-summary setting. Account summary tail apart
from transfer work; stop collectors before independent SHA/source checks.
These are attribution samples, not a gain/CV acceptance matrix. Store all runs;
report observation overhead and sampled CPU proportions with coverage/loss limits.

Use only owned PIDs and bounded CPU sampling (data<=32MiB per process), timeouts,
MemAvailable>=12GiB, owned VmSwap0 and host pswpout delta0. Prefer acknowledged
perf enable before exec/new worker creation; verify inherited threads/symbols and
exact profiler shutdown. Independent SHA for all members, CRC/tree/checkpoints,
fresh target and exact marker-root cleanup remain required.

Output: reproducible code/commands/receipts, manifest/metadata/publish and ACK CPU
or wait evidence, and a conservative *wall-time* opportunity budget. If no>=20%
T_ready opportunity is supported, do not begin member microbatch implementation.
If supported, design one fixed-member mechanism with identity, per-member CRC,
failure isolation/resume/atomic publish and bounded memory. Adaptive follows
independent fixed-batch acceptance; neither replaces the open large-file gate.
