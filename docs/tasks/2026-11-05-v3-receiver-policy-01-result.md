# V3 Receiver Policy 01 Result

Date: 2026-11-05. Input: 330b959d4d30b5e8b63cf049f3a8d61b5fc1bc31.
Measured source: d323f1fa442071bee64650c44c96d8a11ba42b1f.
Status: code gates and six fresh single-lane 64 GiB policy comparisons accepted for protocol/integrity. Link/production stability is not accepted.

## Implementation

Download CLI now exposes the existing local receiver final-verify, manifest flush policy/interval, commit-sync and range checkpoint override. Defaults remain full, every_n_chunks/16, none, and automatic range selection. The local receiver also receives the selected checksum backend. Upload CLI rejects these receiver flags because its receiver is remote.

The receiver_policy JSON distinguishes local configuration from an unobserved remote receiver. An actual range batch is reported independently of phase timing; whole-file work reports no observed range batch. An explicit range override takes precedence over the existing environment variable. No protocol or server policy negotiation changes.

The truncation regression exposed a pre-existing publication gap: checksum=none skips session readback despite entering the full branch. Both receiver roles now check physical file length after writer flush and complete coverage, before publication. No-checksum JSON accurately reports coverage_only and no readback. This is not a content integrity guarantee. See the verification semantics decision; the historical report's no-checksum readback statement is superseded.

verified_chunks still verifies arriving data and complete chunk coverage, and resumed data is revalidated. It omits full's post-write reread and therefore has different failure coverage. Its availability is an explicit policy choice, not a change to the default or an equivalent-integrity performance claim.

## Code Validation

Evidence root on both hosts: /home/Su/experiments/CPNetFlux/2026-11-05-v3-receiver-policy-01.

- Actual-client red gate rejects the new flag before implementation: acceptance/receiver-policy-red.log.
- Focused accepted unit gate: 47/47, acceptance/receiver-policy-unit-accepted.log. Includes role-scoped parsing/default preservation, bad CRC rejection, corrupt resume checkpoint retransmission under both final policies, and physical truncation rejection for both receiver roles with CRC32C/none.
- Actual-client policy smoke: 7/7 cases, acceptance/receiver-policy-smoke-accepted.log. Includes durability and manifest options, checksum=none semantics, upload unobserved fields, and explicit/automatic range selection without phase timing. Every case independently compares tree content hashes.
- Full R770-1 CTest: 514/514, no skips, 266.74 seconds, acceptance/ctest-r770-1.log. Generated test credentials stay in process; native tests bind 192.168.100.1. Build and git diff --check pass.
- R770-2 focused CTest: 137/137, no skips, 37.66 seconds, acceptance/ctest-r770-2.log. Includes the new policy smoke, persistent/range/session/recovery tests and parameterized corruption/truncation cases, with native bind 192.168.100.2.
- Both hosts build measured source d323f1fa with matching CPNETFLUX CMake options and version 0.1.0. Server, tree upload/download clients, RDMA bench and unit binary SHA-256 values match. Receipt: acceptance/matching-builds.json. The source was committed/pushed to the verified CPNetFlux GitHub remote and its branch SHA read back equal before peer synchronization and transfers.
- Initial failed smoke and truncation logs are retained as diagnostic evidence. The smoke's no-checksum server fixture was corrected to match the client checksum negotiation. The truncation failure led to the physical-size guard and corrected policy semantics.

## Controlled Native RDMA Comparison

Six serial 64 GiB single-lane downloads alternate full, verified_chunks for three repetitions. Source is the previous stage's verified seed on R770-2, hardlinked into the new evidence root without another payload copy. Both ends use fixed source/configuration and native RDMA SEND/RECV, explicit 192.168.100.1 / 192.168.100.2 binds, CRC32C/auto, compression off and zero recovery retries. Manifest policy stays every_n_chunks/16 and output commit_sync stays none. This is not an fsync durability comparison. No system networking, service or cache configuration is changed.

The requested client chunk size is 4 MiB, but the remote download source supplies an effective 1 MiB file identity, giving 65536 records per 64 GiB file. The explicit range override is 16; observed effective range batch is null because c1 executes whole-file work. It does not change the non-range manifest cadence.

Task rate = 64 * 2^30 * 8 / client-process wall seconds / 1e9. It includes setup, data, receiver commit and data/control termination plus process/summary overhead. Independent external SHA-256 runs after the timer and before accepting/removing the fresh output. It is not included in task rate for either policy. Normal buffered filesystem/cache behavior is retained; this is not a cold-cache or raw-storage benchmark. Phase timers overlap and must not be added to task wall time.

| Policy / repeat | Task wall seconds | Task Gbps | External SHA / protocol |
|---|---:|---:|---|
| full / 01 | 301.525458 | 1.823248 | PASS |
| verified_chunks / 01 | 144.949958 | 3.792728 | PASS |
| full / 02 | 297.185652 | 1.849873 | PASS |
| verified_chunks / 02 | 144.900314 | 3.794028 | PASS |
| full / 03 | 299.413863 | 1.836107 | PASS |
| verified_chunks / 03 | 143.511497 | 3.830744 | PASS |

