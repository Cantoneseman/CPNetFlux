# V3 Cache Path 01 Result

Completed 2026-11-06 on R770-1/R770-2 through passwordless SSH only. Single writer
on R770-1; no desktop control, agents, worktrees, network or system tuning.

## Versions and Scope

- Previous input: 94cf591c679679cb3988c4fe87ebd879753422a4.
- Route/baseline: 183002d31e5a38c854df09611e999f56829246bb, docs-only change.
- Measured implementation: 46be03da033401e8e547246cd78c616c76ebce38.
- Runtime change: src/core/io/persistent_tree_transfer.cpp passes a four-byte
  stack CRC prefix and the existing payload to writeFrameBuffered. It no longer
  allocates a protectedPayload vector and copies the DATA body into it.
- Header payload length, lease I/O accounting and wire accounting include the
  prefix. Raw/compressed CRC frames keep the same byte format and flags. The
  existing TCP/TLS/RDMA span APIs are reused. RDMA still copies these spans into
  registered staging memory before returning; this is not zero-copy networking.
- Test change: tests/unit/framed_data_socket_test.cpp observes real TCP sendmsg
  spans and checks an independent bitwise CRC32C oracle, logical payload bytes,
  compression and short raw fallback, no-checksum mode, nonaligned chunk endings,
  final committed output and protocol completion.
- Pipeline depth/budget, frame limit, checksum/checkpoint/verification defaults,
  compression defaults and RDMA queue/registration policies are unchanged.

All three phase commits were pushed to the verified CPNetFlux GitHub origin with
exact SHA readback. The final documentation receipt and next input revision are
recorded in the external HANDOFF.md and acceptance/github-backup.json.

## Experiment Contract

One actual 1 GiB file on each end's tmpfs at most, one transfer at a time. A
random 8 MiB block is repeated 128 times; compression is off in measured native
tests. This file is materially smaller than the historical 64 GiB disk workload:
1024 logical 1 MiB chunks and 17408 DATA frames. No new 64 GiB experiment ran.

R770-2 source to R770-1 receiver, one native RDMA lane, explicit .100.1/.100.2
binds through eno17095np0/mlx5_0; control TCP, TLS off in this isolated diagnostic.
CRC32C/auto, verified_chunks, every_n_chunks/16 and commit_sync=none in the six
primary samples. Full verification is a separate single candidate correctness
control. These are non-durable tmpfs tests, not storage bandwidth or production
security/stability acceptance.

Same fixed commit/config/version and seven binary SHA-256 checked before baseline
runs; eight checked before candidate runs, adding the new framed-socket test
executable. Source pages all resident before/after each valid sample. Whole sender
process physical reads: baseline 0/0/73728 bytes; candidate and full control zero.
The 72 KiB includes unrelated metadata/library I/O and is not source-file-specific.
No positive source-interval swap-usage delta. Source/target residency, memory,
routes, HCA/interface counters, QP IDs, process I/O and raw logs are retained.

## Results

Medians of three samples per primary group:

| Metric | Baseline 183002d | Candidate 46be03d |
|---|---:|---:|
| Whole CLI task seconds | 2.817790286 | 2.767708014 |
| Whole CLI task Gbps | 3.048464832 | 3.103627459 |
| Population task-rate CV | 0.851890% | 0.864489% |
| Data connection setup seconds | 2.10216 | 2.09972 |
| Receiver file phase seconds | 0.482160294 | 0.484688199 |
| Receiver file phase Gbps | 17.815516331 | 17.722599002 |
| Sender frame preparation seconds | 0.061855149 | 0.001985696 |
| Sender socket write seconds | 0.038260775 | 0.092577312 |
| Sender lane thread CPU seconds | 0.153226144 | 0.155226889 |
| Sender producer thread CPU seconds | 0.230730977 | 0.252483731 |
| Receiver write seconds | 0.232686036 | 0.233963399 |
| Receiver checksum seconds | 0.087177803 | 0.086324769 |

Frame-preparation elapsed time decreased about 96.79%, but socket-write elapsed
time increased and lane CPU did not decrease. File-phase rate is effectively
unchanged (about 0.52% lower); task rate about 1.81% higher does not establish a
useful end-to-end improvement. Groups are sequential, not randomized/paired, and
these small transfers are dominated by connection setup. Do not present the
removed copy or the preparation timer as an equivalent throughput/CPU gain.
Retain the scoped copy removal and its byte/span regression, while recording
that its net performance value remains unproven in this workload.

The receiver file interval includes payload processing and commit/result work;
it is not a wire-only steady-state rate. Whole CLI task time includes connection
setup and final summary work, excludes the independent SHA. Nested sender and
receiver timers cannot be added or subtracted as disjoint task costs.

