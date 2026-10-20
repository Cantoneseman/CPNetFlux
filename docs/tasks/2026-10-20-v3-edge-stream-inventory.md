# V3-EDGE-STREAM-01 implementation inventory and next-stage handoff

Date: 2026-10-20
Status: bounded payload path implemented; dual-host acceptance, pressure sampler, and I/O mechanism decision complete
Owner: current commander, single writer directly in /root/projects/CPNetFlux
Route: R2026-10-16-V3-UNIFIED-MULTILINK
Inputs: Slice E accepted 63e5fcc; V3-LOGICAL-10LINK-01 accepted implementation bb983f5e91711d55b312a841e27b2ab721cd291d; both hosts 406/406 and real cross-cloud matrix 10/10

## Practical issue

Ten logical sessions and correct recovery do not prove the host can overlap disk reads, checksums and network sends efficiently, or explain whether CPU/disk/network pressure is limiting it. This is the next software milestone; it is not physical NIC validation.

## Current evidence from source inspection

| Route requirement | Current implementation | Remaining evidence/work |
| --- | --- | --- |
| Bounded queue and lane backpressure | DynamicFileQueue has a 1..256-entry ring and per-worker credits; persistent sender bounds pending results by the negotiated window. Ten-lane matrix and queue regressions cover logical work ownership and bounded admission. | Queue occupancy/producer wait/consumer wait and byte bounds are emitted per transfer; the fixed fault/recovery suites and cross-cloud matrix cover cancellation and resume. A dedicated artificial slow-consumer benchmark remains outside the candidate gate. |
| Bounded payload memory | `BoundedPayloadPipeline` limits each file to two prepared chunks and a fixed byte budget; frame payloads remain capped at 65536 bytes. High-watermark and wait counters are emitted in V3 summaries. | Shenzhen and Shanghai fixed builds report the same bounded pipeline implementation; cross-cloud plain upload/download matrix passed 10/10. Artificial throttling remains a future performance experiment, not a correctness gate. |
| Read/checksum/send pipeline | A V3 producer thread performs read/checksum preparation while the sender loop compresses, protects and writes frames. Cancellation joins the producer before result handling; existing lease, resume and TLS paths remain in use. | Cross-cloud 1/2/4/8/10-lane upload/download matrix passed 10/10 with tree hashes, checkpoints, and ranges matching; existing fault/recovery CTest coverage passed on both hosts. |
| sendfile/splice evaluation | No sendfile/splice implementation was found in tracked src/include by git grep. Current framed checksum/compression/TLS path copies payload through userspace buffers. | Decision recorded below: retain buffered V3 I/O. Framed checksum/compression/TLS writes require userspace transformation and framing; no zero-copy claim is made. |
| NUMA/CPU/disk pressure observation | tools/perf/collect_env.sh records numactl hardware if available. Scheduler metrics have pressure fields, but these are not a per-run /proc CPU/disk/PSI sampler for this V3 payload path. | tools/perf/collect_pressure.py now writes run-scoped JSONL samples for CPU, diskstats, and PSI, with explicit unavailable records. It was syntax-checked and produced three valid samples on Shenzhen; it is optional and does not require special hardware. Physical NIC/NUMA pinning experiments stay deferred. |


## Implemented in this development slice

The sender now uses one V3-only `BoundedPayloadPipeline` per persistent file. A producer thread performs bounded `readAtAll` and checksum preparation; the sending loop performs compression, frame protection and socket writes. The queue is limited to two chunks and a byte budget of `two times the actual per-frame read limit (`min(configured buffer size, 65532 bytes)`)`, so a slow network cannot make file-read memory grow without limit. Transport or producer errors cancel the queue and wake both sides before the file result path continues.

The run statistics now expose `payload_pipeline_budget_bytes`, `payload_pipeline_high_watermark_bytes`, `payload_pipeline_producer_waits` and `payload_pipeline_consumer_waits` in V3 summaries. Existing compression, checksum, duplicate/corrupt-chunk, resume-range and work-lease behavior remains in the V3 send path.

