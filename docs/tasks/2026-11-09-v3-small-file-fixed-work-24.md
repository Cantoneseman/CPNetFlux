# S2 Small-File Fixed Work24

Date:2026-11-09. Status: next execution plan, not profiled or implemented yet.
Input: pushed Gate23 result commit; record exact SHA at execution start.
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
