# V3-GRIDFTP-OPTIMIZED-RERUN-01

Status: measurements, analysis and cleanup complete; see the result document.

## Scope

Re-run the 2026-10-22 uncompressed CPNetFlux V3 versus Globus GridFTP comparison
on the R730 sender and R750 receiver, using input commit
`aa20229e0844aa2a5ec80c5b372d5448931b856d`. The source commit includes the
FD-lifetime fix, tree-manifest snapshot coalescing, CRC32C summary reuse, and
bounded range-checkpoint batching; serial range writes remain enabled.

Use the same deterministic 10.12890625 GiB input: dense 128 MiB/2048 files,
random 4 GiB, repeated 4 GiB, and mixed 2 GiB/514 files. Re-screen the previous
concurrency choices (V3 4/8/10; GridFTP 4/8/10/16) on dense and random data,
then run the selected profile for three repetitions per dataset. Capture the
stage-observation V3 group, GSI/private GridFTP group, and paired V3 group with
JSON summary disabled. Hash each received tree with SHA-256 and retain timing,
CPU, memory, disk, NIC, socket, and authentication evidence.

## Non-goals

- Do not change CPNetFlux runtime, agent, desktop, or system installation.
- Do not change host networking, firewall, routing, or persistent GridFTP policy.
- Do not enable compression, native RDMA verbs, or cross-domain claims.
- Do not treat authentication, storage, or integrity failures as throughput data.
- Do not add concurrency stage counters as if they were wall-clock time.

## Acceptance

1. Build Release/TLS binaries from the exact input commit; record SHA-256 and
   confirm identical binaries on both servers.
2. Confirm GSI user proxy validity for the full run and GridFTP GSI/private
   smoke success before measurements.
3. Record the selected concurrency profile and confirm V3 summary reports V3,
   persistent-tree/dynamic scheduling, and compression off.
4. Complete three valid repetitions per system and dataset, with identical
   file counts, byte counts, and sender/receiver SHA-256 tree hashes.
5. Preserve sanitized logs and machine/network evidence; close only this task's
   ports and remove only this task's payloads and temporary credentials.
6. Commit the task/result/evidence metadata on this task branch, push to the
   verified CPNetFlux GitHub remote, and read back the pushed SHA.

## Allowed paths and rollback

Git changes are limited to this task plan, result, and its compact experiment
evidence. Runtime files, payloads, logs, TLS/token material, and GSI proxy stay
under this task's dedicated `/tmp/cpnetflux-runs/V3-GRIDFTP-OPTIMIZED-RERUN-01`
directories on Shenzhen, R730, and R750. Rollback stops only processes and
listeners created by this task and removes only its dedicated run directories;
it does not touch existing agent/desktop services, other experiments, or dirty
files. The completed result and compact evidence use the same task ID.
