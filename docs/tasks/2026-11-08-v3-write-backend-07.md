# V3 File Write Backend Feasibility 07

Date: 2026-11-08
Input: 3120bb4abdce9fe7b75be475f46bfd0e4d5beebc
Status: complete; four-worker mapped diagnostic passed; V3 integration pending.

## Scope and Hypotheses

Continue the accepted lane/storage-06 handoff. Shared sparse-file pwrite loses
about 25% from one to four workers; independent files reach about 87 Gbps on
both hosts. Distinguish shared descriptor from shared inode ownership using
independently opened writer descriptors on the same inode. Then evaluate one
bounded MAP_SHARED write diagnostic against identical sparse-file pwrite.

Mappings are lazy, writable and at most 64 MiB per worker, reused for frames in
that window; no prefault or prefill. The write interval includes all mmap,
first-touch faults, memcpy and munmap operations. Retain whole-process time
including creation/resize, worker startup, verification and cleanup separately.
This is feasibility evidence; no V3 backend is promoted in this scope.

## Non-goals

No V3 runtime, wire, CRC, lease, resume, checkpoint, publication, default mode,
RDMA, kernel/network/NUMA/affinity or production-service changes. No additional
pipeline queues/workers, reopened serialization/preallocation candidates, large
disk payloads, 64 GiB files or 100 Gbps readiness claims. No parallel writers,
subagents or worktrees. Preserve unrelated files and CPSS(DCC).

## Affected Files

This task, docs/ROADMAP.md, CMakeLists.txt, tools/perf/offset_write_bench.cpp,
tools/perf/run_offset_write_memory.py, the existing offset smoke and a new backend
smoke plus a Linux test-only mmap fault shim. Compact analysis/receipts remain
under /home/Su/experiments/CPNetFlux/2026-11-08-v3-write-backend-07.

## Acceptance

1. Behavioral contract tests fail on the old binary before new implementation:
   shared-open must retain one inode with distinct writer descriptors; mapped
   writes must produce the exact lane/frame oracle including uneven sizes and
   64 MiB window transitions. Existing pwrite behavior stays covered.
2. Test mmap failure before touch and real SIGBUS after truncating the mapped
   backing file. Disable core dumps in the diagnostic. A signal exits the isolated
   child, cannot emit PASS and is recorded as failure; the supervising runner
   waits for process exit before deleting only its known files. Do not install
   a process-wide signal recovery handler in the V3 runtime.
3. Keep <=8 GiB total tmpfs files, frame buffers <=64 KiB per worker, mappings
   <=64 MiB per worker, measured MemAvailable >=12 GiB, owned swap zero and host
   pswpout delta zero. Maximum four workers in experiments. RSS must stay <=1 GiB;
   mapped RSS refers to tmpfs pages too and is not additional independent payload.
4. R1 CMake build/related CTest, explicit commit/push/remote readback, R2 fixed
   commit build/test and matching binary/configuration before measurements.
   Hosts execute serially; no dirty or mixed-version transfer experiments.
5. Five interleaved samples/cell: descriptor control shared/shared-open/independent
   pwrite at 1/2/4 workers, then same-file pwrite/mapped at 1/2/4 workers on both
   hosts. All start sparse: final logical size 8 GiB, initially allocated blocks 0.
   The pwrite/shared cells can serve both comparisons on the same fixed commit;
   rotate ordering and never pool prior scope samples. 120 total samples planned.
6. Report median/range/population CV, paired gains, timed CPU/GiB and system CPU.
   Feasibility needs >=10% median gain, CV <=5%, no integrity/resource regression;
   retain all failed/unstable results. At most two candidates, one planned here.
   A diagnostic win permits a separate V3 integration scope only; full task A/B,
   CRC/resume/lease/cancellation/publication acceptance remain mandatory there.
7. Clean owned files after every sample, compact receipts only, verify final
   payload roots absent and production PIDs preserved. Record exact inputs,
   commands, hashes, failure reasons, output evidence and next action.

## Tool and Ownership Check

Shell/SSH, apply_patch and dedicated chat tools are exposed. Local Codex CLI
exists; R1 command-v did not find a Codex CLI. No dispatch or CLI continuation
is invoked. User-authorized single-writer work remains on R1; R2 only receives
fixed commits for build/test/measurement. R1 has only the two unrelated untracked
2026-10-14 worker-state task documents; R2 is clean. Both start at input SHA.
Source of historical-readiness boundaries: docs/RESEARCH_BASELINE.md.

## Implementation and R1 Acceptance

