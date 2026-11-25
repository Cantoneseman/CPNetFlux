# R770 Receiver Mmap Profile and Private Tmpfs Candidate 01

Date: 2026-11-25
Input commit: `77c8b781ca9b8b9ca7145944d16560d612a5a90f`
Writer: R770-1. R770-2 receives a fixed committed version for build validation.

## Scope

Profile the current default buffered inline-mmap receiver on one 512 MiB V3 TCP transfer using the established four-lane workload. Record receiver phase CPU, mmap copy CPU and minor/major faults, read/CRC/state/lease/checkpoint costs, range lock wait/hold, process CPU, sampled process faults and context switches, diagnostic syscall counts, network counters, memory, integrity, and cleanup.

Evaluate one candidate supported by prior cold-write evidence. Use a private tmpfs mount with `huge=within_size` for the target, compared with the same isolated mount using `huge=never`. This is not the rejected `MADV_POPULATE_WRITE`, collapse, larger DATA frame, exact-read, or direct-to-mmap candidate. Do not change the shared tmpfs mount or global THP policy.

After the 512 MiB gate passes, run five 1 GiB AB/BA pairs using the same inline buffered-mmap TCP workload. Give each arm a private 2 GiB tmpfs mount and include namespace/mount setup, target-directory preparation, and client completion in the complete-task denominator. Collect per-sample RX missed/OOB counters and host TCP retransmission deltas on both endpoints. Require at most two retransmissions per host per sample, matching the established comparison bound.

Both 512 MiB arms use the same four TCP lanes, inline buffered mmap, 256 KiB DATA, 1 MiB requested receive buffer, 1.5 GB/s per-lane application pacing, CRC32C, 1 MiB chunks, checkpoint interval 16, tmpfs source and destination, compression off, and identical direction, data, bind, and task timing. Include necessary private mount and target preparation in the candidate's complete-task time. Detailed syscall tracing is diagnostic only and is not used to accept throughput.

Security-profile compatibility is a separate deliverable. It may follow the performance candidate review and must not be reported as a throughput optimization.

## Evidence and hypothesis

The existing receive-cost profile measured 512 MiB buffered mmap with 96.90 ms socket-read CPU, 9.80 ms CRC CPU, 1.09 ms state CPU, 2.63 ms lease CPU, 324.88 ms physical-write CPU, and 131,072 minor faults inside mapped copies. Map/unmap cost was small. Direct receive moved page faults into socket reads without reducing total receiver CPU and regressed the 1 GiB five-pair median by 36.69%.

The prior private-tmpfs mechanism test measured a 49.82% reduction in worker CPU for a fresh 1 GiB local mapped write under `huge=within_size`, with minor faults falling from 262,158 to 1,553. A later single 1 GiB V3 admission observed only a partial large-page mapping, triggered network RX/OOB counters, and did not pass its 40 Gbps admission threshold. These results justify one bounded, same-profile comparison but do not establish full-task benefit.

## Non-goals

- No protocol, frame, checksum, chunk, checkpoint, lease, recovery, or completion changes.
- No change to direct-receive default or eligibility.
- No global THP, shared tmpfs, kernel, network, NIC, IRQ, RSS, ring, MTU, or offload changes.
- No RDMA, compression, pwrite, disk persistence, or annual GridFTP comparison.
- No 1 GiB five-pair matrix unless the 512 MiB candidate passes correctness and network gates and shows a repeatable CPU or complete-task improvement.

## Expected files

- `tools/perf/run_private_tmpfs_v3.py`
- `tools/perf/run_private_thp_1g_ab.py`
- `tools/perf/run_large_data_ab.py`
- `tools/perf/private_tmpfs.py`
- `tools/perf/private_tmpfs_launcher.cpp`
- `tools/test/run_private_tmpfs_test.py`
- `tools/test/run_private_setup_smoke.py`
- `tools/perf/run_small_file_baseline.py`
- TLS profile-compatibility code and tests, only if needed after candidate review
- This task and its result document

The final affected-file list may be narrower after implementation review.

## Acceptance

1. The current default buffered mmap profile is collected from a fixed commit and matched R770-1/R770-2 builds. Record phase-level costs, mmap write CPU/faults, lock/lease/checkpoint accounting, process resource use, sampled context switches and faults, existing diagnostic syscall counts, interface/RX/OOB/retransmission counters, SHA-256, verified chunks, and cleanup.
2. Candidate and control each complete one 512 MiB task with identical workload and instrumentation. Both pass protocol terminal, external SHA-256, verified-chunk, memory/swap, network, and cleanup gates.
3. The candidate is considered promising only if receiver CPU falls by at least 10% without a complete-task throughput regression greater than 5%. A pass permits a separate decision on 1 GiB five-pair A/B. A failed network or correctness gate stops the experiment.
4. The 1 GiB matrix requires all ten samples to pass integrity, resource, network, retransmission, actual-page-policy, and cleanup gates. Both arms must have CV <=5%, at least four of five paired candidate wins, and candidate median task-rate improvement >=10% to qualify as an annual-baseline candidate.
5. If neither arm is safely comparable or the candidate misses its CPU/task or matrix gate, record the result as no demonstrated performance improvement. Do not label environment compatibility as a performance optimization.
6. Separately test any TLS compatibility changes against the same DATA size, receive-buffer request, pacing, inline-mmap, and checksum profile. Preserve buffered receive under TLS and prove that the direct path remains inactive.
7. Run related CMake/CTest, build both endpoints from the same pushed commit, compare runtime binary SHA-256, and record remote SHA readback before any transfer.

## Stop conditions

Stop on nonzero receive-network error counters, memory/swap gate failure, integrity failure, unexpected workload differences, mismatched binary hashes, or incomplete cleanup. Do not relax a gate or retry a failed sample to obtain a favorable result.
