# V3 Sender Service 12 Result

Date:2026-11-08. Input:fa25de238164b7930db4672b0c3023833f684d1c.
Implementation:351ae7ce7117da6a7250c72488f82b71ca0743c9.
Branch:codex/V3-GRIDFTP-OPTIMIZED-RERUN-01. Single R1 writer, R2 fixed-code peer.

## Decision

The sender handoff has a reproducible CPU cost. An explicit inline sender removes
its producer/queue and reduces sender CPU40.663% and both-end CPU/GiB21.852% in
the full-task control. It does NOT pass the throughput gate:median task rate
changes28.756716->28.091585 Gbps(-2.312962%),four of five paired samples regress.

**Throughput candidate:NO-GO. Default sender stays queued.** Inline remains an
explicit diagnostic/CPU-efficiency experiment, not a throughput recommendation
or automatic policy. Do not tune more sender queues/wakeups, add workers, or retry
page population based on this result. Current complete-file capability remains
about28-29 Gbps in the limited configuration;30-50 Gbps,70-85/100 Gbps and production
readiness are unproven. RDMA stays paused.

## Local Source Isolation

An external diagnostic links the input build's actual core/ISA-L libraries and
uses PosixFile/readAtAll,ChecksumComputer and BoundedPayloadPipeline. R2 source is
8 GiB resident tmpfs; same content,65532-byte frames,1 MiB chunk boundaries,
139264 frames per8 GiB,one/four nonoverlapping source ranges. Three repetitions
per cell,alternating mode order;18 samples. Consumer reads a byte per cache line
in all modes to exercise buffer ownership;this is not a TCP or full-file task.

| Local mode | One-lane Gbps / CPU s | Four-lane Gbps / CPU s |
| --- | ---: | ---: |
| Read and sink touch | 67.103 / 1.024 | 177.386 / 1.416 |
| Read,CRC and sink touch on same thread | 50.620 / 1.357 | 153.031 / 1.666 |
| Same read/CRC through depth2 producer/consumer | 38.094 / 2.865 | 128.185 / 3.178 |

Four-lane CRC/queue CV0.611%/1.038%;queue median voluntary switches130371 versus3
for same-thread. CRC tuples and consumer sink totals agree across comparable
modes;exact bytes/frame counts pass. At most1048512 bytes application payload
capacity across four queue lanes;the8 GiB tmpfs source is separately accounted.
No source/network policy, affinity or NUMA setting changed.

The normal memory guard did not recognize this external executable:its owned
RSS0 field is NOT a measurement of zero memory. Preserve those original receipts.
Host headroom/swap and explicit capacity checks passed;an additional time-v
four-lane queue probe reports maxRSS5236 KiB,major faults0,swaps0,file inputs0,
exit0. This closes a resource visibility gap without treating local service rate
as network throughput. Minimum sampled host MemAvailable20.062 GiB for18 samples.

## Implementation and Regression Coverage

CPNETFLUX_SENDER_PAYLOAD_EXECUTION accepts queued(default) or inline;invalid
values fail before payload/publication. Inline prepares each DATA frame on the
network lane and retains one reusable frame vector. No producer thread/queue is
created. Both modes share the same missing-range preparation,checksum accumulator,
compression/corruption handling,framed socket output,work lease start/finish,
source identity checks,result lifetime and terminal path. Frame format,CRC,
checkpoint/recovery,atomic publication and receiver policy are unchanged.

JSON exports sender_payload_execution;inline queue depth/budget/high watermark,
queue waits and producer-thread CPU are0;per-range buffer ownership budget65532
bytes. Four active lanes therefore own at most262128 sender payload bytes.
Admission's existing stage-wide bound remains conservative and does not expand.
Optional sender CPU observations explicitly export read_checksum_cpu_owner=lane
for inline and producer for queued. Inline read/checksum are nested in lane CPU;
queue counts/CPU are0. Never add those intervals again to the lane total.

Test-first real striped upload/download smoke failed against the old binary with
missing sender execution contract. New smoke checks actual single-buffer/zero-
queue accounting,content/CRC,both endpoints,CPU containment,enabled/disabled
observation and invalid-setting rejection. Existing two-range/content/publication
unit regression now explicitly runs both modes and retains positive queue checks
for queued;its8-byte fixture also exercises checksum=none and range boundaries.

- R1 initial default related CTest202/202,76.37 s.
- R1 first inline201/202,76.41 s:legacy assertion required sender queue>0. No data
  corruption;receipt retained. Updated that contract to test both execution modes.
- R1 expanded final inline208/208,96.52 s,including compression and interrupted,
  service-restart and timeout resume. Formatting-only unit rerun1/1 passed.
- R2 fixed source default208/208,96.52 s;inline208/208,96.97 s.

These are related subsets,not a new full-suite or production acceptance. Both
hosts built source351ae7ce with matching configuration/version0.1.0/eight binary
SHA-256 hashes before every network task. Source pushed to the verified CPNetFlux
GitHub origin and remote SHA read back. No interhost performance task used a dirty
source tree.

Relevant acceptance commands from project root:

```sh
cmake --build build -j4
CPNETFLUX_SENDER_PAYLOAD_EXECUTION=inline ctest --test-dir build --output-on-failure -R 'Persistent|BoundedPayload|TransferStages|WorkExecutor|DynamicFileQueue|HotPathCompression|sender_(cpu|inline|execution)|range_.*observability|range_observation_export|mapped_download|tree_resume|v3_.*resume'
```