Shared-open independently calls openReadWrite for each writer on one exclusively
created shared.bin. fstat records actual distinct writer-fd and (device,inode)
counts. Mapped mode is explicit --backend=mmap, accepts only shared sized files,
and reuses lazy 64 MiB windows. Every worker closes its final window before its
finish timestamp. Payload-write calls, pwrite wrapper calls and mapped copies
are separately counted, along with mapping/unmapping and system CPU.

The new tests failed on the old binary (red-layout-only.log: invalid layout;
red-mmap.log: unsupported arguments). An additional valid chunk size 100003
crossing a mapping window first failed with frame-crosses-window; the helper now
splits only the memory copy, preserving the logical payload call and oracle.
red-window-crossing.log retains this failure.

R1 all-target build and focused CTest passed 8/8. Original smoke has 72 successful
invocations; backend smoke has 30, including independently read file bytes,
uneven partitions and 64 MiB window transitions. A test-only LD_PRELOAD shim
targets only the exact test file: mmap ENOMEM exits 2, truncate-after-mmap causes
real SIGBUS (exit -7); both return no success JSON and runner cleanup removes
only the terminated child's files. Production code has no fault-injection hook.
ctest-R770-1.log records the actual fault receipts. No V3 runtime binary changed.

## Fixed-Commit Peer Acceptance

Implementation: ae3f0a94d9404ff02ec3bcd56cb21140656f51e2, pushed to the verified
CPNetFlux GitHub origin with matching remote SHA readback. R2 received only this
commit, built all targets and passed focused CTest 8/8. Both hosts used
RelWithDebInfo, tests ON, desktop/cross-domain tests OFF, ISAL/io_uring/TLS/RDMA ON.
The diagnostic itself performs no transport. SHA-256 on both hosts:

- cpnetflux-offset-write-bench: ffe298ea55e1f915d2d71f1c01dc4aeb1865007d66dafde1846c1c734314d465
- cpnetflux-tree-download-client: 4113437be9d4348c04dc9d97565ec0a307380a31fc9d1ea24c95707a513cce95
- cpnetflux-gridftp-server: 7d234ec315e1ecff80da74b841a5e6da3455dfcdc143f2ff71fb0a29b74f142a

The two runtime hashes stayed equal to scope 06. Each host ran the 72 original
plus 30 backend success invocations and the two expected fault cases. This is
focused acceptance, not a new full-suite run or production-readiness claim.

## Descriptor and Mapping Results

R1 then R2 ran serially. 60 samples per host, five/cell, each with 8 GiB total
sparse files, 65532-byte payload pieces and 1 MiB chunk boundaries. Every sample
started with logical size 8589934592, allocated blocks zero and no prefill.
All 120 formal samples passed exact bytes, full content comparison, descriptor/
inode checks, memory, swap and cleanup gates. No failed sample was replaced.

Median local write Gbps:

| Host | Workers | Shared fd / pwrite | Independent opens, same inode / pwrite | Independent files / pwrite | Shared file / mapped |
| --- | ---: | ---: | ---: | ---: | ---: |
| R770-1 | 1 | 41.343 | 41.071 | 41.593 | 21.015 |
| R770-1 | 2 | 31.227 | 31.392 | 63.715 | 33.344 |
| R770-1 | 4 | 30.848 | 31.097 | 86.909 | 54.896 |
| R770-2 | 1 | 41.284 | 41.150 | 41.256 | 20.804 |
| R770-2 | 2 | 30.997 | 31.218 | 63.555 | 33.513 |
| R770-2 | 4 | 31.110 | 31.029 | 87.296 | 55.348 |

All 24 rate cells pass population CV <=5%. Independent opens record four actual
writer descriptors and one (device,inode) at four workers. They do not remove
the scaling loss: +0.81% / -0.26% versus shared fd on R1/R2. Independent files
remain high-rate controls, not a publishable single-file backend.

Mapped versus shared-file pwrite, paired only within this commit and host:

| Host | Workers | Mapped CV | Median rate gain | Diagnostic decision |
| --- | ---: | ---: | ---: | --- |
| R770-1 | 1 | 0.348% | -49.17% | no-go |
| R770-1 | 2 | 0.622% | +6.78% | no-go: below 10% |
| R770-1 | 4 | 0.913% | +77.96% | eligible for separate V3 integration |
| R770-2 | 1 | 0.558% | -49.61% | no-go |
| R770-2 | 2 | 0.669% | +8.12% | no-go: below 10% |
| R770-2 | 4 | 1.204% | +77.91% | eligible for separate V3 integration |

Four-worker mapped ranges are 54.018-55.382 Gbps on R1 and 53.768-55.590 on R2;
all five paired gains are positive on each host. Timed CPU sec/GiB decreases
1.1108 -> 0.6237 and 1.1031 -> 0.6177, about 44%. System CPU/GiB decreases
1.1086 -> 0.5899 and 1.1016 -> 0.5841. Full per-cell ranges/CVs, every paired
gain, CPU cost and whole-process time including verification are in
analysis-R770-1-R770-2.json. Mapped rate includes its mmap, first-touch and munmap;
verification is outside this rate and inside the separately reported process wall.

