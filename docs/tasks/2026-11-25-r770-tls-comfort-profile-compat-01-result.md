# R770 TLS Comfort-Profile Compatibility 01 Result

## Decision

TLS comfort-profile compatibility passed local and two-node functional checks. The single 512 MiB inter-node run is rejected as an acceptance sample because R1 reported a TCP retransmission delta of +1. It is not a throughput result and was not retried. This compatibility work is separate from receiver performance optimization.

## Fixed build

- Commit on R1 and R2: `6a308498a5434f8df28dd57e0a3b8b1be14f2490`.
- Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`.
- Server SHA-256: `3c3e7ac962cc272cfea6087a2ce9153ed36aac8299e434ecc316e1fa3632b777`.
- Download client SHA-256: `c5660979b9ba167276754ae0dea44b6ad817d5f51f9b08444199aeb22d1f9bce`.
- Upload client SHA-256: `8eab328866dd80e2a228b0469d348f1921965b312934080bc7e28467b3699713`.

## Compatibility checks

- R1 unit suite: 661 tests reported, 650 passed, 11 conditionally skipped, zero failed.
- R1 CTest: `cpnetflux_gridftp_data_tls_smoke`, `cpnetflux_v3_interrupted_resume_smoke`, and `cpnetflux_large_data_interrupted_resume_smoke` passed, 3/3.
- R2 parser unit test and the same three-mode TLS integration checks passed.
- The 512 MiB local TLS large-data smoke passed token-only, control TLS with plaintext data, and control plus data TLS. The full-TLS case exercised interrupted/resumed transfer and checkpoint handling.
- Tests confirmed V3 TCP, four lanes, 256 KiB DATA, 1 MiB requested SO_RCVBUF, 1.5 GB/s/lane application pacing, CRC32C, 1 MiB chunks, checkpoint interval 16, buffered inline mmap, and direct receive disabled. TLS does not enable the direct-to-mmap opt-in path.
- No protocol format, frame size, pacing rate, network hardware, or security level was changed.

## Inter-node run

Run ID: `2026-11-25-r770-tls-comfort-02`.

The single run used R1 `192.168.100.1` to R2 `192.168.100.2`, V3 TCP with four lanes, 512 MiB tmpfs source/target, control and data TLS required, buffered inline mmap, direct receive off, 256 KiB DATA, requested 1 MiB SO_RCVBUF (2 MiB effective readback), 1.5 GB/s/lane application pacing, CRC32C, 1 MiB chunks, checkpoint interval 16, and compression off.

The protocol completed 536,870,912 bytes across four lanes, verified TLS on both planes, committed 512 chunks, and reported zero direct-receive calls. The source SHA-256 was `ad2f2f84c6cc1fe087ca4c4ccf19012aa50491eefa1873fcace327c8d33c6d63`. The client task-time measurement was 0.194792957 seconds, or 22.0489 Gbps, but this value is rejected with the sample and must not be used as performance evidence.

The runner rejected the sample for R1 `Tcp.RetransSegs +1`; R2 had no reported network deltas. The rejection list contained no RX missed/OOB or NIC error delta. The runner failed before persisting R1 `result.json` and its raw before/after counter JSON, so a complete R1 counter-delta record is unavailable. This evidence gap is retained rather than inferred away.

## Cleanup and evidence

Both endpoint tmpfs task roots were checked absent after the run. The R1 `peer-cleanup.json` records R2 source/root removal. R2's temporary test server PID `1249173` is no longer present in `/proc`. The production listeners were not stopped. No retry was performed and no acceptance threshold was changed.

Evidence directories:

- R1: `/home/Su/experiments/CPNetFlux/2026-11-25-r770-tls-comfort-02`
- R2: `/home/Su/experiments/CPNetFlux/2026-11-25-r770-tls-comfort-02`

## Follow-up

TLS compatibility is available in local integration coverage, with one inter-node function run rejected by the network gate. Do not repeat that run as part of this task. Annual comparison remains a separate future stage. Continue the receiver CPU task from `2026-11-25-r770-receiver-cpu-followup-01.md`; its baseline phase-level CPU profile currently available is 512 MiB, so a 1 GiB profile is required before selecting a performance candidate.
