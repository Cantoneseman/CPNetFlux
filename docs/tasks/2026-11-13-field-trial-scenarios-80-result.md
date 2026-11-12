# Stage80: new-runtime GridFTP comparison and acceptance stop

Status: A/B/reverse PASS; TLS attempted and failed; C/faults NOT_RUN; overall NOT_READY.

Delivery commit: `5e68e1c163e22d7dfb20241e3069d8993cf199ad`; runtime `dbb2d6bac86b29914dbea655b29660937267e38f`. Four binaries matched on both hosts, same build configuration. Stage78 full CTest on each host: 825 passed, 3 environment-specific native-RDMA skips, zero failures.

## Accepted measurements

1 GiB whole-task alternating five pairs: CPNetFlux median **30.8034 Gbps**, GridFTP **12.0953 Gbps**; sample CV **4.4938% / 4.2739%**. All ten accepted. This is a fixed tested configuration, not a universal product speedup or encrypted/durable storage result. CP uses private hugewithin_size tmpfs/mmap, CRC32C verified_chunks, 4 TCP lanes, 256 KiB DATA, application pacer 1.5e9 B/s/lane, TLS off and commit-sync none. GridFTP uses GSI-authenticated clear data, ordinary tmpfs, -fast -p4 -cc4 -tcp-bs1048576 -bs262144 -r -cd. Authentication, checksumming, memory backend and commit semantics differ. Required target setup and live source-plan validation are included in task time; external SHA and observers are separate.

| Scenario | CP median Gbps | GridFTP median Gbps | Repetitions each |
|---|---:|---:|---:|
| dense | 2.623660 | 1.122657 | 3 |
| tiny | 0.094756 | 0.122180 | 3 |
| mixed | 9.840808 | 3.048738 | 3 |
| reverse | 19.741570 | 12.415901 | 3 |

All accepted scenario/reverse tasks have full file SHA and directory-set equality, including empty directories and zero-byte files. Tiny files remain slower than GridFTP. Reverse throughput is asymmetric and is not the A private-target profile. No native-verbs result is claimed.

## Stop and causal follow-up

8 MiB verified-TLS download failed before directory metadata or payload, with unexpected peer close and zero transferred bytes. No retry was run in the failed window; subsequent C and cross-host fault groups were skipped. Both rings restored RX/TX=1024/1024. Original 2121 processes, routes, RSS/IRQ settings preserved. Host pswpout delta zero, owned swap zero; no network-error deltas in accepted groups. Automatic cleanup initially rejected ordinary root tree-manifest metadata; evidence was retained and cleanup completed manually. Both postflights clean; private credentials removed.

Root then reproduced in separate loopback diagnostic with unchanged runtime: TLS+pacer-on fails with zero bytes; pacer-off succeeds for 8 MiB plus empty directories/zero file, full SHA/tree equality, control_tls_verified=true and data_tls_verified=true. Source guard in persistent_tree_transfer.cpp rejects this plain-TCP-only pacing setting under TLS. This confirms an experiment-profile error; Stage80 failure remains preserved. Follow-up81 corrects only harness profile/cleanup, adds offline rejection, and must finish S/C/F before readiness.

## Evidence and backup

Remote: `/home/Su/experiments/CPNetFlux/2026-11-13-field-trial-scenarios-80`. Archive delivery-final.tar.gz: 35,941,848 bytes; SHA256 `7bd4c6da70bd13030de2468c4eeba525490c70a9c2ea6306b06c2fab111f54c2`. SHA256SUMS contains 938 individually verified files. Windows archive and extracted evidence: `D:/Project/CPNetFlux/_codex_field_trial_scenarios_80`; all 938 hashes checked, no failures. No large evidence/payload/private credential is committed to Git.
