# Milestone38: queued mmap receiver candidate result

Date: 2026-11-10. Input `2cfecdae584b40b934faed851661fbedeffb4c49`.
Candidate `02f6a8b7c2fda4d7e57997d25804259aa6d8b6eb`; rejected and reverted by
`c48a3b0b99de2725890288faeb98e3e15e42803b`. Final route/docs commit follows.
R770-1 was the only writer; R770-2 built and tested fixed commits.

## Decision

The bounded queued handoff moved CRC-verified 256 KiB frames from each socket
thread to the existing mmap writer queue at depth 2. The queue was active and
bounded, but the complete task became slower in every pair and receiver CPU
increased. This is **NO-GO**; the runtime allowance and test/driver changes were
reverted. Inline mmap remains the only tested path for this experimental backend.

## Paired 1 GiB task result

Both arms used candidate commit `02f6a8b`, RelWithDebInfo, RDMA/TLS/io_uring/ISA-L
enabled, R770-2 to R770-1 with explicit `192.168.100.2` to `.1` bind on
`eno17095np0`, TCP data transport, resident tmpfs source and fresh mmap tmpfs
target. Each task used four lanes, eight 128 MiB ranges, 256 KiB logical DATA,
1 MiB recovery chunks, CRC32C/verified_chunks, checkpoint interval 16,
TLS/compression off, and non-durable commit. The ten tasks were interleaved
AB/BA; detailed range timing stayed off and all samples were retained.

| Metric | Inline control | Queued candidate |
|---|---:|---:|
| T_ready seconds | .240224, .241880, .236352, .241785, .233179 | .279192, .276096, .253641, .278198, .281112 |
| Median task rate | 35.758 Gbps | 30.877 Gbps |
| T_ready CV | 1.43% | 3.70% |
| Paired time change | - | candidate slower 5/5, by 7.31% to 20.56% |
| Sampled receiver CPU lower-bound median | .85 s/GiB | 1.25 s/GiB |
| Peak RSS | 250,576,896 B | 249,798,656 B |
| DATA frames per GiB | 4,096 | 4,096 |

Median throughput fell **13.65%**. The queued pipeline reached its 524,296-byte
per-lane high-watermark at depth 2 (about 2.0 MiB across four lanes), confirming
that the handoff was exercised. The added consumer thread/queue did not increase
the work unit or overlap it enough to improve the full task; sampled receiver CPU
rose about 47%. RSS was effectively unchanged.

All ten transfers passed V3 completion, SHA/tree integrity, eight-range and
1,024 verified-chunk checks. The minimum sampled MemAvailable was about 27.47 GiB;
owned swap and host `pswpout` deltas were zero. No `rx_errors`, `rx_dropped` or
CRC-error increments occurred. One inline sample had an interface-wide
`rx_missed_errors` delta of 170; all queued intervals had zero. This counter is
host-interface scoped and does not establish per-flow causality. Both experiment
roots were removed; `/dev/shm` was empty on both hosts afterward and production
server identities remained unchanged.

## Verification and final state

Candidate commit `02f6a8b` built on both hosts; each ran 589 passing unit tests
and skipped one native-RDMA hardware test. Four related CTests passed on each:
mmap configuration, mmap fault/resume, receive-buffer smoke and receive-buffer
resume. After reverting, both hosts rebuilt final source commit `c48a3b0`; the
same unit and CTest gates passed again. The final runtime no longer accepts queued
receive for experimental mmap download.

The full sample receipts, summaries, route/interface counters, matching-build
receipts, resource gates and cleanup evidence are in
`/home/Su/experiments/CPNetFlux/2026-11-10-v3-receiver-pipeline-38`.
The directory contains measurement metadata only, no payload. A local evidence
copy is retained under `_codex_stage38/evidence`.

## Next boundary

Do not tune queue depth, wake policy or add more receiver threads. The next
software candidate is narrower: evaluate moving the existing per-range CRC32C
combine out of the shared `RangeFileCoordinator::Entry` critical section while
the range slot remains pinned. Before implementing, establish the slot/write
ownership and retry invariants with a failing test; keep CRC validation,
checkpointing, leases and publication order unchanged. The prior audit did not
quantify lock contention, so this is a bounded design/test task, not a claimed
bottleneck or guaranteed speedup. The 8 GiB M1 remains `BLOCKED_ENV`; do not retry
the same source-residency conditions or treat this 1 GiB result as M1/100 Gbps.
