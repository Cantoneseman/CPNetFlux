# V3 Bounded Mapped Populate 10 Result

Date: 2026-11-08. [Scope](2026-11-08-v3-mapped-populate-10.md).
Input: `87438fdb700265b908d378aa1b89229a8eebbed5`.
Initial diagnostic source: `806a788d58058cad7e0432494dac0ed4db816932`.
Corrected timing source: `e61a5868abd2b89f44263753ebe1fb04e8bf8c5f`.
Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`; version0.1.0.
Both sources committed, pushed to Cantoneseman/CPNetFlux and SHA-read back.

## Decision

**No-go for MADV_POPULATE_WRITE as a V3 mapping candidate.** Both hosts show a
stable approximately25% local write-rate regression. All five paired comparisons
on each host regress, including the repeat after correcting phase CPU observation.
No V3 integration or complete-task candidate test is justified by these results.
The explicit option remains only in the local diagnostic as a measured negative
control; default lazy mapping and all transfer binaries are unchanged.

The full-task memory-workload result therefore remains scope08/09's approximately
28-29 Gbps, below the 30 Gbps midterm floor. Local55 Gbps is not network throughput,
nor is a negative local result a new full-task measurement. Three-stage expansion,
RDMA tuning, TLS/durable/long-workload acceptance and100 Gbps remain open/paused as
previously recorded. Close this page-population hypothesis and next attribute
sender read/CRC/queue/socket CPU independently; do not stack a second population
variant onto the failed candidate.

## Implementation

Diagnostic `cpnetflux-offset-write-bench` accepts `--populate=lazy|write`; lazy is
the default, explicit population options require mmap. Unknown/duplicate policy
and pwrite+population are rejected before file creation. Each existing <=64 MiB
MAP_SHARED window optionally receives MADV_POPULATE_WRITE immediately after mmap,
inside the worker write interval. No new workers, asynchronous population, full
file map, THP setting, global policy or file prefill. Four workers bound active
mappings to256 MiB. Map/populate/unmap wall sums and CPU are separately exported
as additive schema3 fields. All population bytes/time are inside write throughput.

Population errors fail without fallback and accepted metrics are not emitted.
Window destruction and the bounded parent runner clean up after partial success.
This does not guarantee recovery from real OOM or later external truncation.
Linux madvise(2) documents partial population on error and error returns for
population faults; actual support is confirmed by content/window tests on both
R770 hosts, not inferred from the header alone:
https://man7.org/linux/man-pages/man2/madvise.2.html

Initial getrusage phase CPU was unsuitable for very short mmap intervals: tick
accounting could assign more CPU than the measured wall subinterval. Corrected
observePhase uses CLOCK_THREAD_CPUTIME_ID. Overall write CPU and fault deltas keep
their existing RUSAGE_THREAD interval. The original twenty receipts and analysis
remain, but tiny-phase CPU attribution from them is rejected. Twenty new samples
at the corrected fixed source are the primary results below; this is one candidate
with a measurement correction, not a second optimization attempt.

## Behavioral Acceptance

The populate-content test fails against the old executable's missing option,
then passes after implementation. The final all-target build and related CTest
pass4/4 on each host, including the offset benchmark smoke, backend smoke, range
observation export and raw-stage analyzer contracts. The backend smoke reports46
successful invocations with independent content checks, including tiny/unaligned
tails,1/2/4 workers, a64 MiB window crossing, and self-cleaning repeats. The older
offset smoke reports72 successful cases and partial-IO/existing-path cleanup.

Fault behavior in both hosts' final backend smoke:

| Fault | Child exit | Success metrics | Parent payload cleanup |
| --- | ---: | --- | --- |
| mmap ENOMEM | 2 | none | passes |
| mmap then truncate, lazy actual SIGBUS | -7 | none | passes |
| Real population of half window, then injected ENOMEM | 2 | none | passes |
| mmap then truncate, population reports error | 2 | none | passes |

The partial-success shim marker proves the failure path is reached after real
page population. These are diagnostic child tests, not new V3 checkpoint/resume
tests. No runtime code changed and previous runtime tests are not relabeled as a
new acceptance run. An initial driver preflight incorrectly used Linux hostname
as SSH identity; both machine aliases differ from that hostname. It failed before
creating payload; corrected driver uses explicit host plus actual eno17095np0 IP.
The original preflight log is retained.

## Local Five-pair Experiment

R1 then R2, serial hosts and serial samples, one8 GiB sparse shared-inode file at
a time. Four workers,65532-byte writes with1 MiB boundaries,64 MiB windows, no
serial prefill. Five alternating lazy/write pairs per host. Map, writable population,
copy, loop and unmap are all in the write interval. Independent exact content and
byte counts are verified afterward. No network or V3 task exists in these samples.

| Corrected source, median | R1 lazy | R1 populate-write | R2 lazy | R2 populate-write |
| --- | ---: | ---: | ---: | ---: |
| Local write Gbps | 55.078277 | 41.554848 | 54.876631 | 41.040652 |
| CV | 0.293604% | 0.647143% | 0.475942% | 0.836230% |
| Write elapsed seconds | 1.247669 | 1.653705 | 1.252254 | 1.674425 |
| Worker CPU seconds | 4.970749 | 6.361399 | 4.985345 | 6.398590 |
| Worker system CPU seconds | 4.697633 | 4.553324 | 4.709424 | 4.604912 |
| Worker minor faults | 2097152 | 2097152 | 2097152 | 2097152 |
| Worker major faults | 0 | 0 | 0 | 0 |
| Population calls | 0 | 128 | 0 | 128 |
| Population wall sum seconds | 0 | 4.493285 | 0 | 4.548452 |
| Population CPU seconds | 0 | 4.400002 | 0 | 4.442358 |
| Map CPU seconds | 0.000491 | 0.001250 | 0.000464 | 0.001215 |
| Unmap CPU seconds | 0.149430 | 0.145084 | 0.170735 | 0.164002 |
| Residual worker CPU seconds | 4.822418 | 1.779280 | 4.814153 | 1.786444 |

Ratio-of-median gains: R1 **-24.553110%**, R2 **-25.212879%**. Both CV gates pass;
the >=10% gain gate fails on both. All ten corrected pairs regress. Each populated
task actually populates8 GiB over128 calls, maps/unmaps128 windows and verifies
8 GiB content. Minor-fault counts are unchanged: batched population is still charged
faults by this kernel. A user-syscall batching hypothesis does not imply fewer
accounted faults. System CPU alone would hide the increased user/residual work;
overall elapsed and CPU determine this no-go decision.

Wall sums span concurrent workers and can exceed elapsed time; phase CPU is nested
in worker CPU. Residual CPU subtracts measured phase CPU per sample and includes
copy/loop/other worker work, not pure memcpy. Medians of components need not sum
to the median total. This control proves no benefit under this configuration,
not that all mmap algorithms or all file systems must behave identically.

Before both source matrices, both ends match fixed commit, version, configuration,
eight existing binary hashes plus changed offset benchmark and fault shim hashes.
Final source offset SHA256:
`49160d3fc3ed8e6e3d98e8fe118012b37cf19bcf29310bf3b09eed843fd8fe22`.
Final source shim SHA256:
`77fd09250cdb721afc1f9674dd63b148b4dbbcb28229ca1ba35a5837b26ff229`.
RelWithDebInfo; ISA-L/io_uring/TLS/RDMA compiledON, desktop/cross-domain testsOFF.
All eight transfer/test binary hashes match scope09 because no runtime is changed.

## Resources, Cleanup and Reproduction

All forty initial+corrected samples pass content/resource/cleanup, with no excluded
performance sample. Corrected sample minimum MemAvailable: R1 20.469967 GiB,
R2 20.046425 GiB. Across all40: minimum20.040062 GiB, maximum polled owned RSS
260.441406 MiB, owned swap0 and host pswpout delta0. The runner enforces12 GiB
minimum and1 GiB RSS bound, with tmpfs backing budgeted separately. Owned files
and roots are removed each sample; both/dev/shm end with0 used bytes. Production
PIDs126766/164834 retain October30 start times. Unrelated October14 task files,
CPSS(DCC), host policies and network remain unchanged.

Evidence R1 (~464 KiB before final audit/docs):
`/home/Su/experiments/CPNetFlux/2026-11-08-v3-mapped-populate-10`.
Primary: `corrected/analysis.json`, corrected per-host receipts, acceptance build/
CTest/detail logs, `matching-builds-e61a5868abd2.json`, source/final cleanup audits.
Initial `analysis.json`/per-host receipts/logs remain to disclose the clock issue.
No payload, credentials or build products enter Git. Script SHA256:

| Script | SHA256 |
| --- | --- |
| populate_matrix_10.py | b5b78dd5402f49bef2ee725f6f661225845e3f85572593c5857912a82bc65247 |
| populate_matrix_initial_10.py | 737553ebbf11fddfc7e68276d8a353a1a01f8c3c5ef98aa61cee49f60abb9035 |
| final_audit_10.py | b98d8f125f68e17715be58ec3e3aa00381b8fd6a93e894fa775c5dab0cfffac3 |

```sh
cmake --build build -j8
ctest --test-dir build --output-on-failure -R 'offset_write|range_observation_export|raw_stage_analyzer'
python3 "$EVIDENCE/populate_matrix_10.py" check
# Run serially on the named host; copied matching-build receipt is mandatory:
python3 "$EVIDENCE/populate_matrix_10.py" run R770-1
python3 "$EVIDENCE/populate_matrix_10.py" run R770-2
# Fetch only R2 evidence to R1, then:
python3 "$EVIDENCE/populate_matrix_10.py" analyze
python3 "$EVIDENCE/final_audit_10.py" source
```

## Next Scope / Progress

This bounded hypothesis is closed no-go; instrumentation and negative-control
behavior are verified and backed up. No full-task throughput advance is claimed.
Next isolate sender source read/CRC and socket-write CPU from condition waits,
receiver backpressure and per-frame lease/dispatch work. Use the existing fixed
8 GiB resident memory workload and thread CPU boundaries; first measure observer
overhead, then select at most one supported hypothesis. Keep CRC, V3 lease,
checkpoint, recovery, atomic publication and memory/cleanup guards. Only a proven
local candidate proceeds to five complete-task A/B pairs; no automatic lane/queue
expansion or reopening this page-population candidate without new evidence.
