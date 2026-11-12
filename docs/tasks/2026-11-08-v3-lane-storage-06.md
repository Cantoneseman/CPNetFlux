# V3 Lane and Storage Service Rate 06

Date: 2026-11-08
Input: 76f7572d1a0fdec462f2791484ea8b9c13fb491a
Status: complete; attribution gates passed, full-file throughput target open.

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

## Storage Results

Both hosts built and passed focused CTest 7/7 on each fixed input. Source and
peer hashes matched before measurement:

| Cohort | Commit | Benchmark SHA-256 | Samples |
| --- | --- | --- | ---: |
| fresh/rewrite | e0b42f865862091f49b29ab2cfb1999daa5d5b53 | da57de7f8eb4f1b474d115799ae189a1c47fe45a056ee18b73c375068a81434f | 120 |
| sparse | 2081a90721280c7fe71d89b3fb610463edbe722e | 5b93c57a97776654214a0471ce0d2a1e209987b94d7b92689cb918a9922fe1f8 | 60 |

Sparse mode is the matched V3 initial state: initial_file_bytes=8589934592,
initial_allocated_bytes=0 and prefill_bytes=0 in every sample. All bytes are
written and independently compared by the diagnostic outside the timed interval.
Five samples per cell; each value is median Gbps / population CV percent:

| Host | Workers | Shared sparse | Independent sparse |
| --- | ---: | ---: | ---: |
| R770-1 | 1 | 41.531 / 0.230 | 41.360 / 0.443 |
| R770-1 | 2 | 31.651 / 1.058 | 63.769 / 0.254 |
| R770-1 | 4 | 31.039 / 0.164 | 87.008 / 0.115 |
| R770-2 | 1 | 41.198 / 0.698 | 41.176 / 0.384 |
| R770-2 | 2 | 31.013 / 3.476 | 63.653 / 0.390 |
| R770-2 | 4 | 30.877 / 0.586 | 87.393 / 0.070 |

All 12 matched-state cells pass CV <=5%. Four-worker shared writes lose about
25% versus one worker, while independent files scale to about 87 Gbps. R1/R2
four-worker timed CPU sec/GiB are 1.1037/1.1116 shared versus 0.3940/0.3924
independent. This reproduces write scaling loss without V3 queues, CRC, leases,
manifest or network and supports prioritizing the shared-file write path. The
current two-layout comparison changes both fd and inode ownership; it does not
independently identify the exact kernel lock. Historical audit-01 perf evidence
found shmem_file_write_iter rwsem/osq costs, on its own recorded input commit.

Boundary cohort, also five samples/cell (not paired across commits):

| Host | Initial state | Shared w1/w2/w4 median Gbps | Independent w1/w2/w4 median Gbps |
| --- | --- | --- | --- |
| R770-1 | zero-length fresh | 41.075 / 30.766 / 30.723 | 41.103 / 63.350 / 87.004 |
| R770-2 | zero-length fresh | 40.836 / 30.876 / 30.651 | 40.861 / 63.399 / 87.963 |
| R770-1 | populated rewrite | 58.867 / 58.309 / 56.896 | 58.392 / 78.640 / 99.802 |
| R770-2 | populated rewrite | 59.106 / 58.653 / 57.445 | 59.280 / 79.049 / 100.463 |

23/24 boundary cells pass CV. R1 fresh/shared/w4 has CV 6.141%, retained as
unstable; no replacement run was made. Rewrite excludes sequential zero-fill
from the timed interval. Its approximately 100 Gbps local write rate is not
network throughput, an end-to-end improvement, an argument for free prefill or
new evidence reopening the closed preallocation/concurrency candidates.

## Resource and Cleanup Acceptance

All 180 storage samples passed exact bytes, content, initial-state assertions
where available, resource and owned-file cleanup checks. Minimum MemAvailable
was 20.0263 GiB; maximum polled owned RSS 4.8711 MiB excludes the separate 8 GiB
tmpfs files. All owned VmSwap and host pswpout deltas were zero. Hosts ran
serially, <=8 GiB payload per host, with per-case cleanup. Both final /dev/shm
usage values were zero; all three owned temporary roots were absent and no
benchmark process remained. Production PIDs 126766/164834 and unrelated files
were preserved. No production service was restarted.

