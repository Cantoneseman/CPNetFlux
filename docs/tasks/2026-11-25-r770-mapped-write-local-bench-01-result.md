# R770 Mapped Write Local Benchmark 01 Result

## Scope and decision

This stage reused the existing `cpnetflux-mapped-cost-bench` on R770-1 `/dev/shm` to separate first-write cost from same-mapping overwrite cost. It made no production source change and ran no inter-node transfer or annual comparison.

No low-risk software candidate showed a measured local advantage. Keep the current buffered inline mmap implementation. The local result supports first-touch faults and tmpfs backing-page allocation as a large part of cold mapped-write CPU, but it does not validate a replacement mechanism or predict end-to-end transfer throughput. Stop this optimization stage here.

## Inputs and acceptance

- R1 project commit: `91b4cb68a2e67c58ae24d59245af3f85469ca350`.
- Benchmark executable SHA-256: `0a233bf9e756a2d7ce915e616c181e94455806178cc9087487c31d311ca2eec8`.
- Build check: `ninja -C build cpnetflux-mapped-cost-bench` reported no work.
- Focused smoke check: `python3 tools/test/run_cpnetflux_mapped_cost_bench_smoke.py --build-dir build` passed with 8 valid and 10 invalid cases; all payloads were removed.
- Acceptance for each measured run: both phases returned pass, verified 256 MiB of written contents, had zero major faults and profile errors, and removed the owned payload.

## Method and measurement boundary

The benchmark wrote 256 MiB in 1,024 256 KiB calls through the actual `MappedFileWindow::write` path. It measured two phases on each fresh file: the first write to a sparse tmpfs file and a second equal-sized overwrite through the same live mapping. Each phase was read back and checked outside the write brackets. Five independent profile-off runs supplied the primary CPU, wall, and `RUSAGE_THREAD` fault measurements. Five profile-on runs provided copy-wrapper CPU/wall and fault-counter cross-checks.

The profile-off worker bracket includes mapping on the cold phase and the writer call loop. It excludes readback verification. The same mapping is reused in the warm phase, so mapping setup is present only in the cold phase. Measured map CPU was about 20 microseconds across four windows, too small to explain the cold/warm difference. Profile-on adds `clock_gettime` and `getrusage` around each copy and is secondary evidence.

`perf` callgraph sampling was unavailable. `perf stat -e minor-faults,task-clock -- true` returned no supported events because `perf_event_paranoid=4`. No system setting was changed. These measurements identify wrapper and fault-count differences, not a kernel callgraph or a direct memcpy-versus-page-fault CPU split.

## Results

All ten measured runs passed content verification and cleanup. Every run used one worker, 256 MiB per phase, 256 KiB writes, and four 64 MiB mapping windows. No major faults occurred.

| Metric | Cold first write median | Warm overwrite median |
| --- | ---: | ---: |
| Profile-off worker CPU | 0.111957 s | 0.028987 s |
| Profile-off worker wall | 0.111493 s | 0.029537 s |
| Minor faults | 65,536 | 0 |
| Profile-on copy CPU | 0.112798 s | 0.029094 s |
| Profile-on copy wall | 0.113253 s | 0.029298 s |

The cold phase had exactly one minor fault per 4 KiB page in 256 MiB. The warm phase had none. Cold worker CPU exceeded warm worker CPU by about 0.083 seconds per 256 MiB, while mapping CPU was about 0.000020 seconds. This is consistent with first-write page allocation/fault handling accounting for much of the cold-path cost. The warm result still includes the existing `memcpy` and writer call overhead, about 0.029 seconds of CPU per 256 MiB in this single-thread local test.

Raw JSON is retained at `/home/Su/experiments/CPNetFlux/2026-11-25-r770-mapped-write-local-bench-01/sample-1.json` through `sample-5.json` and `sample-off-1.json` through `sample-off-5.json`. The temporary `/dev/shm` directory was empty and removed; post-run `df -h /dev/shm` showed 16 GiB available.

## Candidate review

No candidate was admitted. Direct-to-mmap already regressed in the prior 1 GiB comparison. Private tmpfs THP was rejected by network counters in prior 1 GiB matrices. Historical page population and larger-write candidates did not pass their performance gates, and the mapped-collapse diagnostic reduced faults without improving worker copy CPU or wall time. This benchmark confirms that cold page faults correlate with substantial local CPU cost; it does not show that a new page policy or copy primitive beats the existing path.

A custom copy primitive would target only the warm-copy component and has no measured alternative implementation in this stage. Repeating a rejected page-preparation strategy would not provide new evidence. Therefore there is no candidate with a local advantage to take to inter-node A/B.

## Stop gate and next step

- Do not run a 1 GiB candidate A/B or annual comparison from this result.
- Do not change THP, sysctl, NIC, or network configuration.
- Keep buffered inline mmap as the production default.
- Resume inter-node performance work only after a separate network acceptance window is established and a software candidate first demonstrates local advantage without repeating the rejected direct-mmap, population, or THP/collapse paths.

R2 was not modified or used. No production source, protocol, pacing, or runtime configuration changed in this stage.
