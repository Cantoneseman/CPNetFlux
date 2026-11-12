# V3 Manifest Serialize 01 Result

Date: 2026-11-05. Status: serialization optimization and seven native integrity gates complete; no meaningful complete-task speedup established; RDMA stability remains open.

## Provenance And Scope

Single writer on R770-1 `/home/Su/projects/CPNetFlux`, branch `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`. Input `4b4bf3a71c861e25503331e140ffec1c90e183eb`; instrumented baseline `d0d98c74b15b728cf2853ec168c26a7a3cc27c70`; candidate `791c2eee785d0c197f8ff86d902f6ce8688112c8`. Both implementation commits were pushed to the verified CPNetFlux GitHub remote and read back. R770-2 receives fixed commits only. The two old untracked October task documents remain untouched.

Evidence root on both hosts: `/home/Su/experiments/CPNetFlux/2026-11-05-v3-manifest-serialize-01`. See task [scope and acceptance](2026-11-05-v3-manifest-serialize-01.md), external `HANDOFF.md`, `acceptance/comparison-summary.json` and final backup receipt. The frozen baseline and candidate benchmark binaries remain under `bin/` on R770-1; payloads are outside Git.

This stage changes CPU serialization work, with the same schema, record ordering, body CRC, RangeList rejection, checkpoint cadence, publication and final verification policies. It does not implement a pipeline, change network settings or resolve RDMA CQ/RNR stability.

## Implementation

- Added caller-owned optional save subphase observations: record preparation, range validation, record formatting and body CRC. They are disjoint subsets of total serialization; scalar header/body assembly remains in total time. Save/write/publish are nested scopes, not additive CPU costs.
- Attached actual whole-file lookup/sort/save observations within session calls, and clear pointers before a transaction can outlive the caller. JSON reports `whole_file_observation`; unobserved sender/range entries use null. The server retains its pre-existing phase-timing policy, independent of the client's mode.
- Corrected missing download observation counters for existing serialize/temp-write/publication operations. Actual publication semantics are unchanged; the smoke uses independent inotify counts to validate download publication counts.
- Serialization now borrows ordered input, or makes one private sorted copy for arbitrary-order input, then shares that view between validation and formatting. Input vectors are never mutated. RangeList still rejects zero length, overlap and overflow.
- Replaced per-record stream/temporary hex formatting with a reserved string, `to_chars` decimal output and fixed-width lowercase hexadecimal output. Scalar headers and parse paths retain their existing implementation.

The old stream implementation remains an independent test byte oracle. Empty, shuffled, sparse and extreme records, repeated chunk IDs where previously valid, special path bytes and checksum modes are covered. The baseline produced four meaningful bounded-work failures while all four format/rejection tests already passed, before candidate implementation.

## Metadata Measurements

Each version ran 24 real-session cases: download/upload x final_only at 4096/16384/65536 records and every_n_chunks/16 at 65536 x three repetitions. Zero payload bytes and no network I/O; persisted record identities and coverage were reloaded and checked. These figures are not file throughput.

| Every16, 65536 records; median seconds unless noted | Download Baseline | Download Candidate | Upload Baseline | Upload Candidate |
|---|---:|---:|---:|---:|
| Session wall | 25.396926 | 12.860539 | 25.810290 | 12.991456 |
| Serialization | 22.276926 | 9.784727 | 22.669354 | 9.800612 |
| Record preparation | 3.573700 | 0.097204 | 3.575302 | 0.100677 |
| Record formatting | 15.235227 | 5.450673 | 15.641791 | 5.453579 |
| Range validation | 1.195678 | 1.179794 | 1.194921 | 1.178295 |
| Body CRC | 0.140623 | 0.141068 | 0.140684 | 0.140453 |
| Serialization record items copied | 268632064 | 0 | 268632064 | 0 |
| Serialization sort calls | 8196 | 0 | 8196 | 0 |
| Manifest saves | 4098 | 4098 | 4098 | 4098 |

Serialized byte totals are unchanged per role: 5562031783 download / 5561847373 upload. Session outer-save sorting is still present, approximately 1.63/1.69 seconds in the candidate; zero internal serializer sorts does not mean all sorting was removed.

Download/upload metadata session wall reductions are 49.36%/49.67%; serialization reductions are 56.08%/56.77%. This justifies retaining the bounded, byte-compatible serializer optimization as a module improvement. A file-throughput gain requires its own evidence and is not inferred from these percentages.

## Real File Acceptance

Three baseline and three candidate verified_chunks downloads plus one candidate full-policy integrity sample passed all task, native-QP, protocol-terminal and external-SHA gates. Fixed 64 GiB seed SHA-256 `054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1`. One lane, native SEND/RECV, direct binds `192.168.100.1/.2`, `eno17095np0`/`mlx5_0`; TCP control, TLS/compression off, CRC32C/auto, every_n_chunks/16, commit_sync=none, no recovery. Whole-file effective range checkpoint override remains null. Production defaults are unchanged.

