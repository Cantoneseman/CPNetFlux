# Phase 4E IO Uring Gate Analysis

Date: 2026-05-17

Phase 4E keeps the existing epoll + framed STOR/RETR path. It does not implement io_uring; it decides whether Phase 4F should prototype an optional file IO backend.

## Executive Summary

- Heavy storage bench completed: 384/384 rows passed.
- Heavy GridFTP-like private matrix completed: 432/432 1GiB cases passed across STOR/RETR, crc32c/none, preallocate off/full, final verify full/verified_chunks, file IO buffer 0/1MiB/4MiB, and advice off/sequential/sequential_dontneed.
- Keep defaults unchanged: `file_io_buffer_size=0`, `file_io_advice=off`, `preallocate=off`, `final_verify_policy=full`.
- `sequential_dontneed` is consistently harmful for RETR and storage read paths and should not be recommended.
- `file_io_buffer_size` and `sequential` advice do not clear the default-change gate across both STOR and RETR.
- Data supports a Phase 4F **design/prototype gate** for optional file-IO-only io_uring, not a main-path switch.
- Stage seconds are accumulated across connections/threads, so `stage_*_seconds` can exceed wall-clock elapsed time. Treat them as parallel work/wait mass.

## Artifacts

- Storage raw CSV: `tools/perf/results/20260517T043710Z_storage-bench.csv`
- Storage summary CSV: `tools/perf/results/20260517T043710Z_storage-bench-summary.csv`
- STOR crc32c raw/summary: `tools/perf/results/20260517T050357Z_gridftp-private-matrix-smoke.csv`, `tools/perf/results/20260517T050357Z_gridftp-private-matrix-smoke-summary.csv`
- STOR none raw/summary: `tools/perf/results/20260517T053411Z_gridftp-private-matrix-smoke.csv`, `tools/perf/results/20260517T053411Z_gridftp-private-matrix-smoke-summary.csv`
- RETR crc32c raw/summary: `tools/perf/results/20260517T060356Z_gridftp-private-matrix-smoke.csv`, `tools/perf/results/20260517T060356Z_gridftp-private-matrix-smoke-summary.csv`
- RETR none raw/summary: `tools/perf/results/20260517T063335Z_gridftp-private-matrix-smoke.csv`, `tools/perf/results/20260517T063335Z_gridftp-private-matrix-smoke-summary.csv`

## Representative Median Results

| direction | checksum | baseline median Gbps | file buf 1MiB | file buf 4MiB | sequential | sequential_dontneed | preallocate full | verified_chunks request |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| STOR | crc32c | 1.078850 | 1.086740 | 1.082580 | 1.130730 | 0.925921 | 1.093540 | 1.382080 effective verified_chunks |
| STOR | none | 1.312390 | 1.318490 | 1.393260 | 1.380590 | 0.962393 | 1.363750 | 1.413620 effective full |
| RETR | crc32c | 3.744850 | 2.989930 | 2.844780 | 3.006430 | 0.766325 | 2.364730 | 3.272160 effective verified_chunks |
| RETR | none | 4.327260 | 4.403150 | 3.587180 | 3.243590 | 0.805688 | 3.794800 | 4.248200 effective full |

Baseline means `preallocate=off`, `file_io_buffer_size=0`, `file_io_advice=off`, `final_verify_policy_effective=full`.

## Representative Stage Breakdown

| direction | checksum | median throughput Gbps | write seconds | file IO wait seconds | write calls | avg write bytes | final verify seconds | manifest flush seconds | overall seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| STOR | crc32c | 1.078850 | 5.203660 | 5.201300 | 4096 | 262144 | 1.976280 | 0.381751 | 8.110950 |
| STOR | none | 1.315200 | 5.439630 | 5.437070 | 4096 | 262144 | 0.000000 | 0.886260 | 6.701780 |
| RETR | crc32c | 3.744850 | 4.496250 | 4.493080 | 4096 | 262144 | 0.531422 | 0.753788 | 2.293800 |
| RETR | none | 4.287730 | 5.642840 | 5.638775 | 4096 | 262144 | 0.000000 | 0.692629 | 2.003550 |

## Gate Recommendations

- `file_io_buffer_size=1048576` vs default: STOR +0.7%, RETR -20.2%; keep default `0`.
- `file_io_buffer_size=4194304` vs default: STOR +0.3%, RETR -24.0%; keep default `0`.
- `file_io_advice=sequential` vs default: STOR +4.8%, RETR -19.7%; keep default `off`.
- `file_io_advice=sequential_dontneed` vs default: STOR -14.2%, RETR -79.5%; keep default `off` and do not recommend for file transfers.
- `preallocate=full` vs default: STOR +1.4%, RETR -36.9%; keep default `off`.
- `verified_chunks` remains opt-in. STOR crc32c improved in this sample, but RETR crc32c regressed and checksum none correctly falls back to full.
- io_uring gate: **GO for Phase 4F design/prototype only**. POSIX file IO wait/write mass remains high and low-risk POSIX knobs did not solve both directions. Do not switch the main path in Phase 4E.

## Phase 4F Minimal Prototype Scope

- Add optional `--file-io-backend io_uring`; keep `posix` as default.
- Limit v1 to file IO only; do not change network epoll.
- Cover STOR temp write and RETR source read first; optionally evaluate download temp write after the first prototype.
- Detect liburing at configure time behind an explicit CMake option; runtime fallback remains POSIX.
- Preserve checksum, manifest, resume, final verify, framed STOR/RETR, and GridFTP control semantics.
- Measure against the Phase 4E CSV set before considering any default change.