Minimum MemAvailable: R1 20.2997 GiB, R2 20.0599 GiB. Maximum polled process RSS:
248.8203 / 240.0195 MiB. Mapped RSS represents the same backing tmpfs pages and
must not be counted as an independent second 8 GiB payload. At most four lazy
64 MiB windows plus <=262128 bytes worker buffers were live; the file itself
consumes up to 8 GiB tmpfs. All owned swap and per-sample host pswpout deltas
were zero. Both final /dev/shm usage values were zero and owned temporary roots
were absent. Production PIDs 126766/164834 kept their original start times.

## Decision and Integration Boundary

The descriptor hypothesis is closed: independent opens are not an effective
optimization for this workload. The four-worker mapped candidate passes local
feasibility; one/two-worker promotion is rejected. No second candidate was added,
no runtime/default was changed and no new complete V3 transfer was measured.
The latest complete-file median therefore remains scope 06's 24.139 Gbps in its
explicit four-lane inline configuration. 30-50 Gbps full-task, durable/TLS-on,
reverse/mixed/long-run and native RDMA acceptance remain open.

Next scope must first make the failure/lifetime contract concrete:

1. Restrict the initial experimental integration to a separately launched
   single-task download CLI with POSIX direct writes and explicit inline/four
   lanes. Keep the server write path and all defaults unchanged. Reject
   unsupported combinations explicitly; no silent fallback or auto-selection.
2. Give each range attempt its own bounded window, tied to RangeFileCoordinator
   Slot identity and its existing writing pin. Avoid a global per-file window
   cache or thread-local fd cache. Retire windows on reclaim/abort/finalize before
   fd closure/publication, preserving offset/coverage and stale-attempt checks.
3. Define failure reporting before enabling mapped writes: mmap failures return
   Status; mapped-store SIGBUS currently terminates the isolated CLI. Do not
   claim this is graceful recovery, install a broad signal handler or put this
   fatal behavior into the shared server. Test that peer EOF terminates the task,
   no success/publication occurs, and prior checkpoints recover correctly.
   If this failure contract cannot be satisfied, stop the integration candidate.
4. Add behavioral tests before implementation for mapping ownership, failed
   chunk/checksum, partial work, cancellation, stale attempts, interrupted resume,
   checkpoint and publication. Account mappings in resource observations.
5. Only then repeat five interleaved 8 GiB full-task baseline/candidate pairs on
   one fixed commit, same actual inline execution and original TCP bind/NIC,
   CRC/SHA/terminal/resource/cleanup gates. Require >=10% median task-rate gain
   and CV <=5%; local 55 Gbps alone does not meet the V3 performance gate.

This handoff proposes the next scope; none of its V3 implementation is claimed
complete. Continue the agreed bounded development; no pipeline/RDMA expansion.

## Reproduction and Evidence

From each project root at implementation input, hosts serially:

```sh
cmake --build build -j 4
ctest --test-dir build --output-on-failure -R 'cpnetflux_offset_write_.*smoke|PosixFileTest'
python3 -u tools/perf/run_offset_write_memory.py --commit ae3f0a94d9404ff02ec3bcd56cb21140656f51e2 --output-root /absolute/new/evidence/host --run-id new-backend07 --modes sparse --layouts shared shared-open independent --backends pwrite mmap
```

Use new evidence roots/run IDs. Fixed-commit and clean tracked-tree guards are
intentional; later documentation HEADs must use the matching source revision
for a fresh run rather than bypassing these guards. No evidence path is reset.

R1 primary evidence root:
/home/Su/experiments/CPNetFlux/2026-11-08-v3-write-backend-07

Contains matrix/R770-1 and matrix/R770-2 (each 60 receipts plus run.json),
analysis-R770-1-R770-2.json, ctest-R770-1/2.log and the retained red logs. R2
preserves its original compact receipts; copies on R1 are audited together.
No payload, binaries or test library is submitted to Git.

Recompute without payload: `python3 analyze_backend_07.py R770-1 R770-2` from
the evidence root. Analysis SHA-256:
42573bb3284584c8853ff7fd0ee86bad2a31ae5c9a3a21da14ed8e0388a233b7.
Tracked runner SHA-256:
d2ef8c902d9df0875e4feeddade27fa975fa847e266de4f3e69deb8d7f0d373e.
Tracked diagnostic source SHA-256:
0dadd7c3423163c8df392124049cd5428dde93a6ded3dfbe063924f3403c6176.
