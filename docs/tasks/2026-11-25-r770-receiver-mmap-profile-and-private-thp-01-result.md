# R770 receiver mmap profile and private THP result

Date: 2026-11-25

## Decision

The 1 GiB private tmpfs `huge=never` versus `huge=within_size` matrix is rejected as an acceptance result. Two independent runs reproduced the same network gate failure before five interleaved pairs could complete. The private THP page policy is not promoted to the annual baseline and no throughput claim is made from the incomplete matrix.

The earlier 512 MiB `within_size` row remains an exploratory signal only. Its complete-task rate was 36.4024 Gbps versus 28.4449 Gbps for the paired `huge=never` row. It passed its single-row correctness, page-policy, resource, network, and cleanup gates, but it is not a stability result.

## Fixed workload

- V3 TCP, four lanes, 256 KiB DATA, 1 MiB requested receive buffer.
- Application pacing 1,500,000,000 bytes/s per lane.
- CRC32C, 1 MiB chunks, checkpoint interval 16, compression off, mmap receive path.
- One 1 GiB resident source file and a private 2 GiB tmpfs target per sample.
- `T_ready` includes private namespace/mount setup, target-directory preparation, and client launch through exit. SHA observation and cleanup are reported separately.
- No ring, IRQ, RSS, MTU, offload, kernel, or pacing setting was changed.

## Matrix evidence

The first run is preserved at `/home/Su/experiments/CPNetFlux/2026-11-25-r770-thp-1g-ab-02`. It completed rep 1 control at 31.1777 Gbps and rep 1 candidate at 38.6867 Gbps. Rep 2 candidate was rejected with R1/R2 `rx_missed_errors` and `rx_out_of_buffer` delta 54 and TCP retransmission delta 6. Its `acceptance.json` has `valid_sample=false`, `network=false`, `cleanup=true`, and its `cleanup.json` confirms the source, peer root, and local mount were removed.

The second run is preserved at `/home/Su/experiments/CPNetFlux/2026-11-25-r770-thp-1g-ab-03`. It completed rep 1 control at 32.3974 Gbps. Rep 1 candidate was rejected with R1 `rx_missed_errors` and `rx_out_of_buffer` delta 28 and TCP retransmission delta 7. Its `acceptance.json` has `valid_sample=false`, `network=false`, `cleanup=true`, and its `cleanup.json` confirms the same cleanup. The second rejection reproduces the hardware receive-queue failure, so no third matrix was started and no gate was relaxed.

All completed samples used source SHA-256 `c4f0cf04d7d559bf85bff30621969a2ab87fb86c76fb33ac8d85b0240a135c0d`. The failed samples also produced content and mount records, but they remain invalid because the network gate failed. The 1 GiB matrix has no valid five-pair result, no CV result, and no annual-baseline decision.

## 512 MiB exploratory row

Evidence is under `/home/Su/experiments/CPNetFlux/2026-11-25-r770-receiver-mmap-profile-01`. The complete-task rates were 28.4449 Gbps for `huge=never` and 36.4024 Gbps for `huge=within_size`. The candidate reduced mapped-write CPU from 316.074 ms to 195.971 ms and copy minor faults from 131,072 to 256 in that single pair. These measurements justify further investigation after the network environment is made acceptable, but they do not establish a stable optimization.

## Code and build state

- `47ea39c` added the bounded 512 MiB receiver profile candidate.
- `40e92357` added the fixed 1 GiB private THP matrix driver.
- `fe4182e` fixed the runner profile whitelist for the two 1 GiB profiles.
- The branch `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01` is pushed at `fe4182e20d000255577f0391acb390425bb8f776`, and `git ls-remote` returns the same SHA.
- R1 and R2 were built from `fe4182e`. Runtime SHA-256 values matched on both nodes for the server, tree download client, tree upload client, and private tmpfs launcher.
- R1 Python private-tmpfs tests passed 28/28. The selected private tmpfs CTest gates passed. The two protected historical untracked task documents remain untouched.

## TLS compatibility status

TLS compatibility is a separate unfinished task. General control/data TLS code paths exist, but the V3 large DATA validation rejects the annual comfort profile when control TLS or data TLS is enabled. The 256 KiB DATA limit, 1 MiB TCP receive-buffer setup, exact-read mode, and application/kernel pacing also require plain TCP in the current implementation. Therefore token plaintext can use the comfort profile, while control-TLS or control-plus-data-TLS arms cannot currently use the same profile without a targeted compatibility design and tests. No TLS relaxation or data-path change was made in this delivery.

## Handoff

Do not use either 1 GiB matrix directory as a performance result. Do not run the annual matrix from this rejected THP candidate. The next decision is whether to resolve the R770 hardware receive-queue gate under a separately authorized network investigation, or to continue annual testing with the existing buffered mmap baseline. TLS compatibility must be evaluated separately and must preserve the fixed workload before it can enter the annual matrix.