## Out Of Scope

- No raw FTP STOR/RETR.
- No TLS/GSI, MLST/MLSD, Mode E, SPAS/SPOR, or third-party server-to-server transfer.
- No default change to `verified_chunks`, `preallocate`, file IO buffer, or advice in Phase 4E.

<details>
<summary>Full generated tables</summary>

## Storage Bench Median Summary

| side | op | buffer | preallocate | advice | n | median Gbps | min | max | median elapsed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| local | read | 1048576 | full | off | 3 | 74.788100 | 63.092400 | 76.289600 | 0.114857 |
| local | read | 1048576 | full | sequential | 3 | 75.092000 | 63.021200 | 77.451400 | 0.114392 |
| local | read | 1048576 | full | sequential_dontneed | 3 | 0.922396 | 0.729729 | 0.922467 | 9.312640 |
| local | read | 1048576 | off | off | 3 | 16.124200 | 4.805860 | 64.437200 | 0.532734 |
| local | read | 1048576 | off | sequential | 3 | 74.368300 | 62.720800 | 77.558100 | 0.115505 |
| local | read | 1048576 | off | sequential_dontneed | 3 | 0.922944 | 0.920894 | 0.923920 | 9.307110 |
| local | read | 262144 | full | off | 3 | 64.614500 | 4.532790 | 78.843200 | 0.132941 |
| local | read | 262144 | full | sequential | 3 | 77.009800 | 64.436100 | 79.228800 | 0.111543 |
| local | read | 262144 | full | sequential_dontneed | 3 | 0.921466 | 0.761774 | 0.923154 | 9.322030 |
| local | read | 262144 | off | off | 3 | 31.456600 | 27.818000 | 60.423500 | 0.273073 |
| local | read | 262144 | off | sequential | 3 | 77.034200 | 64.529600 | 77.726400 | 0.111508 |
| local | read | 262144 | off | sequential_dontneed | 3 | 0.923412 | 0.918025 | 0.931566 | 9.302390 |
| local | read | 4194304 | full | off | 3 | 58.633700 | 7.693840 | 72.910600 | 0.146502 |
| local | read | 4194304 | full | sequential | 3 | 70.394000 | 58.816500 | 73.296500 | 0.122027 |
| local | read | 4194304 | full | sequential_dontneed | 3 | 0.922050 | 0.740746 | 0.924301 | 9.316130 |
| local | read | 4194304 | off | off | 3 | 58.860600 | 17.657500 | 72.638100 | 0.145937 |
| local | read | 4194304 | off | sequential | 3 | 70.567700 | 58.193600 | 72.420100 | 0.121726 |
| local | read | 4194304 | off | sequential_dontneed | 3 | 0.921598 | 0.919192 | 0.923521 | 9.320690 |
| local | read | 65536 | full | off | 3 | 50.945600 | 2.345460 | 75.723500 | 0.168610 |
| local | read | 65536 | full | sequential | 3 | 73.482900 | 62.338900 | 75.890800 | 0.116897 |
| local | read | 65536 | full | sequential_dontneed | 3 | 0.917527 | 0.761000 | 0.921938 | 9.362050 |
| local | read | 65536 | off | off | 3 | 1.216640 | 0.961966 | 3.276730 | 7.060380 |
| local | read | 65536 | off | sequential | 3 | 15.812300 | 15.525400 | 52.083700 | 0.543243 |
| local | read | 65536 | off | sequential_dontneed | 3 | 0.923058 | 0.913336 | 0.923459 | 9.305950 |
| local | write | 1048576 | full | off | 3 | 1.258800 | 1.229250 | 1.274920 | 6.823920 |
| local | write | 1048576 | full | sequential | 3 | 1.274480 | 1.250730 | 1.277530 | 6.739970 |
| local | write | 1048576 | full | sequential_dontneed | 3 | 1.252930 | 1.241880 | 1.272170 | 6.855880 |
| local | write | 1048576 | off | off | 3 | 0.924228 | 0.900590 | 0.943074 | 9.294170 |
| local | write | 1048576 | off | sequential | 3 | 0.921021 | 0.919983 | 0.991753 | 9.326530 |
| local | write | 1048576 | off | sequential_dontneed | 3 | 0.919305 | 0.910822 | 0.964196 | 9.343950 |
| local | write | 262144 | full | off | 3 | 1.257460 | 1.248500 | 1.263230 | 6.831170 |
| local | write | 262144 | full | sequential | 3 | 1.258800 | 1.204400 | 1.305010 | 6.823920 |
| local | write | 262144 | full | sequential_dontneed | 3 | 1.241550 | 1.214670 | 1.278300 | 6.918700 |
| local | write | 262144 | off | off | 3 | 0.923801 | 0.913684 | 0.925166 | 9.298460 |
| local | write | 262144 | off | sequential | 3 | 0.941721 | 0.919585 | 0.971564 | 9.121530 |
| local | write | 262144 | off | sequential_dontneed | 3 | 0.934948 | 0.899080 | 0.984539 | 9.187610 |
| local | write | 4194304 | full | off | 3 | 1.203070 | 1.199760 | 1.300470 | 7.139990 |
| local | write | 4194304 | full | sequential | 3 | 1.301510 | 1.250760 | 1.307970 | 6.599970 |
| local | write | 4194304 | full | sequential_dontneed | 3 | 1.211380 | 1.204430 | 1.248540 | 7.091040 |
| local | write | 4194304 | off | off | 3 | 0.929681 | 0.924778 | 0.934110 | 9.239650 |
| local | write | 4194304 | off | sequential | 3 | 0.918892 | 0.908436 | 0.984930 | 9.348140 |
| local | write | 4194304 | off | sequential_dontneed | 3 | 0.922038 | 0.902824 | 0.983386 | 9.316240 |
| local | write | 65536 | full | off | 3 | 1.286820 | 1.266260 | 1.292110 | 6.675300 |
| local | write | 65536 | full | sequential | 3 | 1.254530 | 1.237060 | 1.382600 | 6.847130 |
| local | write | 65536 | full | sequential_dontneed | 3 | 1.228480 | 1.207840 | 1.257270 | 6.992320 |
| local | write | 65536 | off | off | 3 | 0.925267 | 0.916183 | 1.537470 | 9.283740 |
| local | write | 65536 | off | sequential | 3 | 0.917653 | 0.907109 | 1.037200 | 9.360760 |
| local | write | 65536 | off | sequential_dontneed | 3 | 0.937976 | 0.905939 | 1.001100 | 9.157940 |
| remote | read | 1048576 | full | off | 3 | 74.559700 | 62.508200 | 76.213100 | 0.115209 |
| remote | read | 1048576 | full | sequential | 3 | 74.802000 | 62.418100 | 77.533500 | 0.114836 |
| remote | read | 1048576 | full | sequential_dontneed | 3 | 0.922550 | 0.663747 | 0.923752 | 9.311080 |
| remote | read | 1048576 | off | off | 3 | 74.319000 | 62.296100 | 77.022100 | 0.115582 |
| remote | read | 1048576 | off | sequential | 3 | 74.907400 | 63.050300 | 76.914200 | 0.114674 |
| remote | read | 1048576 | off | sequential_dontneed | 3 | 0.922391 | 0.651752 | 0.923366 | 9.312690 |
| remote | read | 262144 | full | off | 3 | 76.604800 | 63.473700 | 77.975700 | 0.112133 |
| remote | read | 262144 | full | sequential | 3 | 76.271600 | 63.752500 | 77.988500 | 0.112623 |
| remote | read | 262144 | full | sequential_dontneed | 3 | 0.923047 | 0.715078 | 0.923507 | 9.306070 |
| remote | read | 262144 | off | off | 3 | 77.062400 | 63.752700 | 78.405700 | 0.111467 |
| remote | read | 262144 | off | sequential | 3 | 76.806100 | 63.820900 | 78.683000 | 0.111839 |
| remote | read | 262144 | off | sequential_dontneed | 3 | 0.922175 | 0.624826 | 0.923409 | 9.314870 |
| remote | read | 4194304 | full | off | 3 | 71.749300 | 58.860100 | 73.166200 | 0.119722 |
| remote | read | 4194304 | full | sequential | 3 | 71.134700 | 58.709300 | 73.207200 | 0.120756 |
| remote | read | 4194304 | full | sequential_dontneed | 3 | 0.922922 | 0.710229 | 0.923397 | 9.307330 |
| remote | read | 4194304 | off | off | 3 | 71.983600 | 59.040000 | 73.537600 | 0.119332 |
| remote | read | 4194304 | off | sequential | 3 | 71.241100 | 59.198500 | 74.263000 | 0.120576 |
| remote | read | 4194304 | off | sequential_dontneed | 3 | 0.922235 | 0.647209 | 0.923298 | 9.314260 |
| remote | read | 65536 | full | off | 3 | 72.903900 | 61.812400 | 75.152200 | 0.117825 |
| remote | read | 65536 | full | sequential | 3 | 74.107600 | 61.712800 | 75.296300 | 0.115912 |
| remote | read | 65536 | full | sequential_dontneed | 3 | 0.923049 | 0.795636 | 0.923248 | 9.306040 |
| remote | read | 65536 | off | off | 3 | 33.863300 | 14.650800 | 54.417000 | 0.253665 |
| remote | read | 65536 | off | sequential | 3 | 64.565900 | 62.171200 | 76.487400 | 0.133041 |
| remote | read | 65536 | off | sequential_dontneed | 3 | 0.923612 | 0.923164 | 1.032370 | 9.300370 |
| remote | write | 1048576 | full | off | 3 | 2.391420 | 2.160450 | 2.585860 | 3.591980 |
| remote | write | 1048576 | full | sequential | 3 | 2.014650 | 1.884620 | 2.164720 | 4.263740 |
| remote | write | 1048576 | full | sequential_dontneed | 3 | 2.066950 | 1.094250 | 2.246520 | 4.155850 |
| remote | write | 1048576 | off | off | 3 | 1.032050 | 0.928790 | 2.384480 | 8.323150 |
| remote | write | 1048576 | off | sequential | 3 | 0.931526 | 0.921373 | 2.375770 | 9.221360 |
| remote | write | 1048576 | off | sequential_dontneed | 3 | 0.998518 | 0.934237 | 2.341500 | 8.602690 |
| remote | write | 262144 | full | off | 3 | 2.267720 | 2.180330 | 2.621820 | 3.787920 |
| remote | write | 262144 | full | sequential | 3 | 2.169260 | 1.924150 | 2.186880 | 3.959850 |
| remote | write | 262144 | full | sequential_dontneed | 3 | 1.999400 | 1.993960 | 2.289420 | 4.296270 |
| remote | write | 262144 | off | off | 3 | 1.045610 | 0.924298 | 2.514590 | 8.215210 |
| remote | write | 262144 | off | sequential | 3 | 1.030420 | 0.927935 | 2.207150 | 8.336360 |
| remote | write | 262144 | off | sequential_dontneed | 3 | 1.027190 | 0.941503 | 2.508170 | 8.362560 |
| remote | write | 4194304 | full | off | 3 | 2.437400 | 2.349750 | 3.002690 | 3.524220 |
| remote | write | 4194304 | full | sequential | 3 | 2.029810 | 1.686610 | 2.164890 | 4.231900 |
| remote | write | 4194304 | full | sequential_dontneed | 3 | 2.072990 | 1.989840 | 2.138930 | 4.143740 |
| remote | write | 4194304 | off | off | 3 | 1.036540 | 0.925490 | 2.380500 | 8.287100 |
| remote | write | 4194304 | off | sequential | 3 | 0.924070 | 0.814452 | 0.928613 | 9.295760 |
| remote | write | 4194304 | off | sequential_dontneed | 3 | 1.027610 | 0.939572 | 2.287440 | 8.359130 |
| remote | write | 65536 | full | off | 3 | 2.291660 | 2.248880 | 2.647180 | 3.748350 |
| remote | write | 65536 | full | sequential | 3 | 2.031620 | 1.910230 | 2.294380 | 4.228120 |
| remote | write | 65536 | full | sequential_dontneed | 3 | 2.039360 | 1.850960 | 2.184830 | 4.212070 |
| remote | write | 65536 | off | off | 3 | 0.922928 | 0.907587 | 4.074690 | 9.307270 |
| remote | write | 65536 | off | sequential | 3 | 0.918251 | 0.910041 | 0.936276 | 9.354670 |
| remote | write | 65536 | off | sequential_dontneed | 3 | 0.931411 | 0.921437 | 1.001600 | 9.222490 |

