# V3 Lane and Storage Service Rate 06

Date: 2026-11-08
Input: 76f7572d1a0fdec462f2791484ea8b9c13fb491a
Status: in progress; single writer on R770-1.

## Scope

Execute the next scope from Full File Path 05. First complete five interleaved
queued/inline pairs at each of 1, 2 and 4 TCP lanes, using identical committed
V3 code and an 8 GiB resident tmpfs source. Then add a bounded diagnostic using
the existing PosixFile::writeAtAll implementation to isolate shared-inode,
disjoint-offset writes from independent-file writes. Preserve evidence from
both hosts and compare service rates rather than guessing from queue waits.

## Non-goals

No RDMA, wire format, CRC/lease/resume/publication changes, additional runtime
workers/queues, kernel/network/affinity changes, production service restarts or
64 GiB payloads. Keep queued as the default and inline explicit. Do not design
automatic selection from incomplete or unstable cells. Do not reopen the closed
pipeline-concurrency or manifest-coalescing candidates. These diagnostics do not
establish durable disk/TLS production readiness or 100 Gbps readiness.

## Affected Files

This task; docs/ROADMAP.md; CMakeLists.txt; a new tools/perf/offset-write benchmark;
its focused Python smoke under tools/test; and a reproducible bounded runner.
V3 runtime source is not changed in this attribution scope. The lane driver uses
the preserved read-only full-path harness under /home/Su/experiments.

## Acceptance

1. Lane matrix: five pairs per 1/2/4 lanes, 30 complete tasks. Rotate lane order
   and alternate queued/inline order. Fix commit, eight binary SHA-256 values and
   CMake configuration on both hosts before every transfer. Record exact actual
   execution mode, task timing, both-end CPU/GiB and client context switches.
2. Existing V3 gates remain mandatory: 8 GiB exact bytes, independent SHA-256,
   DATA CRC and terminal results, resident source before/after, TCP local bind
   192.168.100.1/2, eno17095np0 route/interface byte coverage, MemAvailable >=12
   GiB, owned swap zero and per-host pswpout delta zero. One output per host.
3. Report each cell's median, range, population CV and all five paired gains.
   Stable cells require CV <=5%; promotion needs >=10% median gain, no correctness
   or resource regression and a supported workload boundary. Mixed results stay
   explicit. Historical four-lane data and single-lane screening are not pooled.
4. New IO diagnostic: same total bytes/pattern across shared and independent
   layouts; start workers together; per-worker buffers <=64 KiB; validate file
   bytes outside the timed write interval; prevent overwrite and clean only owned
   files. Cover 1/2/4 workers, uneven sizes, >1 chunk, existing paths and invalid
   arguments. Include fresh vs already populated rewrite if needed to separate
   allocation from steady writes; keep those timing boundaries explicit.
5. Build and focused CTest on R1, commit/push/readback, then sync fixed commit to
   R2 and build/test/hash before storage measurements. Use five interleaved
   samples per chosen storage cell, <=8 GiB total files and the same memory gates.
6. Clean the source and every output after verification; retain only compact
   evidence. Record failures without replacing them with successful retries.

## Evidence and Handoff

/home/Su/experiments/CPNetFlux/2026-11-08-v3-lane-storage-06

Lane workload: CRC32C, verified_chunks, direct POSIX IO, checkpoint batch 16,
pipeline depth 2, stage read/write concurrency 0, compression off, commit_sync=none,
TLS off. V3 validation semantics remain present; storage-only diagnostics exclude
V3, network, checkpoints, work leases and durability. Keep these claims separate.
Preserve the two unrelated 2026-10-14 untracked task files and both resident
production services. Raw payload and experiment evidence are not Git payload.

## Lane Matrix Results

Input 76f7572d; 30/30 complete tasks passed all original acceptance checks and
the additional actual-execution/interface-byte gates. New evidence is not pooled
with scope 05. Five samples per cell; population CV:

| Lanes | Queued median Gbps | Queued CV | Inline median Gbps | Inline CV |
| --- | ---: | ---: | ---: | ---: |
| 1 | 22.7637 | 4.2370% | 15.7361 | 3.4034% |
| 2 | 18.1279 | 5.2940% | 23.3778 | 5.1788% |
| 4 | 15.5370 | 1.8854% | 24.1385 | 0.9125% |