Runtime binary SHA-256 stayed unchanged from the lane matrix on both hosts:
tree-download-client 4113437be9d4348c04dc9d97565ec0a307380a31fc9d1ea24c95707a513cce95;
gridftp-server 7d234ec315e1ecff80da74b841a5e6da3455dfcdc143f2ff71fb0a29b74f142a.
Build: RelWithDebInfo; ISAL, TLS, RDMA, io_uring enabled. Measurements selected
TCP, not native verbs; runtime data TLS off and commit_sync=none remain the
scope boundary. These measurements do not validate durable or secure deployment.

## Decision and Next Development

Attribution scope is complete, not the 30-50 Gbps full-file target. Four-lane
inline improves this matrix's queued median by 55.36% and cuts both-end CPU/GiB
39.96%, but only exceeds one-lane queued by 6.04%. Single-lane inline median
regresses 30.87%; two-lane cells remain unstable. Keep queued default, inline
explicit, and no automatic lane/mode selection. Stop expanding the completed
three-stage architecture as previously agreed.

Next independent scope is file-write-backend feasibility, before V3 integration:

1. Add an independently opened fd per worker to the same sparse inode as a
   control, distinguishing descriptor ownership from file layout. Keep the
   current shared and independent layouts as fixed controls.
2. Evaluate one same-file alternative, initially a bounded mapped-write
   diagnostic, only after specifying mapping lifetime, total memory accounting,
   ENOSPC/SIGBUS handling and cleanup. Include mapping/first-touch/unmapping in
   the reported whole operation; no untimed prefill benefit. This is proposed,
   not implemented or accepted in scope 06.
3. At most two measured candidates; require five interleaved pairs, >=10% median
   service-rate gain, CV <=5%, no correctness/resource regression and reported
   CPU/GiB. A diagnostic win alone does not promote a backend to V3.
4. Only a supported backend gets V3 behavioral tests for CRC failure, partial IO,
   cancellation, stale leases, interrupted resume, checkpoints and publication;
   then a complete 8 GiB V3 A/B with the original task-rate gate. Failed
   feasibility stops this branch; do not fall back to extra queues/threads.

Native RDMA, durable disk throughput, TLS-on performance, reverse-direction and
mixed/long-run acceptance remain separate open work. Small-file improvements
remain valid in their own workload and do not close the large-file throughput gap.

## Reproduction and Evidence Index

Lane input: 76f7572d1a0fdec462f2791484ea8b9c13fb491a. The driver command was
`python3 -u lane_matrix_06.py matrix 76f7572d1a0fdec462f2791484ea8b9c13fb491a`
from the evidence root. Source seeding/residency and cleanup used the same
driver; the source was removed, so reruns must use a new evidence root/run ID.

For either host on the corresponding storage input, from the project root:

```sh
cmake --build build -j 4
ctest --test-dir build --output-on-failure -R 'cpnetflux_offset_write_bench_smoke|PosixFileTest'
# First cohort used the pre-sparse runner on e0b42f8, 60 cases/host:
python3 -u tools/perf/run_offset_write_memory.py --commit e0b42f865862091f49b29ab2cfb1999daa5d5b53 --output-root /absolute/new/evidence/host --run-id new-fresh-rewrite
# Matched-state cohort used 2081a90, 30 cases/host:
python3 -u tools/perf/run_offset_write_memory.py --commit 2081a90721280c7fe71d89b3fb610463edbe722e --output-root /absolute/new/evidence/host --run-id new-sparse --modes sparse
```

The latest runner defaults to all three initial states; use explicit --modes
when reproducing a subset. Fixed-commit guards intentionally reject other HEADs.
Run hosts serially; never overwrite the existing evidence paths. Evidence root
contains lane-analysis.json, metrics/, acceptance/, storage/, storage-sparse/,
storage-analysis-storage-storage-sparse.json and ctest-sparse-R770-1/2.log.
R2 compact receipts are copied to R1; no payloads are versioned. Recompute
without transfers: `python3 analyze_lanes_06.py` and
`python3 analyze_storage_06.py storage storage-sparse` in the evidence root.

External script SHA-256:

- lane_matrix_06.py: b4f3c911ef8085ee55d4c034e1a470e660eb0c931d1878bb293f873fb961d4b3
- analyze_lanes_06.py: 44363b1fbdc80634d3fc2a54ba9aac5cfcd9b2c352d138d8e263664bd2eb9ff1
- analyze_storage_06.py: c7168c43d24358b14c68bd085a59e260c0d7fbdc3a07ca658376122c69459903
