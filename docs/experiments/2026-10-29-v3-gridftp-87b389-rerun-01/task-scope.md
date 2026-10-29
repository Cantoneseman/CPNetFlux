# V3-GRIDFTP-87B389-RERUN-01

Date: 2026-10-29

## Scope

Deploy the exact CPNetFlux implementation commit
`87b389fc34615d1c43ccb36b5064beda2f47d1f6` into task-isolated prefixes on
R730 (sender) and R750 (receiver), then repeat the optimized uncompressed V3
versus GridFTP physical-pair experiment from 2026-10-24. The authoritative
source and report/evidence commit remain in Shenzhen `/root/projects/CPNetFlux`.

Use the same deterministic 10.12890625 GiB input and four workloads: dense
2048 x 64 KiB (128 MiB), random 4 GiB, repeated 4 GiB, and mixed 2 GiB plus
512 x 8 KiB and one empty file (514 files total). Re-screen V3 at 4/8/10
lanes and GridFTP at 4/8/10/16 lanes on dense and random, then run three
formal repetitions per system and dataset. Keep V3 TLS, CRC32C, dynamic file
scheduling, shared leases, range transfer, worker control reuse, tree data
reuse, and compression off. GridFTP uses GSI/private data protection.

## Non-goals

- No runtime source changes, system-wide package/service replacement, or
  persistent firewall, routing, NIC, or GridFTP policy changes.
- No compression, NUMA pinning, native RDMA verbs, or cross-domain claim.
- Do not count authentication, storage, integrity, or cleanup failures as
  throughput samples.
- Do not add the external SHA-256 verification pass to measured transfer time.

## Acceptance

1. Verify source commit and Release/TLS build provenance; record exact binary
   SHA-256 on Shenzhen, R730, and R750.
2. Refresh a 12-hour GSI proxy; check remaining validity and complete a GSI
   private-data smoke transfer before formal GridFTP measurements.
3. Record environment, NUMA topology without pinning, link, GridFTP versions,
   phase timing, process CPU/RSS, disk and NIC counters.
4. Pass 14/14 tuning samples and 36/36 formal samples; each formal destination
   matches expected path/count/bytes and independent sender/receiver SHA-256.
5. Stop only this task's services, remove only its payloads and temporary
   credentials, and verify task ports/processes are closed.
6. Commit explicit task/result/compact-evidence files on the existing verified
   branch, push to `origin`, and read back the remote SHA. Copy the report and
   evidence additively into `D:\\读研\\干活\\算力网项目——存储优化\\CPNetFlux\\实验报告`.

## Allowed paths and rollback

- Repository: this task file, its matching result file, and compact evidence
  under `docs/experiments/2026-10-29-v3-gridftp-87b389-rerun-01/`.
- Runtime artifacts on Shenzhen, R730, and R750: only
  `/tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01/`.
- Windows: temporary transport staging and the new dated report subdirectory
  below the requested report destination; no development in the Windows copy.
- Rollback: stop this task's recorded PIDs, remove its temporary credentials
  and payloads, and retain the evidence and binaries. No system installation
  or service is replaced, so rollback does not require system restoration.

The primary comparison preserves TLS/private protection from the previous
run. The implementation's plain-socket sendmsg optimization is not exercised
by TLS DATA frames; this experiment must not claim a sendmsg-specific gain.