## GridFTP Matrix Median Summary

| direction | checksum | prealloc | file buf | advice | policy | effective | pass | fail | median Gbps | min | max | median elapsed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stor | crc32c | full | 0 | off | full | full | 3 | 0 | 1.093540 | 1.090010 | 1.104410 | 7.855170 |
| stor | crc32c | full | 0 | off | verified_chunks | verified_chunks | 3 | 0 | 1.458140 | 1.269590 | 1.464700 | 5.891040 |
| stor | crc32c | full | 0 | sequential | full | full | 3 | 0 | 1.382450 | 1.067710 | 1.382690 | 6.213560 |
| stor | crc32c | full | 0 | sequential | verified_chunks | verified_chunks | 3 | 0 | 1.320690 | 1.311310 | 1.396840 | 6.504140 |
| stor | crc32c | full | 0 | sequential_dontneed | full | full | 3 | 0 | 0.921019 | 0.920129 | 0.925123 | 9.326550 |
| stor | crc32c | full | 0 | sequential_dontneed | verified_chunks | verified_chunks | 3 | 0 | 0.962431 | 0.956053 | 0.963129 | 8.925240 |
| stor | crc32c | full | 1048576 | off | full | full | 3 | 0 | 1.101930 | 1.092230 | 1.189700 | 7.795380 |
| stor | crc32c | full | 1048576 | off | verified_chunks | verified_chunks | 3 | 0 | 1.352890 | 1.309800 | 1.405840 | 6.349320 |
| stor | crc32c | full | 1048576 | sequential | full | full | 3 | 0 | 1.089580 | 1.067010 | 1.244950 | 7.883720 |
| stor | crc32c | full | 1048576 | sequential | verified_chunks | verified_chunks | 3 | 0 | 1.306740 | 1.255800 | 1.389500 | 6.573570 |
| stor | crc32c | full | 1048576 | sequential_dontneed | full | full | 3 | 0 | 0.924115 | 0.918674 | 0.926593 | 9.295310 |
| stor | crc32c | full | 1048576 | sequential_dontneed | verified_chunks | verified_chunks | 3 | 0 | 0.963118 | 0.963114 | 0.963224 | 8.918880 |
| stor | crc32c | full | 4194304 | off | full | full | 3 | 0 | 1.067830 | 1.034600 | 1.092990 | 8.044280 |
| stor | crc32c | full | 4194304 | off | verified_chunks | verified_chunks | 3 | 0 | 1.351960 | 1.304220 | 1.371340 | 6.353700 |
| stor | crc32c | full | 4194304 | sequential | full | full | 3 | 0 | 1.083230 | 1.076350 | 1.083480 | 7.929920 |
| stor | crc32c | full | 4194304 | sequential | verified_chunks | verified_chunks | 3 | 0 | 1.295510 | 1.221490 | 1.401450 | 6.630520 |
| stor | crc32c | full | 4194304 | sequential_dontneed | full | full | 3 | 0 | 0.919762 | 0.904339 | 0.926726 | 9.339300 |
| stor | crc32c | full | 4194304 | sequential_dontneed | verified_chunks | verified_chunks | 3 | 0 | 0.953616 | 0.947663 | 0.965286 | 9.007750 |
| stor | crc32c | off | 0 | off | full | full | 3 | 0 | 1.078850 | 1.015540 | 1.314050 | 7.962150 |
| stor | crc32c | off | 0 | off | verified_chunks | verified_chunks | 3 | 0 | 1.382080 | 1.311670 | 1.440460 | 6.215230 |
| stor | crc32c | off | 0 | sequential | full | full | 3 | 0 | 1.130730 | 1.065180 | 1.144390 | 7.596800 |
| stor | crc32c | off | 0 | sequential | verified_chunks | verified_chunks | 3 | 0 | 1.376560 | 1.338290 | 1.435490 | 6.240160 |
| stor | crc32c | off | 0 | sequential_dontneed | full | full | 3 | 0 | 0.925921 | 0.901722 | 0.926779 | 9.277180 |
| stor | crc32c | off | 0 | sequential_dontneed | verified_chunks | verified_chunks | 3 | 0 | 0.961053 | 0.956908 | 0.968404 | 8.938040 |
| stor | crc32c | off | 1048576 | off | full | full | 3 | 0 | 1.086740 | 1.052810 | 1.163740 | 7.904300 |
| stor | crc32c | off | 1048576 | off | verified_chunks | verified_chunks | 3 | 0 | 1.458190 | 1.393790 | 1.495030 | 5.890840 |
| stor | crc32c | off | 1048576 | sequential | full | full | 3 | 0 | 1.186750 | 1.083830 | 1.240590 | 7.238190 |
| stor | crc32c | off | 1048576 | sequential | verified_chunks | verified_chunks | 3 | 0 | 1.501670 | 1.380090 | 1.507980 | 5.720270 |
| stor | crc32c | off | 1048576 | sequential_dontneed | full | full | 3 | 0 | 0.927993 | 0.920429 | 0.931327 | 9.256470 |
| stor | crc32c | off | 1048576 | sequential_dontneed | verified_chunks | verified_chunks | 3 | 0 | 0.962772 | 0.954952 | 0.969455 | 8.922090 |
| stor | crc32c | off | 4194304 | off | full | full | 3 | 0 | 1.082580 | 0.974325 | 1.087670 | 7.934670 |
| stor | crc32c | off | 4194304 | off | verified_chunks | verified_chunks | 3 | 0 | 1.421410 | 1.280180 | 1.422100 | 6.043260 |
| stor | crc32c | off | 4194304 | sequential | full | full | 3 | 0 | 1.057790 | 1.055110 | 1.076600 | 8.120680 |
| stor | crc32c | off | 4194304 | sequential | verified_chunks | verified_chunks | 3 | 0 | 1.355020 | 1.342330 | 1.482520 | 6.339350 |
| stor | crc32c | off | 4194304 | sequential_dontneed | full | full | 3 | 0 | 0.919677 | 0.875115 | 0.923403 | 9.340170 |
| stor | crc32c | off | 4194304 | sequential_dontneed | verified_chunks | verified_chunks | 3 | 0 | 0.963514 | 0.961599 | 0.965832 | 8.915220 |
| stor | none | full | 0 | off | full | full | 3 | 0 | 1.363750 | 1.325890 | 1.379800 | 6.298750 |
| stor | none | full | 0 | off | verified_chunks | full | 3 | 0 | 1.403820 | 1.397240 | 1.432530 | 6.118980 |
| stor | none | full | 0 | sequential | full | full | 3 | 0 | 1.372190 | 1.281270 | 1.402900 | 6.260030 |
| stor | none | full | 0 | sequential | verified_chunks | full | 3 | 0 | 1.442100 | 1.324580 | 1.458600 | 5.956550 |
| stor | none | full | 0 | sequential_dontneed | full | full | 3 | 0 | 0.962095 | 0.926967 | 0.963159 | 8.928360 |
| stor | none | full | 0 | sequential_dontneed | verified_chunks | full | 3 | 0 | 0.958056 | 0.933703 | 0.969524 | 8.966000 |
| stor | none | full | 1048576 | off | full | full | 3 | 0 | 1.387380 | 1.288830 | 1.445000 | 6.191480 |
| stor | none | full | 1048576 | off | verified_chunks | full | 3 | 0 | 1.429360 | 1.271070 | 1.473380 | 6.009630 |
| stor | none | full | 1048576 | sequential | full | full | 3 | 0 | 1.392510 | 1.311650 | 1.412250 | 6.168680 |
| stor | none | full | 1048576 | sequential | verified_chunks | full | 3 | 0 | 1.394340 | 1.294890 | 1.452010 | 6.160560 |
| stor | none | full | 1048576 | sequential_dontneed | full | full | 3 | 0 | 0.968598 | 0.957297 | 0.970142 | 8.868420 |
| stor | none | full | 1048576 | sequential_dontneed | verified_chunks | full | 3 | 0 | 0.967968 | 0.942858 | 0.968857 | 8.874200 |
| stor | none | full | 4194304 | off | full | full | 3 | 0 | 1.403090 | 1.262400 | 1.410550 | 6.122170 |
| stor | none | full | 4194304 | off | verified_chunks | full | 3 | 0 | 1.452720 | 1.418140 | 1.493410 | 5.913000 |
| stor | none | full | 4194304 | sequential | full | full | 3 | 0 | 1.376110 | 1.357800 | 1.391250 | 6.242200 |
| stor | none | full | 4194304 | sequential | verified_chunks | full | 3 | 0 | 1.408970 | 1.333340 | 1.424680 | 6.096630 |
| stor | none | full | 4194304 | sequential_dontneed | full | full | 3 | 0 | 0.948729 | 0.939935 | 0.968887 | 9.054150 |
| stor | none | full | 4194304 | sequential_dontneed | verified_chunks | full | 3 | 0 | 0.952393 | 0.934573 | 0.963231 | 9.019320 |
| stor | none | off | 0 | off | full | full | 3 | 0 | 1.312390 | 1.276930 | 1.318010 | 6.545270 |
| stor | none | off | 0 | off | verified_chunks | full | 3 | 0 | 1.413620 | 1.270120 | 1.443930 | 6.076570 |
| stor | none | off | 0 | sequential | full | full | 3 | 0 | 1.380590 | 1.290300 | 1.441570 | 6.221910 |
| stor | none | off | 0 | sequential | verified_chunks | full | 3 | 0 | 1.399100 | 1.287740 | 1.444680 | 6.139600 |
| stor | none | off | 0 | sequential_dontneed | full | full | 3 | 0 | 0.962393 | 0.958005 | 0.968526 | 8.925600 |
| stor | none | off | 0 | sequential_dontneed | verified_chunks | full | 3 | 0 | 0.956184 | 0.953980 | 0.969174 | 8.983550 |
| stor | none | off | 1048576 | off | full | full | 3 | 0 | 1.318490 | 1.265270 | 1.434110 | 6.514980 |
| stor | none | off | 1048576 | off | verified_chunks | full | 3 | 0 | 1.363910 | 1.362060 | 1.415170 | 6.298010 |
| stor | none | off | 1048576 | sequential | full | full | 3 | 0 | 1.322270 | 1.258230 | 1.389900 | 6.496330 |
| stor | none | off | 1048576 | sequential | verified_chunks | full | 3 | 0 | 1.368680 | 1.292780 | 1.420760 | 6.276080 |
| stor | none | off | 1048576 | sequential_dontneed | full | full | 3 | 0 | 0.934683 | 0.931644 | 0.963723 | 9.190210 |
| stor | none | off | 1048576 | sequential_dontneed | verified_chunks | full | 3 | 0 | 0.956981 | 0.951285 | 0.961998 | 8.976070 |
| stor | none | off | 4194304 | off | full | full | 3 | 0 | 1.393260 | 1.332830 | 1.451100 | 6.165340 |
| stor | none | off | 4194304 | off | verified_chunks | full | 3 | 0 | 1.394040 | 1.366690 | 1.419790 | 6.161910 |
| stor | none | off | 4194304 | sequential | full | full | 3 | 0 | 1.398420 | 1.356480 | 1.432670 | 6.142590 |
| stor | none | off | 4194304 | sequential | verified_chunks | full | 3 | 0 | 1.357860 | 1.335990 | 1.406990 | 6.326100 |
| stor | none | off | 4194304 | sequential_dontneed | full | full | 3 | 0 | 0.957848 | 0.957391 | 0.967236 | 8.967960 |
| stor | none | off | 4194304 | sequential_dontneed | verified_chunks | full | 3 | 0 | 0.962689 | 0.957611 | 0.969958 | 8.922850 |
| retr | crc32c | full | 0 | off | full | full | 3 | 0 | 2.364730 | 2.275780 | 3.540480 | 3.632530 |
| retr | crc32c | full | 0 | off | verified_chunks | verified_chunks | 3 | 0 | 3.216700 | 2.439500 | 3.668960 | 2.670410 |
| retr | crc32c | full | 0 | sequential | full | full | 3 | 0 | 2.903090 | 2.785230 | 2.998130 | 2.958890 |
| retr | crc32c | full | 0 | sequential | verified_chunks | verified_chunks | 3 | 0 | 4.402830 | 3.638140 | 4.414020 | 1.951000 |
| retr | crc32c | full | 0 | sequential_dontneed | full | full | 3 | 0 | 0.743785 | 0.723042 | 0.769513 | 11.549000 |
| retr | crc32c | full | 0 | sequential_dontneed | verified_chunks | verified_chunks | 3 | 0 | 0.809532 | 0.784248 | 0.820534 | 10.611000 |
| retr | crc32c | full | 1048576 | off | full | full | 3 | 0 | 3.250270 | 3.224780 | 3.285500 | 2.642840 |
| retr | crc32c | full | 1048576 | off | verified_chunks | verified_chunks | 3 | 0 | 3.738330 | 3.402930 | 4.450100 | 2.297800 |

