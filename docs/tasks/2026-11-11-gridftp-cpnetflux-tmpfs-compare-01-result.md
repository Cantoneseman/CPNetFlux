# CPNetFlux/GridFTP tmpfs compare 01 result

Date: 2026-11-11
State: STOPPED_AT_RESOURCE_GATE; authentication resolved, formal comparison incomplete.
Both nodes executed commit 03bca8b871567ec13701bc5eec48207a534d887e; production source is unchanged from a811f0b54c4075a5c13a3208cf14015e378d3963.
Evidence root: `/home/Su/experiments/CPNetFlux/2026-11-11-gridftp-cpnetflux-tmpfs-compare-01`.

## Completed prerequisites

- R1 single writer on branch codex/V3-GRIDFTP-OPTIMIZED-RERUN-01; GitHub origin git@github.com:Cantoneseman/CPNetFlux.git verified. R2 detached at the exact commit from the same repository. Two unrelated untracked R1 worker-state documents were preserved.
- User authorized local test credentials. An isolated SimpleCA signed separate per-node user/host certificates; per-node RFC3820 proxies generated locally with `grid-proxy-init -verify -rfc -valid 12:00` returned Proxy Verify OK. `grid-proxy-info -e -valid 1:00` and OpenSSL user/host chain checks passed. No /etc trust or institutional identity was modified; no credential is in Git. Nonsecret metadata: credentials-public-metadata.json.
- Temporary GSI servers listened only on 192.168.100.1/2:2811, data-interface explicitly bound to those IPs, ports 40000..40199, encrypt-data required, mapped test subjects to Su, restrict-paths limited to the exact task tmpfs. No anonymous/no-auth workaround.
- 8 MiB private transfers passed both directions with external SHA/tree checks and zero stop-counter deltas. This was smoke, not a performance sample.
- Both RelWithDebInfo builds succeeded with RDMA/IO_URING/ISAL/TLS ON, desktop OFF, tests ON. Three binaries matched bit for bit; builds.json records full SHA-256. Server: 125db9bf5e21cf98de5ef674d097b894cdcaabe9ed5dbadebdf3c586570e45a5; download: 20f600140276a636740b43deb172d5ecb8bab81f593733452b7e22a91c76c30c; upload: b6c9e9c53360ca0dd0ef8a071518834dd80208ac66a3c1fe38af365fb3fb76b0.
- R1 CTest: 10/10 relevant mmap/download/parallel/data-reuse/large-DATA/receive-buffer/resume/edge-case checks passed, 18.06 s; exact regex/output in ctest.log. No production code changed.

## Screening actually run

One fully materialized 1 GiB file generated with independent os.urandom blocks; allocated bytes and external SHA recorded in screen/source-random.json. R1 -> R2 only. Each candidate ran once, in alternating protocol order. No dense-directory screening or formal matrix ran.

| Candidate | Monotonic wall s* | Provisional Gbps* | SHA | Resource gate |
|---|---:|---:|---|---|
| CPNetFlux 1 pwrite | 0.514410 | 16.699 | pass | pass |
| GridFTP 4 private | 0.965031 | 8.901 | pass | pass |
| CPNetFlux 2 pwrite | 0.314188 | 27.340 | pass | pass |
| GridFTP 8 private | 1.015220 | 8.461 | pass | pass |
| CPNetFlux 4 pwrite | 0.364281 | 23.581 | pass | pass |
| GridFTP 16 private | 1.215629 | 7.066 | pass | pass |
| CPNetFlux 8 pwrite | 0.364435 | 23.571 | pass | pass |
| CPNetFlux 4 mmap | 0.264159 | 32.518 | pass | FAIL (+375 missed/OOB) |

*These are exploratory observations only. After the run, harness review found `Popen.wait(timeout=900)` uses polling and can add up to about 50 ms to monotonic wall. External SHA is outside timing; SSH startup is excluded for screening. The 0.26..1.22 s jobs are too short to ignore that bias. /usr/bin/time diagnostics are retained independently. No median/range/CV/paired winner is reported from n=1. The experiment-only harness was subsequently changed to blocking wait plus a separate timeout timer; three local no-network 0.2 s waits measured 0.2031..0.2040 s (harness-timer-check.json). No network sample was rerun after the stop gate. Smoke used an earlier controller timer and is excluded from performance tables.

## Resource-gate failure

At 08:04:17Z..08:04:18Z (16:04:17..18 CST), the four-lane mmap candidate added R2 rx_missed_errors=375 and ethtool rx_out_of_buffer=375. Earlier seven candidate windows had zero increments; R1 increments were zero. Hardware/global counter changes do not establish causal attribution to mmap. HCA/ethtool/netdev snapshots, command, route, memory/CPU/I/O and timestamps are retained in samples/screen-random-cpnetflux-4-mmap.json. No later network benchmark ran.

The last client returned pass/ok, completed_files=1, failed_files=0, bytes_transferred=1073741824; V3/TCP, four observed data connections with .2 local/.1 peer addresses. Member state=committed, tree entry=completed, control final wait returned successfully. Sender diagnostics contain all four records for batch c3d3fbd09978debba6a9c14d2a7112ef. External sha256sum source and destination both equal 2b5f8ef8bed2c96c914154aca7664733b88b2cd9f036d3e07361ef78cd6b770d. This is integrity/commit success with resource acceptance failure, not an accepted performance run.

All eight screening windows had pswpout delta=0, owned swap peak=0; sampled minimum MemAvailable exceeded 26.9 GiB. These rule out the configured swap/12 GiB stop conditions for these windows; they do not identify the NIC root cause. Sampling CPU totals are lower bounds because short-lived children may finish between 50 ms samples. /usr/bin/time provides exact client cumulative CPU/I/O where available. TCP snapshots at 500 ms spacing missed some sub-second flows; actual lane endpoint summaries and source/destination interface byte deltas confirm the selected data path. Detailed per-queue IRQ/softirq attribution was not captured.

## Not completed

Dense screening, frozen per-class winners, all formal 1/4/8 GiB repetitions, structured/dense/tiny/mixed directories, reverse formal transfers, median/CV and paired comparisons are unrun. No 64 GiB workload or native RDMA test ran. No payload steady-state or filesystem-only baseline can be claimed from these full-task samples. The old RDMA failure repair is not accepted by this TCP experiment. A later passive 10-second window per node showed zero missed/OOB increments; current RX/TX rings are 1024/1024 (maximum 8192), combined queues=32. These settings and quiet observations do not prove a root cause and were not modified.

## Cleanup and backup

Stopped only the four task-owned temporary server PIDs using recorded process groups after command/port validation. Removed only the exact task tmpfs root on both nodes; /dev/shm is empty/free 16 GiB each. Existing 2121 servers (R1 126766, R2 164834) retain original Oct 30 starts and command lines. cleanup.json/finish.log record postchecks. Obsolete task CA/proxies were removed; current task-only credentials are retained with mode 0600 inside mode 0700 directories for controlled refresh, and the local CA password was rotated to a private random value. No key or password is in the evidence bundle or Git. Scripts/evidence are outside the repository, without payload.

Small plan/result/analysis docs only are committed and pushed. Exact report commit and GitHub ref readback are recorded in completion.md after publication; only pushed commits are GitHub backup. Full raw evidence remains on R1, peer evidence is copied under peer-evidence; no credential backup is published.