Red/green evidence so far:

- `/tmp/cpnetflux-runs/V3-EDGE-STREAM-01-red-build.log`: expected link failure before the implementation source was added.
- `/tmp/cpnetflux-runs/V3-EDGE-STREAM-01-build.log`: Shenzhen build after implementation completed.
- `cpnetflux_unit_tests --gtest_filter='BoundedPayloadPipelineTest.*:PersistentTreeRangeTest.LoopbackTransfersTwoRangesAndPublishesAfterFinalRange'`: 4/4 passed.
- Full unit suite after the final metrics edit: 357/357 passed (`/tmp/cpnetflux-runs/V3-EDGE-STREAM-01-unit-final.log`).
- Shenzhen CTest: 409/409 passed when the host token was supplied from `/etc/cpnetflux/token` (`/tmp/cpnetflux-runs/V3-EDGE-STREAM-01-ctest-final-token.log`). The first environment-only run reported 2 token-auth failures because `CPNETFLUX_TEST_TOKEN` was unset; the two tests passed immediately when rerun with the existing host token.

This slice does not yet close the milestone. Remaining work in `V3-EDGE-STREAM-01` is a bounded slow-consumer/interrupt observation on both cloud hosts, optional run-scoped CPU/disk/PSI sampling, and the written sendfile/splice suitability decision.

## Remaining acceptance scope

1. Keep artificial slow-consumer benchmarking as optional performance research; do not use it as a correctness or readiness claim.
2. Carry the accepted source/build evidence into V3-CANDIDATE-CLOSEOUT-01: freeze the manifest, rerun the fixed acceptance gate, and prepare the release handoff.

Acceptance evidence for this slice: commit 164b98b is pushed to origin/codex/V3-LOGICAL-10LINK-01; final source archive SHA256 is c8e660e38d62a73dbc0d97c5c374269d03c36b7b9691bcb77bfb39ea685f9416 on both hosts; the C++ parent fa77fc8 passed 409/409 CTest and 357/357 unit tests on each host, and the 87550a2 source rebuilt successfully on both; the cross-cloud 10-case matrix passed 10/10; critical binary SHA256 values matched (cpnetflux-server 366f5fc8..., tree upload e0627efc..., tree download 2f761157...).

## Non-goals and remaining route

No physical NIC/local bind/different physical path validation, formal service replacement, RDMA/SmartNIC, 100G/1Tbps readiness, or V1/V2 executor. After this stream-path acceptance, one software block remains: V3-CANDIDATE-CLOSEOUT-01 packaging, fixed acceptance, and documentation freeze. Artificial slow-consumer performance exploration is optional and does not reopen the architecture.


## Final acceptance update (2026-10-21)

The cross-cloud rerun used the committed fa77fc8 source on both hosts. It exercised plain upload and download at 1, 2, 4, 8, and 10 logical lanes. All 10 cases passed admission, coverage, tree-hash, checkpoint, and range checks. Existing fault/recovery CTest cases also passed on each host.

The optional pressure collector is tools/perf/collect_pressure.py. It samples /proc/stat, /proc/diskstats, and /proc/pressure/{cpu,io,memory} into JSONL records tied to a run id. Missing files are represented as available: false with a reason. Shenzhen and Shanghai each produced three validated samples at /tmp/cpnetflux-runs/V3-EDGE-STREAM-01/{shenzhen,shanghai}-pressure.jsonl after source synchronization.

I/O mechanism decision: retain buffered userspace I/O for the V3 framed path. CPNetFlux must read, checksum, optionally compress, protect, and frame each payload before writing it. sendfile or splice cannot replace that path while preserving these transformations and TLS behavior. No zero-copy or throughput claim is made; a future protocol-specific optimization would need its own correctness gate.