_Only the first 80 of 144 grouped rows are shown._

## Stage And File IO Breakdown

Stage seconds are accumulated across connections/threads, so `stage_*_seconds` can exceed wall-clock `overall` time. Treat these as work/wait mass, not elapsed time.

| direction | checksum | prealloc | file buf | advice | effective | read s | write s | io wait s | read calls | write calls | avg read | avg write | final verify s | overall s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| retr | crc32c | full | 0 | off | full | 0.000000 | 4.659080 | 4.649350 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.564627 | 3.632530 |
| retr | crc32c | full | 0 | off | verified_chunks | 0.000000 | 5.120310 | 5.116950 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.670410 |
| retr | crc32c | full | 0 | sequential | full | 0.000000 | 4.662030 | 4.659210 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.342837 | 2.958890 |
| retr | crc32c | full | 0 | sequential | verified_chunks | 0.000000 | 6.133890 | 6.130870 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 1.951000 |
| retr | crc32c | full | 0 | sequential_dontneed | full | 0.000000 | 0.432915 | 0.429727 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.338451 | 11.549000 |
| retr | crc32c | full | 0 | sequential_dontneed | verified_chunks | 0.000000 | 0.592811 | 0.589850 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 10.611000 |
| retr | crc32c | full | 1048576 | off | full | 0.000000 | 6.397470 | 6.303560 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.703388 | 2.642840 |
| retr | crc32c | full | 1048576 | off | verified_chunks | 0.000000 | 7.155200 | 7.061890 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.297800 |
| retr | crc32c | full | 1048576 | sequential | full | 0.000000 | 6.087640 | 5.989590 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.363269 | 2.293100 |
| retr | crc32c | full | 1048576 | sequential | verified_chunks | 0.000000 | 5.296310 | 5.218870 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.849760 |
| retr | crc32c | full | 1048576 | sequential_dontneed | full | 0.000000 | 0.600038 | 0.548889 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.336401 | 11.231600 |
| retr | crc32c | full | 1048576 | sequential_dontneed | verified_chunks | 0.000000 | 0.556863 | 0.504847 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 11.008700 |
| retr | crc32c | full | 4194304 | off | full | 0.000000 | 6.591260 | 6.489700 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.365527 | 2.542480 |
| retr | crc32c | full | 4194304 | off | verified_chunks | 0.000000 | 3.830340 | 3.692970 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.245920 |
| retr | crc32c | full | 4194304 | sequential | full | 0.000000 | 6.096000 | 5.921290 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.344290 | 2.340520 |
| retr | crc32c | full | 4194304 | sequential | verified_chunks | 0.000000 | 6.044410 | 5.908400 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.001290 |
| retr | crc32c | full | 4194304 | sequential_dontneed | full | 0.000000 | 0.517337 | 0.448541 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.341265 | 11.541800 |
| retr | crc32c | full | 4194304 | sequential_dontneed | verified_chunks | 0.000000 | 0.987149 | 0.920226 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 10.684900 |
| retr | crc32c | off | 0 | off | full | 0.000000 | 4.496250 | 4.493080 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.531422 | 2.293800 |
| retr | crc32c | off | 0 | off | verified_chunks | 0.000000 | 5.077270 | 5.073990 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.625160 |
| retr | crc32c | off | 0 | sequential | full | 0.000000 | 5.654160 | 5.647690 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.345195 | 2.857190 |
| retr | crc32c | off | 0 | sequential | verified_chunks | 0.000000 | 5.510500 | 5.506600 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.682700 |
| retr | crc32c | off | 0 | sequential_dontneed | full | 0.000000 | 0.547967 | 0.544941 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.570761 | 11.209300 |
| retr | crc32c | off | 0 | sequential_dontneed | verified_chunks | 0.000000 | 0.580694 | 0.577574 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 10.585100 |
| retr | crc32c | off | 1048576 | off | full | 0.000000 | 5.160590 | 5.089410 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.564598 | 2.872960 |
| retr | crc32c | off | 1048576 | off | verified_chunks | 0.000000 | 5.662520 | 5.602870 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.496470 |
| retr | crc32c | off | 1048576 | sequential | full | 0.000000 | 5.073260 | 4.986630 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.447040 | 2.338910 |
| retr | crc32c | off | 1048576 | sequential | verified_chunks | 0.000000 | 4.768950 | 4.686790 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.817150 |
| retr | crc32c | off | 1048576 | sequential_dontneed | full | 0.000000 | 0.705013 | 0.655257 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.339636 | 11.280900 |
| retr | crc32c | off | 1048576 | sequential_dontneed | verified_chunks | 0.000000 | 1.790190 | 1.739490 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 11.147000 |
| retr | crc32c | off | 4194304 | off | full | 0.000000 | 5.539250 | 5.423840 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.377202 | 3.019540 |
| retr | crc32c | off | 4194304 | off | verified_chunks | 0.000000 | 4.813280 | 4.704850 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.316400 |
| retr | crc32c | off | 4194304 | sequential | full | 0.000000 | 6.548280 | 5.424830 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.477335 | 2.647670 |
| retr | crc32c | off | 4194304 | sequential | verified_chunks | 0.000000 | 5.765480 | 5.652250 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.219640 |
| retr | crc32c | off | 4194304 | sequential_dontneed | full | 0.000000 | 0.526211 | 0.456284 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.339591 | 11.005400 |
| retr | crc32c | off | 4194304 | sequential_dontneed | verified_chunks | 0.000000 | 0.511172 | 0.440497 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 11.408600 |
| retr | none | full | 0 | off | full | 0.000000 | 4.043510 | 4.040700 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.263600 |
| retr | none | full | 0 | off | full | 0.000000 | 4.917390 | 4.914520 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.528150 |
| retr | none | full | 0 | sequential | full | 0.000000 | 4.617970 | 4.615070 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.101500 |
| retr | none | full | 0 | sequential | full | 0.000000 | 5.818090 | 5.815530 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 1.951080 |
| retr | none | full | 0 | sequential_dontneed | full | 0.000000 | 0.715361 | 0.711713 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 11.040200 |
| retr | none | full | 0 | sequential_dontneed | full | 0.000000 | 0.540902 | 0.537912 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 10.796300 |
| retr | none | full | 1048576 | off | full | 0.000000 | 5.226970 | 5.061660 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.106870 |
| retr | none | full | 1048576 | off | full | 0.000000 | 8.556190 | 8.406530 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.060730 |
| retr | none | full | 1048576 | sequential | full | 0.000000 | 5.717530 | 5.611550 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.910580 |
| retr | none | full | 1048576 | sequential | full | 0.000000 | 4.286470 | 4.115750 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.472180 |
| retr | none | full | 1048576 | sequential_dontneed | full | 0.000000 | 0.682000 | 0.630487 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 10.967400 |
| retr | none | full | 1048576 | sequential_dontneed | full | 0.000000 | 0.528649 | 0.476251 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 10.801300 |
| retr | none | full | 4194304 | off | full | 0.000000 | 4.862610 | 4.661890 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.105030 |
| retr | none | full | 4194304 | off | full | 0.000000 | 5.651090 | 5.435930 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 1.838000 |
| retr | none | full | 4194304 | sequential | full | 0.000000 | 4.798830 | 4.650890 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.417100 |
| retr | none | full | 4194304 | sequential | full | 0.000000 | 7.035630 | 6.859760 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 1.933170 |
| retr | none | full | 4194304 | sequential_dontneed | full | 0.000000 | 0.545125 | 0.470233 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 10.961000 |
| retr | none | full | 4194304 | sequential_dontneed | full | 0.000000 | 0.998778 | 0.932598 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 10.795700 |
| retr | none | off | 0 | off | full | 0.000000 | 4.948650 | 4.944410 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 1.985080 |
| retr | none | off | 0 | off | full | 0.000000 | 6.337030 | 6.333140 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.022020 |
| retr | none | off | 0 | sequential | full | 0.000000 | 5.399060 | 5.396460 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.648280 |
| retr | none | off | 0 | sequential | full | 0.000000 | 4.975340 | 4.972880 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 2.004890 |
| retr | none | off | 0 | sequential_dontneed | full | 0.000000 | 0.492926 | 0.489993 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 10.661600 |
| retr | none | off | 0 | sequential_dontneed | full | 0.000000 | 0.569493 | 0.566539 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 10.822100 |
| retr | none | off | 1048576 | off | full | 0.000000 | 4.537870 | 4.409960 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 1.950860 |
| retr | none | off | 1048576 | off | full | 0.000000 | 5.807550 | 5.690860 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.200500 |
| retr | none | off | 1048576 | sequential | full | 0.000000 | 6.514600 | 6.378060 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.333230 |
| retr | none | off | 1048576 | sequential | full | 0.000000 | 4.741090 | 4.614050 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 2.254510 |
| retr | none | off | 1048576 | sequential_dontneed | full | 0.000000 | 0.539869 | 0.484277 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 10.576100 |
| retr | none | off | 1048576 | sequential_dontneed | full | 0.000000 | 1.050030 | 0.995904 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 10.946200 |
| retr | none | off | 4194304 | off | full | 0.000000 | 5.770420 | 5.591650 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.394620 |
| retr | none | off | 4194304 | off | full | 0.000000 | 6.074450 | 5.923980 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.375400 |
| retr | none | off | 4194304 | sequential | full | 0.000000 | 5.750580 | 5.575900 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.349950 |
| retr | none | off | 4194304 | sequential | full | 0.000000 | 4.110640 | 3.951550 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 2.106870 |
| retr | none | off | 4194304 | sequential_dontneed | full | 0.000000 | 0.517653 | 0.445917 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 11.181900 |
| retr | none | off | 4194304 | sequential_dontneed | full | 0.000000 | 0.507143 | 0.438331 | 0.000000 | 256.000000 | 0.000000 | 4194300.000000 | 0.000000 | 11.066900 |
| stor | crc32c | full | 0 | off | full | 0.000000 | 5.125290 | 5.122620 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 1.876090 | 8.024320 |
| stor | crc32c | full | 0 | off | verified_chunks | 0.000000 | 5.271840 | 5.269340 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 6.061490 |
| stor | crc32c | full | 0 | sequential | full | 0.000000 | 5.035770 | 5.033410 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.339451 | 6.369900 |
| stor | crc32c | full | 0 | sequential | verified_chunks | 0.000000 | 5.099410 | 5.097120 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 6.663440 |
| stor | crc32c | full | 0 | sequential_dontneed | full | 0.000000 | 0.335421 | 0.332615 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.351746 | 11.262400 |
| stor | crc32c | full | 0 | sequential_dontneed | verified_chunks | 0.000000 | 0.336709 | 0.334028 | 0.000000 | 4096.000000 | 0.000000 | 262144.000000 | 0.000000 | 11.046800 |
| stor | crc32c | full | 1048576 | off | full | 0.000000 | 5.191940 | 5.144470 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 1.789330 | 7.967590 |
| stor | crc32c | full | 1048576 | off | verified_chunks | 0.000000 | 5.244270 | 5.197480 | 0.000000 | 1024.000000 | 0.000000 | 1048580.000000 | 0.000000 | 6.508120 |

