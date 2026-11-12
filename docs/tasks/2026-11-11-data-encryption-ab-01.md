# CPNetFlux / GridFTP data encryption A/B 01

Date: 2026-11-11. State: STOPPED_AT_RESOURCE_GATE; bounded observations reported, repeated comparison incomplete. Input source/report commit: fe0f78172ac3e85e8d83619d7dc7234d63fce093.

## Scope and hypothesis

User asks how fast GridFTP is without data encryption and CPNetFlux with it. Measure rather than estimate: same materialized 4 GiB random file on tmpfs, R770-1 -> R770-2, four data connections, three interleaved repetitions per arm. Four arms: GridFTP authenticated data (default PROT C), GridFTP private data (-dcpriv), CPNetFlux verified control TLS + token with plain data, CPNetFlux same control TLS + token with required data TLS. Keep each application's other settings fixed; these remain distinct implementations/security stacks, not identical crypto primitives. Obtain 8 MiB smoke, external SHA and verified terminal states before generating 4 GiB.

CPNetFlux current source does not permit 256 KiB DATA, experimental mmap or requested 1 MiB receive buffer with data TLS. Both CP arms therefore use 65532 logical DATA bytes, pwrite, receive-buffer experiment off, four lanes, 1 MiB chunks, CRC32C, verified_chunks, commit_sync=none, compression off. Do not compare this TLS-compatible pair directly against the old optimized plain 256 KiB mmap result as isolated encryption cost. GridFTP fixes streams=4, concurrency=4, TCP buffer request=1 MiB, block=256 KiB; GSI control/authentication remains enabled. Isolated GridFTP service allows both protection levels; do not disable authentication.

This bounded diagnosis is separately authorized by the user's new encryption question. The prior full matrix remains stopped after missed/OOB increments and is not resumed. Use pwrite only; no mmap re-test. Any auth, transfer, SHA/commit error, rx_missed_errors or global rx_out_of_buffer increment, pswpout/owned swap increment, MemAvailable <12 GiB, or tmpfs space violation stops further samples. Quiet baseline counters and IRQ/queue snapshots are recorded; no risk is declared cleared from quiet counters alone.

## Non-goals

No production source, system/NIC/IRQ/routing/firewall changes, no mmap, no full prior workload matrix, no RDMA, no disk/64 GiB/WAN or 1 Tbps readiness claim. Do not alter existing TCP/2121 services, user data or unrelated Git content. Sole writer R770-1 root; R2 receives fixed commits only.

## Acceptance and measurement

- R1/R2 same fixed committed revision/config/build/binary SHA before transfers; relevant TLS CTest gates pass.
- Refresh local lab GSI proxies with -verify and validity >=1 hour. Task-only CP CA/server SAN certificates and token remain private, never in Git/log contents. Verify TLS server identity; CP summary must report control/data verification as requested. Record public certificate metadata; do not assert a negotiated cipher without evidence.
- Both paths confirmed tmpfs, fully allocated incompressible bytes. Explicit .1/.2 binds; route/netdev/ethtool/HCA counters before/after each sample. Timing uses local monotonic clock around actual client start and blocking wait, with separate timeout. External SHA outside timed region. Capture client /usr/bin/time plus process-tree CPU/I/O, memory/swap and receive queue evidence. Report statistical median/min/max/CV only if all three samples in an arm pass. Stop windows remain evidence, not accepted throughput samples.
- Success requires complete external SHA, CP member committed/tree completed/control success, or GridFTP both sender RETR and destination STOR CODE=226 with exact byte count. GridFTP smoke may enable debug only in an in-memory pipe with sanitized PROT/DCAU command extraction; timed samples have no verbose/debug. Performance may only be compared at recorded configurations.
- Preserve all evidence outside Git, remove task-owned servers/payload after diagnostics, retain private lab credentials for controlled refresh. Commit/push only three small task docs and read back remote SHA; sync final R2 revision.

## Expected affected files

- docs/tasks/2026-11-11-data-encryption-ab-01.md
- docs/tasks/2026-11-11-data-encryption-ab-01-result.md
- docs/tasks/2026-11-11-data-encryption-ab-01-analysis.md
- Experiment-only scripts/evidence/credentials under /home/Su/experiments/CPNetFlux/2026-11-11-data-encryption-ab-01, payload only /dev/shm/cpnetflux-data-encryption-ab-01.

Continuation: inspect precise stop reason if any; do not silently disable guards or resume old formal matrix. Existing unrelated worker-state docs remain preserved.

## Recorded bounded-size amendment after 4 GiB stop

The four 8 MiB arms passed SHA/terminal/security checks. The first 4 GiB GridFTP-private transfer completed SHA and both CODE=226, but R1 pswpout increased by 1512 pages (~5.91 MiB at 4096 B/page), with task-owned swap=0, MemAvailable minimum=24.56 GiB, and no NIC error increment. This fails the unchanged global swap stop rule; the 4 GiB group is stopped and cannot provide accepted throughput. No swap/system setting is changed; global cause is unresolved. Initial smoke collector incorrectly required explicit DCAU A even though GSI defaults to authenticated data and need not resend that command. Preserve that collector stop separately; corrected smoke verifies explicit PROT P for private, default PROT C/no downgrade command for clear, GSI identity success, no DCAU N/-nodcau, plus both terminal records.

A distinct 1 GiB diagnosis is now fixed to reduce resident payload demands while answering the user's encryption-cost question. Remove only the task's 4 GiB source/destination, record quiet counter baseline, materialize and SHA/mincore-verify a new 1 GiB random source, then run all four arms with the same frozen settings and three interleaved repetitions each. Keep every original resource gate including global pswpout; do not merge 4 GiB, smoke or prior screening records. If this 1 GiB group triggers any resource gate, stop it and report incomplete comparison. No further automatic downsizing or guard relaxation. This is short-file encryption diagnosis, not sustained bulk acceptance.


## Execution outcome

Initial execution revision 8746f129e720e3d79d021a46ed6d7a214fc64fd5, amended 1 GiB execution revision 1c9f858c39eab0cf9b05c4d1ae6ddc5ebd286e66; source and binary hashes unchanged. Four security-mode smokes passed; 4 GiB group stopped on global R1 swap; separate 1 GiB group stopped on R2 missed/OOB +104 during second GridFTP-clear sample. Six 1 GiB attempts, five accepted resource windows, no arm reached the required three accepted repetitions. Do not compute median/CV, silently relax guards, restart old matrix, or call either configuration sustained/bulk accepted. Four temporary services/payload were cleaned. Details in result and analysis; all exact commands/counter snapshots and peer evidence are in the experiment directory. Only small docs are published to GitHub.
