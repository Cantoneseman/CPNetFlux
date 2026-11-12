# V3 Bounded Mapped Populate 10

Date: 2026-11-08. Input: 87438fdb700265b908d378aa1b89229a8eebbed5.
Single writer: R770-1 /home/Su/projects/CPNetFlux, existing branch.

## Scope and Hypothesis

Scope09 found about 2.11 million receiver minor faults per 8 GiB, mapped writer
wall sums around 4.72 seconds and no net benefit from serial whole-file prefill.
Test one candidate: MADV_POPULATE_WRITE immediately after each <=64 MiB mmap,
before copying into that window, inside the measured write interval. It may reduce
per-page user fault entry overhead while retaining the same inode/windows/workers.
Check actual support and all costs; do not infer benefit from reduced fault counts.

Initial implementation is diagnostic only. Add explicit --populate=lazy|write,
default lazy, only valid with mmap; errors fail without fallback. Retain schema3
with additive population and lifecycle observations. Measure map, populate and
unmap intervals with CPU; residual write cost includes copy/loop/synchronization
and is not pure memcpy. All preparation remains inside the write wall/CPU interval.
Partial population failure must fail the child and leave parent cleanup possible.

## Non-goals and Files

No runtime/default/backend changes, V3 integration until a separate scoped gate,
RDMA, additional workers/queues, THP/kernel/network policy, prefill outside timers,
64 GiB payload, subagents, worktrees, production restarts or weakened CRC/durability.
Exact initial files: this task; tools/perf/offset_write_bench.cpp;
tools/perf/run_offset_write_memory.py;
tools/test/run_cpnetflux_offset_write_backend_smoke.py;
tools/test/offset_write_fault_shim.cpp. Final result and docs/ROADMAP.md.
Evidence/scripts outside Git at
/home/Su/experiments/CPNetFlux/2026-11-08-v3-mapped-populate-10.
Preserve unrelated October14 untracked task files and CPSS(DCC).

## Acceptance and Sequence

1. Add meaningful configuration/content/window/failure tests and observe failure
   on the old executable, then implement. Verify mmap failures, actual lazy SIGBUS,
   populated EOF failure, partial advice failure and no accepted partial metrics.
2. Build and run related CTest on R1, commit/push/read back verified CPNetFlux
   origin, sync fixed commit to R2, same configuration/build/tests/version, eight
   existing binary hashes plus changed offset benchmark and fault shim hashes.
3. Local shared-inode sparse files only, 8 GiB, four workers, 65532-byte pieces,
   1 MiB chunks, 64 MiB windows, five alternating lazy/write pairs per host, hosts
   serial. Reuse exact content/bytes/window/cleanup/memory/swap guards. Require
   median >=10% local gain, both CV<=5%, no resource/correctness regression.
   No network task throughput or production claims from this experiment.
4. Record observer boundaries and any preparation costs, residual CPU, minor/major
   faults; keep failed samples. MemAvailable >=12 GiB, owned swap=0, host pswpout
   delta0, one <=8 GiB diagnostic file at a time; remove owned payload each sample.
5. If the gate passes, preserve the candidate only as an explicit diagnostic option
   and hand off a separately scoped V3 integration and five complete-task A/B pairs.
   If it fails, close this candidate and next isolate sender read/socket CPU rather
   than add a second page-population variation in this scope.
6. Final result/roadmap, scripts/evidence hashes, cleanup/production PID audit,
   explicit commit/push/remote SHA readback and R2 sync. No payload/credential Git.

## Initial Inspection

Both HEADs match input; R1 has only the two unrelated untracked files, R2 clean.
Shell/SSH and chat-dispatch APIs exposed; local Codex CLI located, continuation
not exercised. No dispatch, CLI turn or desktop control invoked. Linux7.0 header
defines MADV_POPULATE_WRITE=23; runtime support still needs actual execution.
Consulted Linux madvise(2): population failure returns an error, may populate only
part of the range, and may allocate file backing. No promise that this avoids OOM
or later SIGBUS after external truncation. Reference:
https://man7.org/linux/man-pages/man2/madvise.2.html

## Measurement Correction

Initial source 806a788d58058cad7e0432494dac0ed4db816932 passes both 4/4 CTest
and twenty content/resource/cleanup samples. Both hosts show a 23..25% regression.
However, getrusage's per-thread CPU accounting assigns tick-sized CPU to very
short mapping intervals (CPU can exceed their measured wall). Before accepting
phase CPU attribution, switch only observePhase CPU to CLOCK_THREAD_CPUTIME_ID.
Keep initial receipts/logs, fix/commit/push/sync/build/test/hash again and repeat
the twenty local samples in a new evidence subdirectory. This is a measurement
correction, not another candidate. Overall getrusage write-interval counters and
minor/major-fault observations retain their original definition.

## Completion

Corrected source e61a5868abd2b89f44263753ebe1fb04e8bf8c5f builds and related
CTest4/4 pass on both hosts; ten binary/shim hashes/config/version match. Both
sources committed/pushed and remote SHA-read back. Forty total diagnostic samples
pass content/resource/cleanup. Primary corrected five-pair medians lazy/write:
R1 55.078277/41.554848 Gbps, R2 54.876631/41.040652 Gbps; all CV<1%, all pairs
regress. Candidate closed no-go, no V3 runtime integration or new task throughput.
Owned payloads removed, both /dev/shm empty, production PIDs preserved. Full
evidence/commands/limitations/next sender-attribution scope:
[result](2026-11-08-v3-mapped-populate-10-result.md).
