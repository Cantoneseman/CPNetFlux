# R770 Receiver CPU Follow-up 01 Result

## Decision

The 1 GiB receiver profile completed the V3 transfer and integrity checks, but it is rejected as a performance sample because the network gate failed. No receiver optimization was measured, no candidate was admitted to A/B, and no annual comparison was run. Keep the production-default buffered inline mmap path.

## Fixed build and workload

- Run commit: `2ba8917b13266c4eaad37ba227e403db560fa973`.
- R1 and R2 were built at the same commit with matching CMake configuration and runtime binary SHA-256.
- Server: `3c3e7ac962cc272cfea6087a2ce9153ed36aac8299e434ecc316e1fa3632b777`.
- Download client: `c5660979b9ba167276754ae0dea44b6ad817d5f51f9b08444199aeb22d1f9bce`.
- Upload client: `383b501a75ec2395cc8a1c73a90d69533e67be9d09e1d19455afb1ea027f3d83`.
- Relevant CTest gates passed: `cpnetflux_large_data_ab_test`, `cpnetflux_sender_pacing_driver_test`, and `cpnetflux_receive_network_test`, 3/3.
- Workload: V3 TCP, four lanes, buffered inline mmap, direct receive off, 256 KiB DATA, requested 1 MiB SO_RCVBUF, 1.5 GB/s per-lane application pacing, CRC32C, 1 MiB chunks, checkpoint interval 16, tmpfs, compression off, explicit R1 `192.168.100.1` to R2 `192.168.100.2`.

## Transfer and network result

Run ID: `2026-11-25-r770-receiver-cpu-profile-01`.

The client completed 1,073,741,824 bytes with one completed file, zero failed/changed files, V3 TCP and four connections. The downloaded tree SHA-256 matched the source SHA-256 `c49634a997d8876f145f287eaa72f129c427da013498c8bacfa5e3164e8d2979`. The saved committed manifest was independently checked after the runner rejected the sample and contained 1,024 verified 1 MiB chunks. The client process exited with code zero.

The network gate rejected this completed transfer:

- R1 `rx_missed_errors`: +285.
- R1 `rx_out_of_buffer`: +285.
- R1 TCP retransmissions: +0.
- R2 `rx_missed_errors` and `rx_out_of_buffer`: +0.
- R2 TCP retransmissions: +285.
- No other interface error/drop counter delta was reported.

The runner saved `failure.json` with `receiver profile network acceptance gate` before appending an acceptance row. The per-client receipt and compressed profile summary preserve the completed transfer and its measurements. Diagnostic task-ready rate was 33.9515 Gbps over 0.253006 seconds; the protocol summary reported 34.5391 Gbps over 0.248701 seconds. Both values are rejected with the sample and are not performance claims.

## Receiver profile

The receiver profile is usable as diagnostic CPU accounting for this completed transfer. It is instrumented wrapper CPU, excludes begin/finalize/background work, and includes the mapped physical write inside nested write/range scopes. Nested values must not be added together.

- Receiver process sampled CPU lower bound: 0.89 seconds.
- Receiver RSS peak: 250,593,280 bytes. Minimum sampled MemAvailable: 28,727,894,016 bytes. Owned swap peak and host `pswpout` delta were zero.
- DATA frames: 4,096. Logical write and physical write coverage: 1 GiB.
- Mapped write wrapper CPU: 0.659171 seconds. Nested physical mmap write CPU: 0.643325 seconds.
- Mapped copy CPU: 0.625719 seconds across 4,096 calls and 1 GiB.
- Mapped copy minor faults: 262,144. Major faults: zero.
- Socket read CPU: 0.183901 seconds across 4,104 calls.
- CRC32C CPU: 0.018615 seconds across 4,096 calls and 1 GiB.
- State CPU: 0.001774 seconds. Lease start/finish CPU: 0.004922 seconds.
- Entry-lock wait/hold: 0.036022/0.021173 seconds. Checkpoint time: 0.008175 seconds.
- Mapping management CPU: map 0.000114 seconds, unmap 0.022160 seconds.

The mapped copy is the largest attributable software wrapper cost, and its fault count matches first writes to 4 KiB pages. The counters do not separate CPU spent in the memcpy implementation from page allocation/fault handling. They do not identify a specific kernel function or prove that changing the copy primitive will improve task throughput.

## Code review and candidate decision

The current receiver write path is `RangeFileCoordinator::write` to `PersistentFileTransaction::writeRange` to `MappedFileWindow::write`, where each accepted DATA payload is copied into the shared mapping with `std::memcpy`. The range lease and prior frame validation protect when bytes may be written and certified.

No low-risk mapped-write candidate is justified by the available evidence:

- Mapping/unmapping CPU is too small to support a material gain from changing the 64 MiB mapping window.
- Direct-to-mmap already regressed by about 36.7% in a 1 GiB five-pair comparison.
- Private tmpfs `huge=within_size` was rejected in two 1 GiB matrices by receive-queue errors/retransmissions. The local mapped-collapse diagnostic reduced faults but did not improve worker copy CPU/wall cost.
- Historical `MADV_POPULATE_WRITE` and larger write batches have negative evidence.
- The remaining large copy/fault cost has no demonstrated replacement with lower CPU or preserved end-to-end behavior. A streaming-store or other copy primitive is only an untested hypothesis and is not admitted to inter-node A/B.

The default path itself triggered R1 RX missed/OOB, and the correlated R2 retransmission increase prevents a clean baseline. Stop transfer experiments until a separate network acceptance window exists. Do not run the annual comparison from this sample.

## Cleanup and retained evidence

R1 local output slot and payload root were removed. R2 source and payload root were removed, the isolated server PID `1255702` is absent from `/proc`, and its server completion receipt exists. Production listeners on R1 PID `2477` and R2 PID `2485`, both port 2121, were preserved. The raw profile and failure evidence remain under:

`/home/Su/experiments/CPNetFlux/2026-11-25-r770-receiver-cpu-profile-01`

The historical process-management anomaly is recorded separately here. During preflight, PID `1285694` was a `cpnetflux_unit_tests` process in `futex_do_wait`, PPID 1, with CPU time 0 and RSS 13,076 KiB. It had no interconnect established socket and did not occupy the reserved experiment ports. I sent it TERM before the user's correction, and it exited. Its session cgroup was subsequently absent, so historical cgroup memory/CPU events cannot be checked. This does not prove that the process caused resource pressure.

## Final status

- Transfer integrity: passed.
- Network acceptance: rejected, R1 RX missed/OOB +285 and R2 retransmissions +285.
- Receiver profile: retained as diagnostic evidence only.
- Performance candidate: none admitted.
- 1 GiB candidate A/B and annual comparison: not run.
- Current production-default buffered inline mmap: retained.
