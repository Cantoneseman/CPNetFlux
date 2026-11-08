# V3 File Write Backend Feasibility 07

Date: 2026-11-08
Input: 3120bb4abdce9fe7b75be475f46bfd0e4d5beebc
Status: in progress; single writer R770-1.

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
