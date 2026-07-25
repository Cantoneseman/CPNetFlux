# Phase 4L Stability and RETR Breakdown

## Inputs

- `tools/perf/results/20260518T004459Z_gridftp-private-matrix-smoke-summary.csv`

## Executive Summary

- Summary rows: `48`; grouped fail count: `0`.
- High spread rows (`>20%`): `21`.
- Stage/throughput mismatch rows: `4`.
- Defaults remain unchanged: POSIX backend, `posix_write_strategy=auto`, `file_io_buffer_size=0`, full final verify, every-16 manifest flush, no commit fsync.
- Opt-in recommendations below are documentation-only; no runtime defaults are changed.
- STOR percentages use wall-clock elapsed time. RETR sender/receiver stage times can be connection-accumulated on different sides, so RETR percentages are shares of listed key stages rather than wall-clock percentages.

## STOR Top Bottleneck Table

| case | median Gbps | spread | elapsed | temp write | checksum | manifest | final verify | unstable |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stor none fv=verified_chunks->full mfp=final_only pws=auto->direct fiobuf=0 | 1.399 | 15.3% | 6.141 | 6.010 (97.9% wall) | 0.000 (0.0% wall) | 0.074 (1.2% wall) | 0.000 (0.0% wall) | 0 |
| stor none fv=verified_chunks->full mfp=final_only pws=auto->coalesced fiobuf=262144 | 1.408 | 9.3% | 6.100 | 5.965 (97.8% wall) | 0.000 (0.0% wall) | 0.168 (2.8% wall) | 0.000 (0.0% wall) | 0 |
| stor none fv=full->full mfp=final_only pws=auto->coalesced fiobuf=262144 | 1.386 | 9.4% | 6.197 | 5.902 (95.2% wall) | 0.000 (0.0% wall) | 0.102 (1.6% wall) | 0.000 (0.0% wall) | 0 |
| stor none fv=full->full mfp=final_only pws=auto->direct fiobuf=0 | 1.428 | 3.1% | 6.017 | 5.810 (96.6% wall) | 0.000 (0.0% wall) | 0.105 (1.8% wall) | 0.000 (0.0% wall) | 0 |
| stor none fv=verified_chunks->full mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 1.454 | 6.2% | 5.906 | 5.743 (97.2% wall) | 0.000 (0.0% wall) | 0.036 (0.6% wall) | 0.000 (0.0% wall) | 0 |
| stor none fv=full->full mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 1.413 | 7.5% | 6.081 | 5.737 (94.3% wall) | 0.000 (0.0% wall) | 0.173 (2.9% wall) | 0.000 (0.0% wall) | 0 |
| stor none fv=verified_chunks->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 1.313 | 14.0% | 6.541 | 5.663 (86.6% wall) | 0.000 (0.0% wall) | 0.697 (10.7% wall) | 0.000 (0.0% wall) | 0 |
| stor crc32c fv=full->full mfp=final_only pws=auto->direct fiobuf=0 | 1.080 | 59.3% | 7.957 | 5.658 (71.1% wall) | 0.129 (1.6% wall) | 0.001 (0.0% wall) | 2.121 (26.7% wall) | 1 |
| stor crc32c fv=full->full mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 1.362 | 38.2% | 6.306 | 5.634 (89.4% wall) | 0.126 (2.0% wall) | 0.005 (0.1% wall) | 0.510 (8.1% wall) | 1 |
| stor none fv=full->full mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 1.350 | 8.4% | 6.365 | 5.621 (88.3% wall) | 0.000 (0.0% wall) | 0.590 (9.3% wall) | 0.000 (0.0% wall) | 0 |
| stor crc32c fv=verified_chunks->verified_chunks mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 1.446 | 5.2% | 5.941 | 5.620 (94.6% wall) | 0.126 (2.1% wall) | 0.086 (1.4% wall) | 0.000 (0.0% wall) | 0 |
| stor crc32c fv=verified_chunks->verified_chunks mfp=final_only pws=auto->direct fiobuf=0 | 1.455 | 9.8% | 5.905 | 5.614 (95.1% wall) | 0.128 (2.2% wall) | 0.074 (1.3% wall) | 0.000 (0.0% wall) | 0 |
| stor none fv=full->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 1.353 | 11.4% | 6.348 | 5.598 (88.2% wall) | 0.000 (0.0% wall) | 0.570 (9.0% wall) | 0.000 (0.0% wall) | 0 |
| stor crc32c fv=full->full mfp=final_only pws=auto->coalesced fiobuf=262144 | 1.087 | 51.4% | 7.901 | 5.591 (70.8% wall) | 0.125 (1.6% wall) | 0.001 (0.0% wall) | 1.880 (23.8% wall) | 1 |
| stor crc32c fv=verified_chunks->verified_chunks mfp=final_only pws=auto->coalesced fiobuf=262144 | 1.434 | 2.7% | 5.990 | 5.585 (93.2% wall) | 0.126 (2.1% wall) | 0.091 (1.5% wall) | 0.000 (0.0% wall) | 0 |
| stor none fv=verified_chunks->full mfp=every_n_chunks pws=auto->direct fiobuf=0 | 1.379 | 12.2% | 6.230 | 5.570 (89.4% wall) | 0.000 (0.0% wall) | 0.399 (6.4% wall) | 0.000 (0.0% wall) | 0 |