Task time is client launch through complete V3 task, including final verification and terminal control exchange. Independent SHA runs outside this timer. All controls precede candidate implementation; outputs are fresh, caches are not dropped, groups are not interleaved. This limits causal precision for wall-time differences. Both endpoints must use the same fixed commit/configuration/version and six matching binary SHA-256 values before every sample.

Baseline three task walls: 143.050683 / 143.248845 / 142.242556 seconds; median 143.050683 seconds, task rate 3.843084 Gbps, population CV 0.305251%. All three protocol terminals, native QP checks and external SHA passed. Outputs were removed only after SHA, checkpoints and logs retained.

Baseline actual whole-file medians: manifest aggregate 52.059743 seconds, save 48.854750, serialization 38.235814, record formatting 26.735168, preparation 6.636095, validation 2.316025, body CRC 0.176464, temp write 1.616825, publication 9.019460. There are 4099 saves, one more than the metadata fixture. Publication is not CPU formatting, and aggregate scopes cannot be summed.

| Single lane 64 GiB verified_chunks, n=3 per version | Baseline Median | Candidate Median |
|---|---:|---:|
| Complete task seconds | 143.050683 | 142.553233 |
| Complete task Gbps | 3.843084 | 3.856495 |
| Population task-rate CV | 0.305251% | 0.590554% |
| Manifest aggregate seconds | 52.059743 | 36.723001 |
| Manifest save seconds | 48.854750 | 32.261562 |
| Serialization seconds | 38.235814 | 20.727219 |
| Record formatting seconds | 26.735168 | 13.641516 |
| Record preparation seconds | 6.636095 | 0.261503 |
| Range validation seconds | 2.316025 | 3.235497 |
| Body CRC seconds | 0.176464 | 0.238251 |
| Temp write seconds | 1.616825 | 2.051479 |
| Publication seconds | 9.019460 | 9.478018 |
| Receiver lane CPU seconds | 99.317055 | 96.704573 |
| Receiver writer seconds | 28.841149 | 34.854906 |
| Receiver checksum seconds | 12.210164 | 16.147994 |
| Receiver payload I/O seconds | 40.800008 | 46.073713 |
| Sender read seconds | 125.353432 | 127.136050 |
| Sender consumer wait seconds | 112.953291 | 114.453123 |

Candidate task walls are 143.325677 / 141.289135 / 142.553233 seconds. Median task rate ratio is 1.00348957, or +0.348957%; wall reduction is about 0.3477%. The small difference, n=3 and sequential group ordering do not establish a meaningful file-throughput improvement. Retain the serializer for its measured metadata/work reduction; do not advertise the roughly 2x metadata result as file speedup. Receiver work and waits overlap sender supply, so subtracting saved serialize seconds from baseline task wall would be incorrect.

Each native sample still has 4099 successful publications and zero save failures. Candidate serialization copies/sorts are zero. Native serialized byte totals differ by 16396 bytes (4 bytes x 4099 saves), from longer candidate-labelled target/temp path text; the matched-input metadata totals and independent legacy byte oracle remain identical. No format/schema change is involved.

Baseline control 01 has receive out_of_buffer / sender rnr_nak_retry_err increments of 7851; controls 02/03 have zero increments for these counters. Both endpoints show CQ error/flush 65/64 in controls 01/02; control 03 shows receiver 1/0 and sender 65/64. These historical sample differences remain visible; no zero-RNR conclusion is inherited from the previous stage.

Candidate verified_chunks 02 has receive out_of_buffer / sender rnr_nak_retry_err increments of 12762; 01/03 have zero. CQ snapshot deltas are receiver/sender 65/64 on 01/02, receiver 1/0 and sender 65/64 on 03. No transport-retry-exceeded or ACK-timeout increment is reported in these six snapshot differences. Intermittent RNR has not been fixed by serializer work; neither its occurrence nor absence identifies the cause without time/resource attribution.

The candidate full-policy task took 296.126176 seconds (1.856492 Gbps), including 155.748470 seconds in receiver finalization/readback. It reports `post_write_readback=true`; FILE_RESULT and DirectoryEnd succeeded without recovery, followed by the matching independent SHA. Its RNR/out_of_buffer deltas are zero, while both endpoints still show CQ error/flush 65/64. This single integrity sample is separate from the repeated verified_chunks comparison and does not establish a full-policy throughput change.

The existing server `CPNETFLUX_PERSISTENT_TIMING` records show baseline sender read time median 125.353432 seconds, producer CPU about 25.55 seconds and consumer wait median 112.953291 seconds, within a roughly 139-second file phase. Candidate 01 reads for 128.560319 seconds and waits for its producer for 116.307574 seconds. These overlapping scopes suggest source supply limits this particular file workload even after receiver manifest work falls. They do not establish standalone disk bandwidth or isolate read syscall cost from scheduling/page-cache behavior.