_Only the first 80 of 144 grouped rows are shown._

## Gate Recommendations

- `file_io_buffer_size=1048576` vs default: STOR 0.7%, RETR -20.2%; keep default `0` unless both directions clear the gate.
- `file_io_buffer_size=4194304` vs default: STOR 0.3%, RETR -24.0%; keep default `0` unless both directions clear the gate.
- `file_io_advice=sequential` vs default: STOR 4.8%, RETR -19.7%; keep default `off` unless both directions clear the gate.
- `file_io_advice=sequential_dontneed` vs default: STOR -14.2%, RETR -79.5%; keep default `off` unless both directions clear the gate.
- `preallocate=full` vs default: STOR 1.4%, RETR -36.9%; keep default `off` unless both directions clear the gate.
- `verified_chunks` opt-in vs full: STOR 28.1%, RETR -12.6%; keep opt-in in Phase 4E regardless of speedup.
- io_uring gate: **GO for Phase 4F design only**, because stor POSIX file IO parallel-summed stage ratio is high (read=0.0%, write=64.2%, wait=64.1%); retr POSIX file IO parallel-summed stage ratio is high (read=0.0%, write=196.0%, wait=195.9%).

## Phase 4F Minimal Prototype Scope If Gate Is GO

- Add optional `--file-io-backend io_uring`; keep `posix` as default.
- Limit v1 to file IO only; do not change network epoll.
- Cover STOR temp write and RETR source read first.
- Detect liburing at configure time behind an explicit CMake option; runtime fallback remains POSIX.
- Preserve checksum, manifest, resume, final verify, framed STOR/RETR, and GridFTP control semantics.

## Out Of Scope

- No raw FTP STOR/RETR.
- No TLS/GSI, MLST/MLSD, Mode E, SPAS/SPOR, or third-party server-to-server transfer.
- No default change to `verified_chunks`, `preallocate`, file IO buffer, or advice in Phase 4E.

</details>