## RETR Sender / Receiver Breakdown

| case | median Gbps | spread | elapsed | sender send | receiver write | source read | final verify | next focus |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| retr crc32c fv=verified_chunks->verified_chunks mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 3.320 | 46.3% | 2.587 | 14.484 (70.5% key) | 5.891 (28.7% key) | 0.167 (0.8% key) | 0.000 (0.0% key) | sender network send |
| retr none fv=full->full mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 3.917 | 19.9% | 2.193 | 13.258 (71.6% key) | 5.104 (27.6% key) | 0.161 (0.9% key) | 0.000 (0.0% key) | sender network send |
| retr none fv=full->full mfp=every_n_chunks pws=auto->direct fiobuf=0 | 4.100 | 38.5% | 2.095 | 13.151 (73.7% key) | 4.538 (25.4% key) | 0.160 (0.9% key) | 0.000 (0.0% key) | sender network send |
| retr none fv=verified_chunks->full mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 3.369 | 55.9% | 2.550 | 13.041 (71.2% key) | 5.106 (27.9% key) | 0.166 (0.9% key) | 0.000 (0.0% key) | sender network send |
| retr crc32c fv=full->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 3.614 | 31.8% | 2.377 | 12.329 (65.3% key) | 6.129 (32.4% key) | 0.165 (0.9% key) | 0.269 (1.4% key) | sender network send |
| retr none fv=full->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 3.694 | 48.0% | 2.325 | 12.169 (71.7% key) | 4.635 (27.3% key) | 0.164 (1.0% key) | 0.000 (0.0% key) | sender network send |
| retr crc32c fv=verified_chunks->verified_chunks mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 3.907 | 46.4% | 2.199 | 11.879 (68.3% key) | 5.344 (30.7% key) | 0.164 (0.9% key) | 0.000 (0.0% key) | sender network send |
| retr crc32c fv=full->full mfp=final_only pws=auto->coalesced fiobuf=262144 | 3.812 | 15.8% | 2.253 | 10.041 (45.2% key) | 11.793 (53.1% key) | 0.166 (0.7% key) | 0.221 (1.0% key) | receiver download write |
| retr crc32c fv=verified_chunks->verified_chunks mfp=final_only pws=auto->direct fiobuf=0 | 3.199 | 33.4% | 2.685 | 9.387 (44.1% key) | 11.738 (55.1% key) | 0.169 (0.8% key) | 0.000 (0.0% key) | receiver download write |
| retr none fv=verified_chunks->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 3.703 | 28.8% | 2.320 | 11.418 (70.9% key) | 4.535 (28.2% key) | 0.157 (1.0% key) | 0.000 (0.0% key) | sender network send |
| retr crc32c fv=full->full mfp=final_only pws=auto->direct fiobuf=0 | 3.627 | 12.6% | 2.368 | 9.417 (45.3% key) | 10.989 (52.8% key) | 0.171 (0.8% key) | 0.222 (1.1% key) | receiver download write |
| retr crc32c fv=full->full mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 3.846 | 21.2% | 2.233 | 10.948 (61.7% key) | 6.323 (35.6% key) | 0.180 (1.0% key) | 0.292 (1.6% key) | sender network send |
| retr crc32c fv=full->full mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 3.684 | 23.2% | 2.332 | 9.379 (45.2% key) | 10.946 (52.8% key) | 0.162 (0.8% key) | 0.260 (1.3% key) | receiver download write |
| retr crc32c fv=verified_chunks->verified_chunks mfp=every_n_chunks pws=auto->direct fiobuf=0 | 3.362 | 23.6% | 2.555 | 10.886 (62.5% key) | 6.350 (36.5% key) | 0.171 (1.0% key) | 0.000 (0.0% key) | sender network send |
| retr crc32c fv=full->full mfp=every_n_chunks pws=auto->direct fiobuf=0 | 3.324 | 16.6% | 2.584 | 10.884 (66.2% key) | 4.929 (30.0% key) | 0.235 (1.4% key) | 0.383 (2.3% key) | sender network send |
| retr none fv=verified_chunks->full mfp=final_only pws=auto->direct fiobuf=0 | 3.856 | 25.2% | 2.227 | 9.154 (45.4% key) | 10.863 (53.8% key) | 0.164 (0.8% key) | 0.000 (0.0% key) | receiver download write |


