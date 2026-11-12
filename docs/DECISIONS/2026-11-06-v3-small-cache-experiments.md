# Small Cache Experiments for Software Optimization

2026-11-06 user steering: continue optimizing software, stop using 64 GiB files
for routine experiments, and clean experimental payloads after verification.

The current measured disk source path supplies about 4 Gbps. This establishes a
workload-specific service rate, not the hardware limit and not the software limit.
Prioritize 1 GiB tmpfs source/target native V3 tests to expose software overhead;
record cache residency, physical reads, memory, task phases, integrity, protocol
terminals and transport errors. Keep commit_sync=none explicitly labelled as
non-durable. Full verification is a separate correctness control.

The previous ROADMAP step that put io_uring disk queue tests and 64 GiB native
repeats first is superseded for current execution. Retain those as deferred
storage investigations. No previous result is rewritten, and no tmpfs result
replaces the 64 GiB disk workload or proves sustained storage-backed throughput.

Each task owns one small source and one output at a time. Preserve logs, hashes,
and receipts; delete task-owned payloads after integrity verification. Inspect
exact ownership/path before cleanup; historical payloads require separate review.

Scope and gates: ../tasks/2026-11-06-v3-cache-path-01.md.

2026-11-06 follow-up: subsequent routine software experiments use memory-backed
source and target, with explicit memory budgets. Start with one lane and 1 GiB,
one output at a time; include registered RDMA receive/send pools in resource
accounting. Check resident pages, process physical I/O, host swap activity and
RSS/minimum MemAvailable. Abort owned experiment processes on the task's sampled
memory threshold, preserve failed workload evidence, and clean only owned paths.
Do not enlarge payloads or concurrent lanes without revisiting the memory budget.
Historical disk service rates remain a separate workload; tmpfs is non-durable.
Current gates: ../tasks/2026-11-06-v3-receiver-profile-01.md.
