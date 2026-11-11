# CPNetFlux / GridFTP data encryption A/B 01

Date: 2026-11-11. State: planned. Input source/report commit: fe0f78172ac3e85e8d83619d7dc7234d63fce093.

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