## High-Variance / Suspicious Rows

| case | median | min | max | spread | spread>20 | min/max | stage mismatch | fail |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stor crc32c fv=full->full mfp=final_only pws=auto->direct fiobuf=0 | 1.080 | 0.510 | 1.150 | 59.3% | 1 | 1 | 1 | 0 |
| retr none fv=verified_chunks->full mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 3.369 | 3.001 | 4.885 | 55.9% | 1 | 1 | 0 | 0 |
| stor crc32c fv=full->full mfp=final_only pws=auto->coalesced fiobuf=262144 | 1.087 | 0.604 | 1.164 | 51.4% | 1 | 1 | 0 | 0 |
| stor crc32c fv=full->full mfp=every_n_chunks pws=auto->direct fiobuf=0 | 1.001 | 0.598 | 1.083 | 48.5% | 1 | 1 | 0 | 0 |
| retr none fv=full->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 3.694 | 2.501 | 4.273 | 48.0% | 1 | 1 | 0 | 0 |
| retr crc32c fv=verified_chunks->verified_chunks mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 3.907 | 2.993 | 4.805 | 46.4% | 1 | 1 | 0 | 0 |
| retr crc32c fv=verified_chunks->verified_chunks mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 3.320 | 2.567 | 4.103 | 46.3% | 1 | 1 | 0 | 0 |
| stor crc32c fv=full->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 1.001 | 0.904 | 1.309 | 40.5% | 1 | 0 | 0 | 0 |
| retr crc32c fv=verified_chunks->verified_chunks mfp=final_only pws=auto->coalesced fiobuf=262144 | 4.197 | 3.137 | 4.774 | 39.0% | 1 | 1 | 0 | 0 |
| retr none fv=full->full mfp=every_n_chunks pws=auto->direct fiobuf=0 | 4.100 | 2.762 | 4.342 | 38.5% | 1 | 1 | 0 | 0 |
| stor crc32c fv=full->full mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 1.362 | 0.883 | 1.404 | 38.2% | 1 | 1 | 0 | 0 |
| retr crc32c fv=verified_chunks->verified_chunks mfp=final_only pws=auto->direct fiobuf=0 | 3.199 | 3.080 | 4.147 | 33.4% | 1 | 0 | 0 | 0 |
| retr crc32c fv=full->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 3.614 | 3.100 | 4.250 | 31.8% | 1 | 0 | 0 | 0 |
| retr none fv=full->full mfp=final_only pws=auto->direct fiobuf=0 | 3.916 | 3.551 | 4.739 | 30.3% | 1 | 0 | 0 | 0 |
| retr none fv=verified_chunks->full mfp=every_n_chunks pws=coalesced->coalesced fiobuf=262144 | 3.703 | 3.501 | 4.566 | 28.8% | 1 | 0 | 0 | 0 |
| stor crc32c fv=full->full mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 1.084 | 0.970 | 1.269 | 27.5% | 1 | 0 | 0 | 0 |
| retr none fv=verified_chunks->full mfp=final_only pws=auto->direct fiobuf=0 | 3.856 | 3.428 | 4.398 | 25.2% | 1 | 0 | 0 | 0 |
| retr crc32c fv=verified_chunks->verified_chunks mfp=every_n_chunks pws=auto->direct fiobuf=0 | 3.362 | 3.026 | 3.819 | 23.6% | 1 | 0 | 0 | 0 |
| retr crc32c fv=full->full mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 3.684 | 3.111 | 3.965 | 23.2% | 1 | 0 | 0 | 0 |
| retr none fv=full->full mfp=final_only pws=auto->coalesced fiobuf=262144 | 4.170 | 3.541 | 4.457 | 22.0% | 1 | 0 | 0 | 0 |
| retr crc32c fv=full->full mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 3.846 | 3.477 | 4.291 | 21.2% | 1 | 0 | 0 | 0 |
| retr none fv=full->full mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 4.010 | 3.899 | 4.249 | 8.7% | 0 | 0 | 1 | 0 |
| stor none fv=verified_chunks->full mfp=every_n_chunks pws=auto->coalesced fiobuf=262144 | 1.379 | 1.288 | 1.395 | 7.8% | 0 | 0 | 1 | 0 |
| stor crc32c fv=verified_chunks->verified_chunks mfp=final_only pws=coalesced->coalesced fiobuf=262144 | 1.446 | 1.414 | 1.489 | 5.2% | 0 | 0 | 1 | 0 |