Full control: 2.967994032 s / 2.894188634 task Gbps; receiver file interval
0.604183991 s / 14.217415092 Gbps, finalize/readback 0.123911835 s. One correctness
sample does not constitute a repeated full-policy comparison.

All seven formal native samples passed CRC32C, independent output SHA-256,
FILE_RESULT/DirectoryEnd, QP evidence and zero recovery attempts. The source SHA
also passed a final independent read before source removal:
ddf9db6fba632fc0c5b7baf7a182136b2cef2b64cad4b2c0d655c474897ca42d.

## Stability Boundary

All seven formal samples had zero sender RNR and receiver out_of_buffer deltas.
Receiver resp_cqe_error/resp_cqe_flush_error deltas remain 65/64, with 193/192 in
the first candidate; sender is usually 65/64, first baseline 1/0. req_cqe error
and flush deltas are zero. Successful transfers do not explain these counters.
They remain a separate unresolved stability gate, not a fixed/no-error claim.

## Verification

- Final regression on unchanged source: only expected CRC 3-span assertions
  failed; candidate green. Earlier compile/oracle authoring failures are retained
  and not counted as the meaningful red cycle.
- R770-1 initial full CTest: 536 registered; 531 passed, two token-env failures,
  three explicit-bind hardware skips. Proper test environment supplied a fresh
  in-memory token and 192.168.100.1; all five failed/skipped cases then passed in
  11.83 s. Combined coverage: every registered test passed. The first full run
  remains a failed run in the evidence.
- R770-2 related CTest: 137/137 passed in 42.13 s, explicit local RDMA bind and
  fresh temporary test token. No credentials written to source or raw logs.
- Both builds pass and candidate eight binary hashes/config/version match.
- git diff --check passed before commits. Runtime version evidence is frozen at
  the implementation SHA; later documentation-only changes retain binary hashes.
- Initial experiment pilots rejected overstrict whole-process read-byte limits;
  one restart also detected an owned tree sidecar. These are excluded from the
  formal matrix. The runner now records process reads separately from mincore
  residency and removes the exact owned tree sidecar. Failure logs remain.

## Cleanup and Evidence

Each fresh output and its checkpoint/tree artifacts was removed after SHA, with
small checkpoint copies and path/inode/size receipts retained. Failed pilot
outputs were also removed after preserving failure evidence. Final source hash
and inode/size were checked before deleting the task-owned source and tmpfs roots.
Both temporary roots are absent, no experimental server/runner remains, original
production PIDs 126766/164834 are alive, and only the original kernel GSI QP
identity remains. Disk available after cleanup: 87945863168 / 209239461888 bytes
(R770-1/R770-2). Historical datasets, backups, CPSS and existing dirty documents
were not cleaned or modified.

Evidence root on R770-1 (small relevant artifacts also copied to R770-2):
/home/Su/experiments/CPNetFlux/2026-11-06-v3-cache-path-01

Key artifacts: experiment.py, baseline_evidence.py, analyze.py, validate.py,
cleanup.py, metrics/*.verified.json, per-end logs/snapshots,
acceptance/comparison.json, matching-builds-*.json, ctest-full.log,
ctest-resolved.log, ctest-peer.log, cleanup-R770-*.json and github-backup.json.

## Next Serial Development

1. Preserve 1 GiB correctness/performance controls. Reuse bounded sender payload
   buffers only after socket write returns (RDMA staging copy completed); verify
   cancellation, shutdown and fault paths. Keep queue/total ownership budgets
   explicit; compare total lane/producer CPU and file phase, not only one timer.
2. Profile receiver frame allocation/copies and fresh tmpfs write/page-fault cost.
   Current write/checksum are about 0.234/0.086 s per GiB. Choose receiver buffer
   reuse or stage decoupling using those measurements, preserving CRC-before-
   record/publication and bounded memory.
3. Investigate RDMA connection resource lifetime independently: the current
   receive registration covers 2080 slots x 1052672 bytes (about 2.04 GiB) per
   endpoint, including the data sender. Initialization dominates small task time.
   Measure allocation, MR registration, posting and connection reuse separately.
   Do not shrink queues or change negotiation without RNR/ownership/recovery gates.
4. Attribute CQ error/flush deltas at payload, FILE_RESULT/DirectoryEnd, close and
   QP destroy boundaries before stability acceptance. Source storage io_uring
   queue tests and 64 GiB repeat matrices remain deferred by the user instruction.

The unchanged 64 GiB disk source result remains near 4 Gbps. This 1 GiB tmpfs
file phase exposes software work around 17.7-17.8 Gbps under tested conditions;
neither it nor the direct RDMA primitive proves 100 Gbps file capability.