R2 also ran that CTest regex without the environment override.

## Complete-Task Acceptance

Five interleaved pairs:queued,inline / inline,queued / queued,inline /
inline,queued / queued,inline. Same commit/build,source and configuration.
R2->R1,8 GiB resident source,four TCP lanes/eight1-GiB ranges,read0/write0,
depth2/batch1,inline/mmap receiver with64 MiB windows,CRC32C/verified_chunks,
checkpoint16,no compression/preallocation,TLSoff,commit_sync=none. Existing
range/payload detail and added sender CPU clocks are OFF in both arms;phase
summaries remain. Explicit192.168.100.2/.1 bind/routes/eno17095np0 counters retained.
TCP over this NIC is not native verbs;no native QPs were initialized.

| Per8 GiB task,median | Queued | Inline |
| --- | ---: | ---: |
| Task Gbps | 28.756716 | 28.091585 |
| CV | 1.350980% | 0.853702% |
| Wall seconds | 2.389684 | 2.446266 |
| Sender process CPU s | 9.96 | 5.91 |
| Sender user / sys s | 2.45 / 7.35 | 1.59 / 4.29 |
| Receiver process CPU s | 7.89 | 7.99 |
| Receiver user / sys s | 1.49 / 6.40 | 1.48 / 6.49 |
| Both-end CPU seconds/GiB | 2.213750 | 1.730000 |
| Sender allocated capacity summed over8 ranges | 2097024 bytes | 524256 bytes |

Medians of individual components need not add to the median total. Task timer is
launch through successful completion;independent external SHA is outside it.
Paired throughput changes:+0.814%,-5.547%,-3.055%,-3.084%,-1.636%. CV passes,but
the required >=10% median task improvement fails. CPU savings alone cannot pass.
Receiver CPU stayed approximately8 s;the end-to-end data do not uniquely prove
receiver limitation versus losing sender read/write overlap. There is no new
evidence that adding sender concurrency or buffering will fix the limit.

An extra observed diagnostic pair,excluded from these acceptance cells,passed
CPU-owner/count/byte/containment checks on the actual path. Queued/inline sender
CPU10.92/5.81 s;wrapper read2.536/1.833 s,checksum1.373/1.048 s,socket3.834/1.801 s,
push+pop0.962/0 s. Each mode has only ONE observed sample. It is not an observer
overhead estimate or an additional throughput acceptance result. Concurrent
checksum cost remains higher than the local0.337-0.380 s control and is not fully
explained by queue CPU. Future analysis must not infer a unique kernel cause.

## Integrity, Resources and Cleanup

All10 primary plus2 diagnostic tasks pass independent SHA-256,logical bytes,
source residency,V3 data/control/range terminals,route/interface and resource
gates. Physical source reads0. Per task139264 DATA/read calls and128 receiver
map/unmap pairs;8 GiB mapped-written,active mapped bytes0,map failures0. Fresh
outputs/sidecars removed after each task. One source and one output at most.

Across12 tasks,minimum MemAvailable:R1 20.562 GiB/R2 20.003 GiB;maximum polled
owned RSS:R1 242.891 MiB/R2 19.535 MiB;owned VmSwap and host pswpout deltas0.
Polled RSS is not an instantaneous peak guarantee. Source SHA-256:
5b1192afb8f9f7144ceecf08b952ceb079e65ac6fbee395fd8cd59be3ab80ddd.
Source size/SHA verified before unlink. Both final tmpfs used bytes0,no owned
live transfer,production PID126766/164834 with originalOct30 start times retained.
Only exact owned paths were cleaned;unrelated October14 tasks,CPSS and old payload
evidence untouched. No agents,worktrees,desktop or chat dispatch used.

## Evidence and Next Scope

R1 evidence root:/home/Su/experiments/CPNetFlux/2026-11-08-v3-sender-service-12.
service-analysis.json/local-service.json,source_service_12.cpp/binary and original
per-sample guard/resource receipts;service-resource-probe.json/txt. Primary
matrix-analysis.json/metrics/*-lean-*.verified.json/matrix.log;separate profile
pair receipts/profile-pair.log. Acceptance build/CTest/red logs and fixed-build
identities;per-host snapshots/logs;source checksum and per-task cleanup receipts.
Scripts:sender_harness_12.py,sender_matrix_12.py,profile_pair_12.py,
analyze_service_12.py,analyze_matrix_12.py,final_audit_12.py. Final cleanup,
backup-readback.json,provenance.json and evidence-sha256.json retain identities.

Dependencies remain outside Git:baseline_evidence.py in tcp-scaling-audit-01,
v3_8g.py in path-isolation-01,input-library provenance in sender-cpu-11. The local
diagnostic was linked before the runtime candidate;checksum/storage/pipeline
implementations did not change. Preserve its input identity and binary hash.
No payload,credential or build artifact is committed to Git.

Next scope keeps the queued sender and isolates receiver socket processing,
mapped file processing and shared-state work on the actual critical path. The
first diagnostic must distinguish downstream limitation from sender overlap loss
under matched sender/file geometry;the earlier no-file frame benchmark is not
that matched counterfactual. Choose another runtime candidate only after that
evidence. Sender fusion/queue tuning closes here;RDMA and structural expansion
remain paused. Preserve verified behavior and the negative throughput result.