## Opt-in Recommendation Matrix

| scenario | recommendation | reference median Gbps | spread | notes |
| --- | --- | --- | --- | --- |
| Default general | Keep defaults / no opt-in recommendation | 1.001 | 48.5% | Baseline is the current default row; Phase 4L does not change runtime defaults. |
| RETR + crc32c | Keep defaults / no opt-in recommendation | 4.197 | 39.0% | May document opt-in only if spread is acceptable and both sender/receiver remain correct. |
| checksum=none | Keep defaults / no opt-in recommendation | 4.170 | 22.0% | Performance comparison only; not a reliable resume recommendation. |
| Conservative recovery | Keep defaults / no opt-in recommendation | 1.001 | 48.5% | Keep full final verify and every_n_chunks manifest flush; opt-in verified_chunks/final_only is not a conservative default. |


## Gate Conclusion

- If STOR rows remain high-spread and temp write dominates, treat the source as storage/writeback or page-cache pressure before changing code defaults.
- If RETR sender network send dominates the best repeat-stable rows, next work should inspect send scheduling/backpressure before more receiver write tweaks.
- If receiver download write dominates RETR rows, keep using POSIX writeback diagnostics and consider storage-side opt-ins only per scenario.
- Do not default-enable `verified_chunks`, `final_only`, `coalesced`, preallocate full, commit fsync, or io_uring from Phase 4L data alone.
