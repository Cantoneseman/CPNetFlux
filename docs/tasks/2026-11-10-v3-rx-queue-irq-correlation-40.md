# Receive Queue and IRQ Correlation 40

Date: 2026-11-10
Input commit: `fce5859a60a3f8683e0a29847bca772169d99c08`
Status: planned, not yet run.

## Scope

Run exactly one bounded invocation of `tools/perf/run_receive_network.py --samples 1` from R770-1, using the current fixed TCP path: R770-2 source to R770-1 destination over `eno17095np0`, 4 lanes, 256 KiB DATA frames, requested 1 MiB TCP receive buffer, mmap experiment receiver, CRC32C and verified chunks. The payload is 1 GiB in tmpfs. This is a diagnostic run with the existing observer, not a throughput acceptance sample.

Record interface `rx_missed_errors`, mlx5 `rx_out_of_buffer`, per-queue RX packet deltas, sampled `mlx5_comp<N>` interrupt counters and CPU placement, TCP/softnet counters, explicit route/bind receipts, source residency, memory and swap guards, integrity receipts, and exact cleanup evidence. Compare the queue and interrupt activity over the same task window, while treating interface and host counters as scoped observations rather than flow-level causality. Do not claim a one-to-one queue/IRQ mapping unless the captured inventory proves it.

## Non-goals

- Do not rerun the rejected 1 MiB DATA-frame candidate or change frame size.
- Do not run the 8 GiB M1 task, an A/B matrix, or a second diagnostic sample.
- Do not change software behavior, NIC settings, IRQ affinity, RSS, RPS, ring/coalescing, sysctl, THP, swap, cgroups, services, or routes.
- Do not treat the instrumented task rate as a performance result or as evidence of native verbs.

## Preconditions and stop conditions

- R770-1 and R770-2 must both be fixed at the recorded commit, have matching configuration and runtime binary SHA-256, and pass the relevant Python and CTest gates.
- Both nodes must have at least 12 GiB `MemAvailable`; the source and destination payload roots must be absent before the run. Preserve the established source-residency, zero owned-swap, zero host `pswpout` delta, process identity, and exact cleanup guards.
- Use only the task-local 1 GiB tmpfs source and destination. Stop on a failed guard, integrity check, counter regression, transfer failure, or cleanup failure. Do not retry within this task.

## Acceptance

The diagnostic is complete if the single task either passes integrity and cleanup with complete before/sample/after queue and IRQ evidence, or stops with the failure and cleanup evidence preserved. If the target counters do not increase, record the event as not reproduced and leave the historical cause open. If they increase, report only the observed interface/queue/IRQ/time-window association and its limits; this task cannot prove per-flow causality. No throughput gate is evaluated.

## Files and evidence

Pre-run change: this plan only. After the run, add a result record and update `docs/ROADMAP.md`, `docs/README.md`, and project `README.md` with the finding and next step; do not alter unrelated files. Raw evidence and task payload use `/home/Su/experiments/CPNetFlux/2026-11-10-v3-rx-queue-irq-correlation-40` and `/dev/shm/cpnetflux-s0-rx-queue-irq-correlation-40`; the runner must remove the marked payload root and target/source payloads. Commit and push each completed stage explicitly, then verify the remote SHA.
