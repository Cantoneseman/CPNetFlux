# CPNetFlux / GridFTP data encryption A/B 01 result

Date: 2026-11-11. State: STOPPED_AT_RESOURCE_GATE; user question has bounded observations, not a completed repeated bulk comparison.
Evidence root: `/home/Su/experiments/CPNetFlux/2026-11-11-data-encryption-ab-01`.
Initial execution commit: 8746f129e720e3d79d021a46ed6d7a214fc64fd5. Distinct 1 GiB execution commit: 1c9f858c39eab0cf9b05c4d1ae6ddc5ebd286e66. Production source unchanged from fe0f78172ac3e85e8d83619d7dc7234d63fce093; same binary hashes on both nodes.

## Conditions and checks

- Same source/config/build/hash per node verified in builds.json and builds-1g.json. RelWithDebInfo with RDMA/IO_URING/ISAL/TLS ON. Server SHA-256 125db9bf5e21cf98de5ef674d097b894cdcaabe9ed5dbadebdf3c586570e45a5; tree-download SHA-256 20f600140276a636740b43deb172d5ecb8bab81f593733452b7e22a91c76c30c.
- R1 -> R2 only over eno17095np0 .1/.2 TCP; source and destination exact paths under /dev/shm/cpnetflux-data-encryption-ab-01, findmnt tmpfs and allocated source bytes verified. Each sample records route, netdev/ethtool/HCA/softirq/IRQ, owned process CPU/I/O/RSS/swap and MemAvailable. Local monotonic clock starts just before the actual client process; blocking wait, separate timeout, no SSH startup; external SHA outside timing. No timed verbose/debug.
- Both GridFTP arms retain GSI authenticated control/data setup. Clear arm uses default PROT C; no -nodcau/DCAU N or anonymous authentication. Private arm uses -dcpriv. Four streams, concurrency 4, block 262144, requested TCP buffer 1048576 (Linux actual 2097152). Services use explicit control/data-interface .1/.2, port 2812, restricted task tmpfs. Smoke sanitized debug recorded PROT P on both servers for private; clear emitted no PROT override (GCT default C). Raw debug was held only in memory and discarded, not saved or printed. No negotiated GSI cipher was captured.
- CPNetFlux arms both use verified control TLS + token, 4 lanes, V3/TCP persistent tree pull, pwrite direct, 65532 logical DATA bytes, receive-buffer experimental switch off, CRC32C verified_chunks, commit_sync=none, compression off. Data TLS off versus required is the only requested CP arm difference. Current source rejects 256 KiB frames/receive-buffer override/mmap with TLS, so old optimized plain/mmap numbers are not this A/B baseline. Receiver inline execution requested in both arms.
- CP server certificate has .1 SAN and strict chain/IP verification passed. Control-only probe negotiated TLSv1.3/TLS_AES_256_GCM_SHA384; this is not a recorded data-channel cipher. Actual transfer summaries report control_tls_verified=true for both arms, data_tls_verified=true only for private; V3/TCP, 4 data connections, explicit .2 local/.1 peer. Lab private keys/tokens remain 0600 in 0700 directories, no content in Git/reports.
- Relevant R1 TLS/token/mTLS/data-TLS CTest 4/4 passed, 1.82 s, after supplying the required private CPNETFLUX_TEST_TOKEN. Initial gate was 3/4 because token test env was absent; preserved in ctest.log. Initial strict Python TLS probe rejected a lab CA missing keyUsage; task CA was reissued with keyCertSign,cRLSign and strict verification passed before actual transfers. No production code/config change.

## Smoke and 4 GiB stop

All four corrected 8 MiB smokes passed external SHA, proper mode/commit or both GridFTP CODE=226, and all resource gates. Initial private smoke completed but collector erroneously required explicit DCAU A although the authenticated default need not resend it; preserve controller.log, correct collector, and use smoke-v2 evidence. It was not an authentication or transport failure.

The first 4 GiB GridFTP-private sample completed in 2.899473 s (observed 11.850 Gbps), with source/destination SHA and RETR/STOR CODE=226. It failed R1 global pswpout +1512 pages (~5.906 MiB at 4 KiB/page), owned task swap=0, minimum MemAvailable 24.56 GiB. R2 global swap and both NIC missed/OOB deltas were zero. Unresolved global swap attribution invalidates this group's performance acceptance; 4 GiB group stopped. Do not mix it with the next distinct input.