Full median task rate is 1.836107 Gbps, median wall time 299.413863 seconds and population CV 0.592008%. verified_chunks median is 3.794028 Gbps, 144.900314 seconds and CV 0.463039%. The median task-rate ratio is 2.066344; median task time drops about 51.6%. This is the measured cost/coverage tradeoff of an explicit existing verification policy, not an equivalent-integrity algorithm speedup. Each group has only three repetitions.

Every accepted output must match SHA-256 054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1, exact 64 GiB, one file/lane, no recovery and native QP initialization on both ends. FILE_RESULT, data DirectoryEnd and the successful client control terminal are required. Policy configuration is checked against receiver_policy JSON. Commands, source identifiers, /usr/bin/time resources, route/HCA/block-device deltas and terminal evidence remain in metrics/*.verified.json, snapshots and logs. acceptance/summary.json recomputes the six-sample protocol/integrity gate and statistics. Successful fresh payloads are removed only after external SHA and checkpoint retention; original seed and older evidence remain.

## Findings And Limits

- Full receiver finalization spans 154.819-160.372 seconds, median 155.591705. verified_chunks finalization spans 0.052822-0.064520 seconds, median 0.058609. Maximum sender FILE_RESULT wait is 165.099 versus 4.002 seconds. All terminal exchanges complete successfully.
- Receiver block-device read deltas are about 63.26-63.44 GiB for full and 0.010-0.218 GiB for verified_chunks. These host-wide deltas support the observed readback cost but are not per-process IO or a raw storage throughput benchmark. External SHA IO is outside these task snapshots/timers.
- Manifest aggregate time remains 52.42-62.34 seconds across the six cases; it includes record updates and checkpoint/file-state work, not just serialization. Client user CPU is 68.71-71.57 seconds and system CPU 31.60-44.22 seconds. Peak RSS remains 2308200-2310320 KiB (about 2.20 GiB). This stage does not reduce the RDMA receive pool.
- Full samples have receiver out_of_buffer and sender rnr_nak_retry_err deltas of 2685, 7681 and 5633 respectively. All verified_chunks samples have zero deltas for these counters. Retry-exceeded and local-ACK-timeout deltas remain zero for all six. Do not infer that the final verification policy causes or fixes RNR: the sampled increments occur before receiver FILE_END, and the hardware counters are port-wide. First-full increments occur at about 129.5-135.7 seconds after local snapshots, before FILE_END at 140.34 seconds.
- Every sample has CQ error/flush increments on both hosts, either 65/64 or 129/128. Sampled changes occur near successful directory completion. Timing does not classify them as harmless. acceptance/error-timeline.json preserves the per-host protocol/error intervals; production stability remains unaccepted. This is a new observation set, not a rewrite of the prior stage's zero-RNR samples.

verified_chunks does not supply full's post-write checksum reread. External SHA validates these experimental outputs, but sits outside both task timers and is not a new built-in production safeguard. The default remains full; there is no demonstrated default-policy throughput gain. This run does not establish 100 Gbps file throughput, fsync durability, symmetric uploads, four-lane policy performance, full baseline rerun or production readiness. Diagnostic servers use anonymous control with TLS disabled on the isolated experiment path.

## Next Implementation

1. Profile non-range record insertion separately from manifest serialization while retaining current CRC, recovery and publication checks. DownloadSession and TransferSession scan all prior records on each recordVerifiedChunk call; a 65536-record append path has quadratic duplicate-lookup work. Measure lookup/serialization CPU and wall time before attributing the roughly 60 second manifest aggregate to either one.
2. Test an indexed lookup or append path that preserves duplicate offset/chunk-ID conflict rejection, out-of-order records, resume revalidation and the existing on-disk format. Compare at equal every_n_chunks/16 and full/verified policies separately. Do not combine an index change with a checkpoint frequency change.
3. Investigate manifest serialization and a bounded checkpoint recovery window next, then shared range lock hold. final_only changes recovery coverage and is not automatically a production default. Compare c4 separately after c1 attribution.
4. Investigate data-tail RNR/buffer increments and completion-time CQ errors with receive/repost and application-blocking timelines. Only then use the evidence to choose bounded three-stage buffering and smaller RDMA pools. No unmeasured 10-20 or 100 Gbps claim follows from this stage.

## Cleanup And Backup

The matrix driver exited 0. Both hosts have no active experiment process or temporary control listener on ports 47301-47306; only the base GSI QP remains. Production PIDs 126766 / 164834 remain running. R770-1 has about 82.9 GiB free and R770-2 about 195.3 GiB after successful fresh-output cleanup. The original seed and old evidence remain. Receipts: acceptance/final-state-R770-1.json and final-state-R770-2.json. They require unchanged measured binary hashes/configuration and are refreshed after documentation synchronization.

Only this stage's explicit source/docs files are committed. R770-1's two pre-existing untracked 2026-10-14 task documents, Windows project sources and unrelated CPSS(DCC) data remain untouched. The final post-measurement commit changes documentation only; no transfer is performed from a dirty or different-source build.

Final source, acceptance commands, evidence location and next steps are persisted in the task plus external HANDOFF.md and NEXT-READONLY.md. acceptance/evidence-scripts.sha256 identifies the experiment/analysis scripts and frozen helper. The final documentation commit and GitHub SHA readback are recorded in acceptance/github-backup.txt and HANDOFF.md outside Git; the measured source remains d323f1fa.