Read-only environment snapshots (`acceptance/storage-environment-R770-*.json`) show approximately 30 GiB RAM per host, the experiments on `/dev/sda2` ext4, and `sda` exposed as a 446.6 GiB non-rotational RAID device. The 64 GiB seed exceeds memory; candidate 01 sender physical read counters cover approximately 64 GiB. Do not call these samples in-memory network tests or assume RAID model/ROTA establishes a 100G-capable storage service rate.

## Verification And Remaining Work

- Observation baseline: R770-1 related 149/149 and peer 157/157 CTest passed. Whole-file upload/download phase on/off, independent publication count, range on/off and recovery gates included.
- Candidate: R770-1 full 534/534 CTest passed in 257.50 seconds; R770-2 related 138/138 passed in 38.97 seconds. Build and six-binary/configuration/version gates passed. No build or metadata benchmark runs during native file task timing.
- Seven native tasks and independent hashes passed, and successful fresh payloads were removed only after SHA; checkpoint/log evidence and the hardlinked frozen source remain. Comparison summary reports complete=true.
- Post-run inspections pass on both hosts: original production PIDs 126766/164834 remain, no experiment process/listener/application QP remains, and only the original kernel GSI identity is present. Dynamic GSI PSN is not treated as a stable identity. Final docs-only synchronization must retain the six measured binary hashes; final-state and GitHub backup receipts record the synchronized documentation SHA and source SHA separately.

Relevant acceptance commands and logs (use their fixed source commits and a fresh evidence directory for reruns):

```sh
ROOT=/home/Su/experiments/CPNetFlux/2026-11-05-v3-manifest-serialize-01
# R770-1
cmake --build /home/Su/projects/CPNetFlux/build -j6
python3 "$ROOT/run_validation.py" 192.168.100.1 candidate-full-ctest.log
python3 "$ROOT/profile.py" baseline
python3 "$ROOT/profile.py" candidate
python3 "$ROOT/experiment.py" compare
python3 "$ROOT/experiment-candidate.py" compare
python3 "$ROOT/summarize.py"
# R770-2, same ROOT path
python3 "$ROOT/run_validation.py" 192.168.100.2 candidate-gates-r770-2.log 'Manifest|Session|PersistentTree|Rdma|whole_file|receiver_policy|range_observ'
```

`ROOT` denotes the evidence root above. Baseline and candidate runners intentionally require their exact commits and clean tracked sources; they are not commands to run against an arbitrary later checkout. `run_validation.py` supplies a transient test credential via environment, without persisting it in the repository. Red evidence: `acceptance/observation-red.log`, `acceptance/observation-download-counters-red.log`, `acceptance/serializer-red.log`. Separate observation fixture corrections remain logged and are not product regressions.

The existing CQ error/flush and prior RNR observations remain open until this stage's counters and a separate lifecycle investigation establish their cause. This is not production readiness or a 100 Gbps acceptance.

## Next Serial Step

1. Establish source service rates before choosing another receiver optimization. Reuse the existing storage benchmark in explicit read mode, keeping the seed, with 64 KiB / 1 MiB / 8 MiB application buffers, one queue and three repeats per setting. Record physical read bytes, read-call count, CPU and wall time. The current benchmark removes its path unless `--keep-file` is supplied, including in read mode; retain that flag in every seed-read command. Do not run these reads during a native file timing sample or drop system caches.
2. Compare source read-only, source read/checksum/frame preparation and actual native transfer waits. Existing sender pipeline depth is 2, with only 131064 budget bytes in this workload. Its roughly 113-second consumer wait is a reason to investigate supply, not proof that larger buffering will help. Distinguish storage wait, short reads, scheduling and queue starvation before implementing prefetch or larger bounded reads.
3. Add a separately labelled cache-resident or synthetic-source native control only after verifying its physical I/O counters; retain the 64 GiB real-file workload. This separates application/network capacity from storage capacity. No 100G storage guarantee follows from the current RAID device label.
4. Attribute CQ error/flush and intermittent RNR through separately counted live transfer, FILE_RESULT/DirectoryEnd and QP close/destroy intervals. Preserve outstanding receive ownership, terminal semantics and failure recovery. Do not classify the observed 65/64 increments as harmless solely because SHA and terminal exchanges pass.
5. Choose a single measured factor for the next code candidate. Checkpoint cadence/shared locks remain candidates; broad pipeline/buffer changes and new defaults wait for supply and lifecycle evidence. Keep the fixed-commit, matching-build, SHA and serial-writer gates.

The storage/supply experiments above are the next task, not measurements performed in this stage. Its input is the final backed-up documentation commit; measured source remains `791c2eee785d0c197f8ff86d902f6ce8688112c8` until a separately tested implementation is fixed.
