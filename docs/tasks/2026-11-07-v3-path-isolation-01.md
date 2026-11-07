# V3 TCP Path Isolation 01: Task Scope

Date: 2026-11-08
Status: diagnostic complete; candidates 1 and 2 are no-go. Three-stage/write-concurrency expansion is paused; no source changes were made by this scope.

## Objective

Find the cause of the repeatable observation that one V3 TCP lane can outperform four lanes, using longer payload windows and separating raw TCP transport capacity from V3 end-to-end work and tmpfs storage behavior. If evidence identifies a software hot path, update this task record with one falsifiable optimization hypothesis, affected files, and acceptance checks before changing code.

## Input and authority

- Authoritative source: R770-1 `/home/Su/projects/CPNetFlux`.
- Tested commit: `43975642da2f8c5dcf587f2c54e56854a961367e`, a scope-only descendant of code commit `94f5ab12976b0d9cabe23aa94f0270d641318f63` (core implementation `201a43a047102dfe52b1f05ec4497181dd64f6bb`).
- R770-2 is only the fixed-commit peer/build/test node.
- Data interface: `eno17095np0`, TCP endpoints `192.168.100.2` to `192.168.100.1`, with explicit bind and route/counter evidence.
- Evidence root: `/home/Su/experiments/CPNetFlux/2026-11-07-v3-path-isolation-01`.

## Scope

1. Raw TCP `iperf3`: one and four streams, 7 sequential repetitions each, 20-second measured windows, explicit source/destination bind. Keep sender/receiver JSON, CPU, route, and interface counter evidence.
2. V3 real file transfer over explicit `--data-transport tcp`, using one resident 8 GiB source on R770-2 and a single fresh tmpfs output on R770-1 per sample. Compare 1/2/4 connections, 7 repetitions per setting in balanced order. Preserve V3 protocol terminal evidence, CRC32C, independent SHA-256, process CPU/I/O, route/counters, and memory guard data.
3. Completed: standalone CPNetFlux storage benchmark on tmpfs with a bounded 1 GiB working file and repeated write/read/rewrite operations. Keep this as filesystem diagnostic evidence, not as a network or V3 throughput result.
4. Analyze complete task rate, per-task CV, aggregate window service rate, CPU/GiB, and memory separately. State whether the measured path accounts for the lane scaling regression.
5. Candidate 1 (write concurrency 1) and candidate 2 (write concurrency 2) are complete and no-go. Stop expanding the three-stage/write-concurrency architecture; no third candidate follows.

## Candidate 1 result (closed, no-go)

Seven fresh c4 crossover pairs compared read=0/write=0 with read=0/write=1 on the fixed build. Both groups were stable (population CV 1.849% and 2.469%), but the candidate median was 14.530 Gbps versus 15.502 Gbps baseline; paired median change was -6.50%, failing the +10% promotion gate. CPU fell from 32.91 to 23.71 client+server CPU seconds/GiB, while task throughput regressed. The candidate is no-go and is not promoted. One earlier pool-r4w1 sample is retained as a failed experiment-driver assertion and excluded; its V3 transfer completed, but independent SHA validation was not run.

## Candidate 2 result (closed, no-go)

The 21-sample 8 GiB baseline reproduced the lane regression with CV below 5%. Raw TCP four streams reached about 89.5 Gbps; V3 c4 had high receiver system CPU, and prior perf evidence located important CPU in same-inode tmpfs pwrite locking. Candidate 1 showed that write concurrency 1 greatly reduces CPU and aggregate range-write time but serializes too much of the receiver path. This scope authorizes exactly one final measurement of the already implemented shared ready-writer pool at write concurrency 2; it does not authorize a new executor, queue, or source-code change.

Hypothesis: keeping four network lanes and allowing two ready writers will preserve useful write parallelism while limiting same-inode pwrite lock contention, improving complete V3 task throughput over the unrestricted read=0/write=0 baseline.

Compare seven newly measured c4 baseline samples (read=0/write=0) with seven c4 candidate samples (read=0/write=2, executor pool) in alternating order on the same commit/build. Report every sample's task Gbps, median, mean, population CV, paired delta, and client+server CPU seconds/GiB. Every sample retains V3 terminal/CRC/SHA, route/counter, memory, swap, and cleanup checks.

Seven fresh c4 crossover pairs compared read=0/write=0 with read=0/write=2 on the fixed build. Both groups were stable (population CV 1.845% and 3.045%), but the candidate median was 14.997 Gbps versus 15.618 Gbps baseline; paired median change was -4.73%, failing the +10% promotion gate. CPU fell from 32.29 to 26.75 client+server CPU seconds/GiB, while task throughput regressed. Candidate 2 is no-go and is not promoted. The default read=0/write=0 configuration remains the verified implementation. No third candidate is authorized by this scope.

## Non-goals

- No RDMA verbs optimization, TLS/security change, kernel/sysctl/NUMA/MTU/network configuration change, package installation, or production service restart.
- No in-memory V3 sink implementation in this diagnostic pass; the current V3 path has no such endpoint. Do not label tmpfs as a protocol-only memory sink.
- No increase to worker/lane architecture. No third same-inode write-admission candidate; the only remaining authorized check is the existing shared-writer-pool configuration explicitly scoped above.
- No claim of 100 Gbps or production readiness.

## Safety and validity gates

- Before transfer: verify both checkouts at the input/fixed commit, tracked trees clean, same build configuration, and matching SHA-256 for the transfer binaries. Build on both sides if needed, then re-check hashes.
- Use only an isolated temporary V3 server, dedicated high ports verified free, and exact task-owned paths. Do not connect to or alter the production services.
- At all times, require at least 12 GiB `MemAvailable` on both nodes, no task-owned swap, no host `pswpout` increase, and no more than one 8 GiB payload per node. Abort a sample on guard failure.
- The source must be fully resident and SHA-256 verified before timing. Every V3 output must pass V3 terminal/CRC checks and an independent SHA-256 match before exact-path cleanup.
- Preserve initial failures and exclude them transparently; do not relabel them as passes. Retain logs, summaries, counters, manifests, cleanup receipts, and process identity records outside Git. Do not remove unrelated data or processes.
- Do not start the next layer if a prior process, payload, resource guard, or production PID is unresolved.

## Acceptance

- Seven raw TCP samples per stream count and seven V3 baseline samples per lane count; report median, mean, population CV, and every sample. Candidate 1 has been closed no-go. Candidate 1 and candidate 2 are closed no-go; the three-stage/write-concurrency performance gate remains open. Raw TCP measured windows are 20 seconds each. V3 file samples use 8 GiB; report per-task wall and aggregate time across each 7-sample group.
- Every V3 sample completes with `data_transport=tcp`, expected connection count, successful V3 terminal state, exact logical byte count, CRC32C, matching external SHA-256, no retries, and successful cleanup.
- All memory/resource gates pass. Evidence records the actual interface route and byte-counter deltas.
- State the narrow finding and select the next single step from evidence. The instability gate is CV <= 5%; failing it means the corresponding comparison is inconclusive and needs a measurement correction, not a performance claim.
- Commit and push the scope, results, and any roadmap update as explicit files on the existing branch; record commit SHA and reread the remote SHA. Synchronize R770-2 and verify same commit/config/binary hashes before any V3 data transfer.

## Files in scope

- `docs/tasks/2026-11-07-v3-path-isolation-01.md` (this scope, before experiment start)
- `docs/tasks/2026-11-07-v3-path-isolation-01-result.md` (results and handoff)
- `docs/ROADMAP.md` (update only after results)

No source or test file is in scope unless the scope is revised after the diagnostic evidence establishes a root cause.
