# V3-EDGE-STREAM-01 implementation inventory and next-stage handoff

Date: 2026-10-20
Status: inventory complete; implementation/acceptance pending
Owner: current commander, single writer directly in /root/projects/CPNetFlux
Route: R2026-10-16-V3-UNIFIED-MULTILINK
Inputs: Slice E accepted 63e5fcc; V3-LOGICAL-10LINK-01 accepted implementation bb983f5e91711d55b312a841e27b2ab721cd291d; both hosts 406/406 and real cross-cloud matrix 10/10

## Practical issue

Ten logical sessions and correct recovery do not prove the host can overlap disk reads, checksums and network sends efficiently, or explain whether CPU/disk/network pressure is limiting it. This is the next software milestone; it is not physical NIC validation.

## Current evidence from source inspection

| Route requirement | Current implementation | Remaining evidence/work |
| --- | --- | --- |
| Bounded queue and lane backpressure | DynamicFileQueue has a 1..256-entry ring and per-worker credits; persistent sender bounds pending results by the negotiated window. Ten-lane matrix and queue regressions cover logical work ownership and bounded admission. | Distinguish metadata backpressure from payload pipeline depth. Record queue occupancy/producer wait/consumer wait and memory bounds under a deliberately slow disk/network consumer. |
| Bounded payload memory | persistent_tree_transfer.cpp sender uses one kMaxPayload buffer per lane; persistent_data_session.h caps a frame payload at 65536 bytes. Receiver processes frames within this bound. | This is chunked streaming, not evidence of a separate bounded payload ring connecting read/checksum/send stages. Audit peak buffers with TLS/compression and shared file state. |
| Read/checksum/send pipeline | Sender reads with storage::readAtAll, calculates checksums and synchronously writes a frame in the same loop (persistent_tree_transfer.cpp around 944..1013); resultReader separately processes acknowledgements. | ACK overlap exists; independent read/checksum/send overlap is not implemented in this loop. Specify and implement one bounded V3 payload pipeline preserving cancellation, lease identity, checkpoint semantics and TLS. Add deterministic stall/failure tests before enabling it. |
| sendfile/splice evaluation | No sendfile/splice implementation was found in tracked src/include by git grep. Current framed checksum/compression/TLS path copies payload through userspace buffers. | Evaluate feasibility with actual framing/checksum/TLS requirements and record the decision. A documented decision to retain buffered I/O for unsupported combinations is acceptable; this concerns I/O mechanisms within V3, never an old protocol/executor. Do not claim zero copy from function names or loopback speed. |
| NUMA/CPU/disk pressure observation | tools/perf/collect_env.sh records numactl hardware if available. Scheduler metrics have pressure fields, but these are not a per-run /proc CPU/disk/PSI sampler for this V3 payload path. | Add bounded, optional host observations and units/sample times; keep unavailable counters explicit. Tie observations to a run and validate parsing without requiring particular hardware. Physical NIC/NUMA pinning experiments stay deferred. |

## Next implementation scope to finalize before edits

1. Specify one bounded payload queue between disk read/checksum preparation and frame sending, with a fixed byte budget and per-lane cancellation wakeup. Use existing V3 executor/lease/checkpoint machinery.
2. Add deterministic slow producer/consumer, cancellation and transport interruption tests that prove bounded memory and complete/unique range delivery. Include TLS/checksum behavior.
3. Add optional CPU/disk/PSI observation and a written sendfile/splice suitability decision. Reuse existing collector components where they meet the contract.
4. Commit on Shenzhen, sync the same fixed source, build/hash-check both hosts, then run functional/stall/recovery tests on both. Performance evidence is bounded experimental observation, not a throughput promise.

## Non-goals and remaining route

No physical NIC/local bind/different physical path validation, formal service replacement, RDMA/SmartNIC, 100G/1Tbps readiness, or V1/V2 executor. After ten-lane acceptance, two software blocks remain: this stream-path milestone, then V3-CANDIDATE-CLOSEOUT-01 packaging/fixed acceptance/docs freeze. Re-estimate the 2-3 working-day stream budget after its concrete buffer/cancellation design; candidate closeout is estimated at another 1-2 working days if no major defects are found.