## Distinct 1 GiB observations

New fully allocated independent os.urandom file: 1073741824 B, SHA-256 d42fdbe9f508334fd44e5883969616fbba801ada2418623f629554d201b2740d. mincore confirmed all source pages resident before samples. First-round each arm passed; second round stopped as below. No medians, CV or formal speedup claims: none reached three accepted repetitions.

| Arm / repetition | Full-task s | Observed Gbps | SHA / terminal | Resource gate |
|---|---:|---:|---|---|
| grid-private / r1 | 0.976661 | 8.795 | pass | pass |
| cp-private / r1 | 0.437506 | 19.634 | pass | pass |
| grid-clear / r1 | 0.684277 | 12.553 | pass | pass |
| cp-clear / r1 | 0.415073 | 20.695 | pass | pass |
| cp-clear / r2 | 0.451725 | 19.016 | pass | pass |
| grid-clear / r2 | 0.679547 | 12.641 | pass | FAIL: missed/OOB +104 |

The first-round arithmetic is GridFTP 8.795 -> 12.553 Gbps when data encryption is disabled (+42.7% observed); CPNetFlux 20.695 -> 19.634 Gbps when enabled (-5.1% observed). These are single observations, not stable encryption penalties. A second clean CP plain observation was 19.016 Gbps, already below the private observation; ordinary run variability prevents declaring a consistent 5% cost. Both CP arms used the same TLS-compatible frame/buffer path; their data protection implementations still differ from GSI.

GridFTP first-round server RETR operation average: private 12.038 Gbps / clear 17.086 Gbps; STOR: private 11.909 / clear 16.909 Gbps. These operation windows exclude some whole-task control overhead but are not a rigorous steady-state payload measurement. No filesystem-only ceiling was measured. /usr/bin/time and monitor evidence are preserved; sampled CPU totals are lower bounds for short-lived processes. No cross-application cipher-equivalence claim.

## 1 GiB resource stop

At R2 2026-11-11T11:07:30Z..11:07:32Z (19:07:30..32 CST), second GridFTP-clear transfer incremented rx_missed_errors=104 and ethtool rx_out_of_buffer=104. Observed full-task rate 12.641 Gbps is excluded as accepted throughput. Both CODE=226 with exact 1073741824 B and external SHA passed. All six 1 GiB attempts had pswpout delta=0, task swap=0, minimum sampled MemAvailable >=26.958 GiB. Earlier five accepted windows had zero missed/OOB increments. Raw samples retain per-queue/interface counters, softirq/IRQ and source residency evidence.

This establishes that the symptom can also appear in a configuration with GridFTP/default-clear and no CPNetFlux/mmap data transfer. It does not identify the loaded receive queue, driver/IRQ cause or prove same underlying cause as the previous +375 event. Counters may reflect delayed/global accounting; do not add missed and OOB as independent packet counts. No queue/NIC/IRQ/swap setting was changed; no third round or further loaded re-test ran.

## Cleanup, publication and remaining work

Four task-owned services stopped: R1 GridFTP 3953596, CP plain 3953698, CP TLS 3953702; R2 GridFTP 1397953. Command/root/port/process-group validated before termination. Exact task tmpfs root removed both nodes, /dev/shm 16 GiB free each, long-running 2121 services retain original PID/start/args. cleanup.json verifies no task listeners, no payload. Private current lab credentials retained only for controlled refresh. Initial cleanup reporting had a ps argv type error after R1 removal; corrected reporting/remaining R2 cleanup and final consistency checks passed, with original finish.log retained.

Scope left incomplete: planned three repetitions per arm, sustained 4 GiB/bulk acceptance, reverse direction, WAN, disk/filesystem/fsync and native RDMA. Resolve receive-resource/global swap interference before restarting those runs. No 100 Gbps or 1 Tbps readiness, and no old 64 GiB failure repair acceptance.

Plan/result/analysis are the only repository changes for this diagnosis. Report commit, verified GitHub ref readback and synchronized R2 HEAD are recorded in completion.md after publication. Full raw evidence stays under the experiment root; peer samples/logs are archived under peer-evidence. No credentials/payload/build outputs are committed. R1's two unrelated untracked worker-state docs remain preserved.
