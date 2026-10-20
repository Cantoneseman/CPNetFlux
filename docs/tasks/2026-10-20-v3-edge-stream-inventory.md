# V3-EDGE-STREAM-01 implementation inventory and next-stage handoff

Date: 2026-10-20
Status: bounded payload path implemented; dual-host acceptance and pressure/mechanism decision pending
Owner: current commander, single writer directly in /root/projects/CPNetFlux
Route: R2026-10-16-V3-UNIFIED-MULTILINK
Inputs: Slice E accepted 63e5fcc; V3-LOGICAL-10LINK-01 accepted implementation bb983f5e91711d55b312a841e27b2ab721cd291d; both hosts 406/406 and real cross-cloud matrix 10/10

## Practical issue

Ten logical sessions and correct recovery do not prove the host can overlap disk reads, checksums and network sends efficiently, or explain whether CPU/disk/network pressure is limiting it. This is the next software milestone; it is not physical NIC validation.

## Current evidence from source inspection

| Route requirement | Current implementation | Remaining evidence/work |
| --- | --- | --- |
| Bounded queue and lane backpressure | DynamicFileQueue has a 1..256-entry ring and per-worker credits; persistent sender bounds pending results by the negotiated window. Ten-lane matrix and queue regressions cover logical work ownership and bounded admission. | Distinguish metadata backpressure from payload pipeline depth. Record queue occupancy/producer wait/consumer wait and memory bounds under a deliberately slow disk/network consumer. |
| Bounded payload memory | `BoundedPayloadPipeline` limits each file to two prepared chunks and a fixed byte budget; frame payloads remain capped at 65536 bytes. High-watermark and wait counters are emitted in V3 summaries. | Validate observed high-watermarks and interruption behavior on both cloud hosts, including TLS/checksum and slow-consumer cases. |
| Read/checksum/send pipeline | A V3 producer thread performs read/checksum preparation while the sender loop compresses, protects and writes frames. Cancellation joins the producer before result handling; existing lease, resume and TLS paths remain in use. | Run cross-cloud slow-consumer and interruption checks and confirm no duplicate or missing ranges. |
| sendfile/splice evaluation | No sendfile/splice implementation was found in tracked src/include by git grep. Current framed checksum/compression/TLS path copies payload through userspace buffers. | Evaluate feasibility with actual framing/checksum/TLS requirements and record the decision. A documented decision to retain buffered I/O for unsupported combinations is acceptable; this concerns I/O mechanisms within V3, never an old protocol/executor. Do not claim zero copy from function names or loopback speed. |
| NUMA/CPU/disk pressure observation | tools/perf/collect_env.sh records numactl hardware if available. Scheduler metrics have pressure fields, but these are not a per-run /proc CPU/disk/PSI sampler for this V3 payload path. | Add bounded, optional host observations and units/sample times; keep unavailable counters explicit. Tie observations to a run and validate parsing without requiring particular hardware. Physical NIC/NUMA pinning experiments stay deferred. |


## Implemented in this development slice

The sender now uses one V3-only `BoundedPayloadPipeline` per persistent file. A producer thread performs bounded `readAtAll` and checksum preparation; the sending loop performs compression, frame protection and socket writes. The queue is limited to two chunks and a byte budget of `2 * max(kPersistentDataSessionPayload, configured buffer size)`, so a slow network cannot make file-read memory grow without limit. Transport or producer errors cancel the queue and wake both sides before the file result path continues.

The run statistics now expose `payload_pipeline_budget_bytes`, `payload_pipeline_high_watermark_bytes`, `payload_pipeline_producer_waits` and `payload_pipeline_consumer_waits` in V3 summaries. Existing compression, checksum, duplicate/corrupt-chunk, resume-range and work-lease behavior remains in the V3 send path.

Red/green evidence so far:

- `/tmp/cpnetflux-runs/V3-EDGE-STREAM-01-red-build.log`: expected link failure before the implementation source was added.
- `/tmp/cpnetflux-runs/V3-EDGE-STREAM-01-build.log`: Shenzhen build after implementation completed.
- `cpnetflux_unit_tests --gtest_filter='BoundedPayloadPipelineTest.*:PersistentTreeRangeTest.LoopbackTransfersTwoRangesAndPublishesAfterFinalRange'`: 4/4 passed.
- Full unit suite after the final metrics edit: 357/357 passed (`/tmp/cpnetflux-runs/V3-EDGE-STREAM-01-unit-final.log`).
- Shenzhen CTest: 409/409 passed when the host token was supplied from `/etc/cpnetflux/token` (`/tmp/cpnetflux-runs/V3-EDGE-STREAM-01-ctest-final-token.log`). The first environment-only run reported 2 token-auth failures because `CPNETFLUX_TEST_TOKEN` was unset; the two tests passed immediately when rerun with the existing host token.

This slice does not yet close the milestone. Remaining work in `V3-EDGE-STREAM-01` is a bounded slow-consumer/interrupt observation on both cloud hosts, optional run-scoped CPU/disk/PSI sampling, and the written sendfile/splice suitability decision.

## Remaining acceptance scope

1. Add or reuse a deterministic slow-consumer/transport-interruption runner and capture queue high-watermark, producer-wait and consumer-wait evidence. Include checksum, compression and TLS combinations that are already supported by the fixed matrix.
2. Add optional run-scoped CPU/disk/PSI observation where the host exposes it, with explicit unavailable fields when it does not.
3. Record the sendfile/splice decision: framed checksum/compression/TLS writes require userspace framing, so retain buffered V3 I/O unless a future protocol-specific path proves equivalent correctness.
4. Commit on Shenzhen, build the fixed source, transfer only that committed source to Shanghai, verify source and critical binary hashes, then run functional/stall/recovery tests on both. Performance evidence is bounded experimental observation, not a throughput promise.

## Non-goals and remaining route

No physical NIC/local bind/different physical path validation, formal service replacement, RDMA/SmartNIC, 100G/1Tbps readiness, or V1/V2 executor. After ten-lane acceptance, two software blocks remain: this stream-path milestone, then V3-CANDIDATE-CLOSEOUT-01 packaging/fixed acceptance/docs freeze. Re-estimate the 2-3 working-day stream budget after its concrete buffer/cancellation design; candidate closeout is estimated at another 1-2 working days if no major defects are found.