All five single-lane inline pairs regress (24.42-34.94%); all five two/four-lane
pairs improve. Both two-lane cells fail the 5% stability gate, so no automatic
selection is implemented. Four-lane inline has stable benefit, but is only 6.04%
above the queued single-lane median, far from 30-50 Gbps. Reducing receiver
handoff helped a particular concurrency level; full task scaling remains limited.

Both-end median CPU sec/GiB, queued/inline: 1 lane 1.16875/1.25875;
2 lanes 2.68375/1.59250; 4 lanes 4.07000/2.44375. Min MemAvailable R1/R2:
20.5495/20.0311 GiB; max polled owned RSS 37.86/21.02 MiB excluding tmpfs pages.
Owned swap and host pswpout deltas were zero. The independently rehashed 8 GiB
source and every output were removed before building the storage diagnostic.
Statistics and per-sample receipts: lane-analysis.json, metrics/, acceptance/.

## Storage Diagnostic Implementation

New Linux-only target cpnetflux-offset-write-bench uses PosixFile::writeAtAll
with one shared fd/inode or one independent file per worker. Worker start is
coordinated, stats are on separate cache lines and reusable buffers are bounded.
The same 65532-byte DATA-like writes stop at 1 MiB chunk boundaries; 8 GiB yields
139264 logical writeAtAll calls in either layout. These count wrapper calls,
not kernel syscalls when a short write retries. Rewrite mode sequentially fills
all pages with zeros before timing; timed writes carry nonzero lane/frame patterns.
Verification compares every byte and exact file size outside timing. Fresh mode
does no prefill/preallocation. There is no CRC, network, V3 or durability work.

The tool refuses existing roots, creates files exclusively, removes only its
known files and never recursively removes a directory containing unexpected data.
--keep-files is explicit for inspection; the bounded memory runner never uses it.
Thread-construction/IO failures release waiting workers before unwinding.

Actual files: CMakeLists.txt; tools/perf/offset_write_bench.cpp;
tools/perf/run_offset_write_memory.py;
tools/test/run_cpnetflux_offset_write_bench_smoke.py; this task; docs/ROADMAP.md.

R1 build passed. Focused CTest filter cpnetflux_offset_write_bench_smoke|PosixFileTest:
7/7 passed. New smoke independently reconstructs and reads all file bytes in 24
kept-file shapes plus 24 cleanup shapes (1/2/4 workers, shared/independent,
fresh/rewrite, tiny/uneven multi-chunk). Real RLIMIT_FSIZE short-write/EFBIG at
1/4 workers fails and cleans safely; existing paths/symlinks and invalid arguments
are preserved/rejected; runner cleanup refuses unrelated entries and symlink roots.
The first fixed-commit storage cohort completed 60 samples/host on e0b42f8;
final statistics and interpretation follow after initial-state matching below.

## Initial File State Correction

The first storage cohort uses committed e0b42f8 on both hosts: fresh (zero
length) and rewrite (populated pages). During code review, FileTransaction::begin
in src/core/io/persistent_tree_transfer.cpp was confirmed to resize every V3
temporary file to totalSize before writing, even when preallocate=off. Therefore
neither boundary exactly matches V3. Preserve this cohort as boundary evidence;
do not infer a V3 ceiling or a benefit from untimed prefill.

Extend only the diagnostic with sparse mode: resize to final length, no prefill.
Record total initial logical size and allocated blocks before timing in every
mode; tests must distinguish zero-length, sparse and populated states. Add an
explicit runner mode filter and run five shared/independent pairs at 1/2/4
workers on each host (30 samples/host), with the same 8 GiB and resource gates.
Use a new fixed commit and evidence storage-sparse/; do not pool different
commits as paired candidates. The runtime and production defaults stay unchanged.
Affected files are the existing diagnostic, runner, smoke and this task.

R1 sparse extension: build passed; focused CTest 7/7 passed. Smoke now covers
72 success invocations including independent byte checks and initial fstat
size/allocation assertions across all three modes, plus prior failure/cleanup
regressions. Commit and peer acceptance precede the sparse-only experiment.
