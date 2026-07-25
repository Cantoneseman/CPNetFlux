# Phase 4K POSIX Writeback Optimization

## Executive Summary

- Storage bench and GridFTP-like private matrix both completed with zero failures.
- No POSIX write strategy met the future-default gate across both STOR and RETR.
- STOR with `crc32c` saw a localized median gain around `+10%` for 256KiB coalescing, but the same family regressed in `checksum=none` and did not generalize.
- RETR showed several opt-in improvements for `crc32c`, but `checksum=none` frequently regressed and min/max spread remained large.
- Default remains `posix_write_strategy=auto` with `file_io_buffer_size=0`; `direct` and `coalesced` stay opt-in diagnostics.
- Reliability defaults are unchanged: POSIX backend, `preallocate=off`, `final_verify_policy=full`, `manifest_flush_policy=every_n_chunks`, `commit_sync_policy=none`.

## Inputs

- `tools/perf/results/20260517T164727Z_storage-bench-summary.csv`
- `tools/perf/results/20260517T171606Z_gridftp-private-matrix-smoke-summary.csv`

## Storage Bench Median

| case | repeat/cases | fail | median Gbps | vs baseline | min | max | temp/write s | write syscalls | avg bytes/syscall |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| read strategy=auto->direct fiobuf=0 | 3 | 0 | 78.844 | +12.5% | 66.417400 | 80.211400 |  | 0.000000 | 0.000000 |
| read strategy=auto->direct fiobuf=0 | 3 | 0 | 59.146 | -18.7% | 0.928322 | 79.621200 |  | 0.000000 | 0.000000 |
| read strategy=auto->direct fiobuf=0 | 3 | 0 | 70.072 | +0.0% | 60.553900 | 73.778700 |  | 0.000000 | 0.000000 |
| read strategy=auto->direct fiobuf=0 | 3 | 0 | 72.766 | +0.0% | 62.367200 | 75.752600 |  | 0.000000 | 0.000000 |
| read strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 78.944 | +12.7% | 65.743100 | 81.986900 |  | 0.000000 | 0.000000 |
| read strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 81.748 | +12.3% | 67.026700 | 84.073300 |  | 0.000000 | 0.000000 |
| read strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 70.646 | +0.8% | 60.375300 | 73.284700 |  | 0.000000 | 0.000000 |
| read strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 72.110 | -0.9% | 61.824700 | 74.686200 |  | 0.000000 | 0.000000 |
| read strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 80.872 | +15.4% | 66.158800 | 82.808500 |  | 0.000000 | 0.000000 |
| read strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 77.664 | +6.7% | 66.213500 | 79.367200 |  | 0.000000 | 0.000000 |
| read strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 70.968 | +1.3% | 60.365200 | 73.734000 |  | 0.000000 | 0.000000 |
| read strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 72.291 | -0.7% | 61.956900 | 75.968400 |  | 0.000000 | 0.000000 |
| read strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 80.966 | +15.5% | 66.003900 | 82.743100 |  | 0.000000 | 0.000000 |
| read strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 82.873 | +13.9% | 67.089200 | 84.372900 |  | 0.000000 | 0.000000 |
| read strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 70.486 | +0.6% | 60.405700 | 73.973400 |  | 0.000000 | 0.000000 |
| read strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 71.775 | -1.4% | 61.763900 | 74.634600 |  | 0.000000 | 0.000000 |
| read strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 80.227 | +14.5% | 66.459100 | 83.471000 |  | 0.000000 | 0.000000 |
| read strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 74.378 | +2.2% | 66.538200 | 79.448600 |  | 0.000000 | 0.000000 |
| read strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 69.963 | -0.2% | 61.301700 | 75.305000 |  | 0.000000 | 0.000000 |
| read strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 72.742 | -0.0% | 62.403800 | 74.881800 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=0 | 3 | 0 | 74.510 | +6.3% | 66.888500 | 83.152500 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=0 | 3 | 0 | 79.453 | +9.2% | 66.277900 | 82.378900 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=0 | 3 | 0 | 70.975 | +1.3% | 60.581700 | 74.629000 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=0 | 3 | 0 | 72.039 | -1.0% | 62.606200 | 75.237100 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=1048576 | 3 | 0 | 80.358 | +14.7% | 65.641300 | 82.028400 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=1048576 | 3 | 0 | 82.075 | +12.8% | 67.425000 | 84.190500 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=1048576 | 3 | 0 | 70.511 | +0.6% | 60.144100 | 73.177800 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=1048576 | 3 | 0 | 72.010 | -1.0% | 62.612200 | 75.315500 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=262144 | 3 | 0 | 80.277 | +14.6% | 66.365300 | 81.744200 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=262144 | 3 | 0 | 80.106 | +10.1% | 65.968500 | 82.606300 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=262144 | 3 | 0 | 70.539 | +0.7% | 59.823600 | 73.020800 |  | 0.000000 | 0.000000 |
| read strategy=direct->direct fiobuf=262144 | 3 | 0 | 72.473 | -0.4% | 62.274000 | 75.558700 |  | 0.000000 | 0.000000 |
| rewrite strategy=auto->direct fiobuf=0 | 3 | 0 | 0.928 | -2.5% | 0.906868 | 1.252220 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=auto->direct fiobuf=0 | 3 | 0 | 0.916 | -3.1% | 0.889601 | 1.328620 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=auto->direct fiobuf=0 | 3 | 0 | 0.952 | +0.0% | 0.917743 | 1.389430 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=auto->direct fiobuf=0 | 3 | 0 | 0.945 | +0.0% | 0.895549 | 2.521220 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.911 | -4.2% | 0.908825 | 1.278340 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.930 | -1.6% | 0.915763 | 1.284340 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.922 | -3.1% | 0.920086 | 1.069690 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.923 | -2.3% | 0.920880 | 1.060690 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 0.946 | -0.6% | 0.907653 | 1.242070 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 0.917 | -3.0% | 0.906501 | 1.302260 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 0.920 | -3.4% | 0.917718 | 6.005730 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 0.956 | +1.2% | 0.886268 | 1.744860 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 0.937 | -1.5% | 0.934497 | 1.239870 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 0.942 | -0.3% | 0.913843 | 1.273290 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 0.911 | -4.3% | 0.907657 | 3.145490 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 0.923 | -2.4% | 0.918127 | 1.675450 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 0.927 | -2.6% | 0.922063 | 1.267960 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 0.934 | -1.2% | 0.911149 | 1.263470 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 0.920 | -3.3% | 0.897780 | 6.251010 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 0.922 | -2.5% | 0.918133 | 1.358490 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=direct->direct fiobuf=0 | 3 | 0 | 0.992 | +4.3% | 0.922815 | 1.135250 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=direct->direct fiobuf=0 | 3 | 0 | 0.952 | +0.7% | 0.905734 | 1.263550 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=direct->direct fiobuf=0 | 3 | 0 | 0.968 | +1.7% | 0.862012 | 3.844650 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=direct->direct fiobuf=0 | 3 | 0 | 0.919 | -2.8% | 0.916182 | 2.705280 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=direct->direct fiobuf=1048576 | 3 | 0 | 0.920 | -3.4% | 0.890411 | 1.296940 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=direct->direct fiobuf=1048576 | 3 | 0 | 0.923 | -2.4% | 0.912678 | 1.301780 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=direct->direct fiobuf=1048576 | 3 | 0 | 0.944 | -0.8% | 0.920478 | 1.788740 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=direct->direct fiobuf=1048576 | 3 | 0 | 0.921 | -2.6% | 0.917739 | 1.347420 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=direct->direct fiobuf=262144 | 3 | 0 | 0.928 | -2.5% | 0.911095 | 1.250330 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=direct->direct fiobuf=262144 | 3 | 0 | 0.924 | -2.2% | 0.913450 | 1.229700 |  | 4096.000000 | 262144.000000 |
| rewrite strategy=direct->direct fiobuf=262144 | 3 | 0 | 0.922 | -3.1% | 0.919289 | 1.834980 |  | 1024.000000 | 1048580.000000 |
| rewrite strategy=direct->direct fiobuf=262144 | 3 | 0 | 0.922 | -2.5% | 0.918127 | 2.413570 |  | 4096.000000 | 262144.000000 |
| write strategy=auto->direct fiobuf=0 | 3 | 0 | 0.923 | +0.2% | 0.907605 | 0.931541 |  | 1024.000000 | 1048580.000000 |
| write strategy=auto->direct fiobuf=0 | 3 | 0 | 0.923 | +0.2% | 0.909801 | 1.420510 |  | 4096.000000 | 262144.000000 |
| write strategy=auto->direct fiobuf=0 | 3 | 0 | 0.921 | +0.0% | 0.816076 | 1.936110 |  | 1024.000000 | 1048580.000000 |
| write strategy=auto->direct fiobuf=0 | 3 | 0 | 0.921 | +0.0% | 0.918673 | 3.557140 |  | 4096.000000 | 262144.000000 |
| write strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.924 | +0.2% | 0.923437 | 0.932423 |  | 1024.000000 | 1048580.000000 |
| write strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.909 | -1.3% | 0.898749 | 0.914859 |  | 1024.000000 | 1048580.000000 |
| write strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.934 | +1.4% | 0.925148 | 2.496920 |  | 1024.000000 | 1048580.000000 |
| write strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.928 | +0.8% | 0.927343 | 2.497350 |  | 1024.000000 | 1048580.000000 |
| write strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 0.921 | -0.0% | 0.911312 | 0.924295 |  | 1024.000000 | 1048580.000000 |
| write strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 0.920 | -0.2% | 0.908707 | 0.921636 |  | 4096.000000 | 262144.000000 |
| write strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 0.921 | -0.0% | 0.840237 | 2.128840 |  | 1024.000000 | 1048580.000000 |
| write strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 0.926 | +0.5% | 0.840999 | 2.417660 |  | 4096.000000 | 262144.000000 |
| write strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 0.923 | +0.2% | 0.922146 | 0.927298 |  | 1024.000000 | 1048580.000000 |
| write strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 0.918 | -0.4% | 0.904645 | 0.933648 |  | 1024.000000 | 1048580.000000 |
| write strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 0.919 | -0.3% | 0.819062 | 0.927487 |  | 1024.000000 | 1048580.000000 |
| write strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 0.939 | +1.9% | 0.926160 | 2.461570 |  | 1024.000000 | 1048580.000000 |
| write strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 0.922 | +0.0% | 0.899560 | 0.922871 |  | 1024.000000 | 1048580.000000 |
| write strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 0.918 | -0.4% | 0.906192 | 0.923833 |  | 4096.000000 | 262144.000000 |
| write strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 0.927 | +0.6% | 0.808409 | 2.086030 |  | 1024.000000 | 1048580.000000 |
| write strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 0.926 | +0.5% | 0.766754 | 0.936966 |  | 4096.000000 | 262144.000000 |
| write strategy=direct->direct fiobuf=0 | 3 | 0 | 0.924 | +0.3% | 0.912970 | 0.929251 |  | 1024.000000 | 1048580.000000 |
| write strategy=direct->direct fiobuf=0 | 3 | 0 | 0.927 | +0.6% | 0.910639 | 0.939109 |  | 4096.000000 | 262144.000000 |
| write strategy=direct->direct fiobuf=0 | 3 | 0 | 0.929 | +0.8% | 0.914168 | 2.318410 |  | 1024.000000 | 1048580.000000 |
| write strategy=direct->direct fiobuf=0 | 3 | 0 | 0.922 | +0.0% | 0.818166 | 0.937834 |  | 4096.000000 | 262144.000000 |
| write strategy=direct->direct fiobuf=1048576 | 3 | 0 | 0.921 | -0.1% | 0.920256 | 0.922272 |  | 1024.000000 | 1048580.000000 |
| write strategy=direct->direct fiobuf=1048576 | 3 | 0 | 0.913 | -0.9% | 0.897434 | 0.925790 |  | 4096.000000 | 262144.000000 |
| write strategy=direct->direct fiobuf=1048576 | 3 | 0 | 0.930 | +1.0% | 0.919292 | 2.574580 |  | 1024.000000 | 1048580.000000 |
| write strategy=direct->direct fiobuf=1048576 | 3 | 0 | 0.923 | +0.2% | 0.800634 | 2.048060 |  | 4096.000000 | 262144.000000 |
| write strategy=direct->direct fiobuf=262144 | 3 | 0 | 0.920 | -0.1% | 0.911361 | 0.922955 |  | 1024.000000 | 1048580.000000 |
| write strategy=direct->direct fiobuf=262144 | 3 | 0 | 0.934 | +1.4% | 0.900967 | 0.940890 |  | 4096.000000 | 262144.000000 |
| write strategy=direct->direct fiobuf=262144 | 3 | 0 | 0.933 | +1.3% | 0.920034 | 2.391970 |  | 1024.000000 | 1048580.000000 |
| write strategy=direct->direct fiobuf=262144 | 3 | 0 | 0.922 | +0.0% | 0.851540 | 0.933614 |  | 4096.000000 | 262144.000000 |


## GridFTP-like Private Matrix Median

| case | repeat/cases | fail | median Gbps | vs baseline | min | max | temp/write s | write syscalls | avg bytes/syscall |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| retr crc32c strategy=auto->direct fiobuf=0 | 3 | 0 | 3.215 | +0.0% | 2.807650 | 4.352120 | 2.886 | 4096.000000 | 262144.000000 |
| retr crc32c strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 3.040 | -5.4% | 2.925230 | 4.236530 | 5.698 | 1024.000000 | 1048580.000000 |
| retr crc32c strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 3.470 | +7.9% | 2.911880 | 3.621280 | 4.482 | 4096.000000 | 262144.000000 |
| retr crc32c strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 3.565 | +10.9% | 3.086580 | 4.390220 | 4.418 | 1024.000000 | 1048580.000000 |
| retr crc32c strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 3.518 | +9.4% | 2.797650 | 4.098880 | 5.993 | 4096.000000 | 262144.000000 |
| retr crc32c strategy=direct->direct fiobuf=0 | 3 | 0 | 3.542 | +10.2% | 3.123840 | 3.774010 | 4.812 | 4096.000000 | 262144.000000 |
| retr crc32c strategy=direct->direct fiobuf=1048576 | 3 | 0 | 3.701 | +15.1% | 2.865830 | 3.710210 | 4.828 | 4096.000000 | 262144.000000 |
| retr crc32c strategy=direct->direct fiobuf=262144 | 3 | 0 | 3.438 | +6.9% | 3.045200 | 3.501020 | 4.588 | 4096.000000 | 262144.000000 |
| retr none strategy=auto->direct fiobuf=0 | 3 | 0 | 4.457 | +0.0% | 4.006820 | 4.805510 | 7.811 | 4096.000000 | 262144.000000 |
| retr none strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 3.552 | -20.3% | 2.850230 | 4.802440 | 5.210 | 1024.000000 | 1048580.000000 |
| retr none strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 3.843 | -13.8% | 3.061900 | 4.422360 | 5.364 | 4096.000000 | 262144.000000 |
| retr none strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 3.804 | -14.7% | 3.145410 | 3.815620 | 3.432 | 1024.000000 | 1048580.000000 |
| retr none strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 3.782 | -15.2% | 3.766910 | 3.896770 | 5.147 | 4096.000000 | 262144.000000 |
| retr none strategy=direct->direct fiobuf=0 | 3 | 0 | 4.285 | -3.9% | 3.109740 | 4.851110 | 4.569 | 4096.000000 | 262144.000000 |
| retr none strategy=direct->direct fiobuf=1048576 | 3 | 0 | 3.490 | -21.7% | 2.442720 | 3.879620 | 5.073 | 4096.000000 | 262144.000000 |
| retr none strategy=direct->direct fiobuf=262144 | 3 | 0 | 3.402 | -23.7% | 3.286230 | 3.660610 | 5.776 | 4096.000000 | 262144.000000 |
| stor crc32c strategy=auto->direct fiobuf=0 | 3 | 0 | 1.000 | +0.0% | 0.989468 | 1.298520 | 5.629 | 4096.000000 | 262144.000000 |
| stor crc32c strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 0.997 | -0.3% | 0.987447 | 1.079880 | 5.424 | 1024.000000 | 1048580.000000 |
| stor crc32c strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 1.103 | +10.3% | 1.003500 | 1.362230 | 5.505 | 4096.000000 | 262144.000000 |
| stor crc32c strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 1.023 | +2.3% | 1.009030 | 1.121040 | 5.464 | 1024.000000 | 1048580.000000 |
| stor crc32c strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 1.103 | +10.2% | 1.087440 | 1.113810 | 5.679 | 4096.000000 | 262144.000000 |
| stor crc32c strategy=direct->direct fiobuf=0 | 3 | 0 | 1.007 | +0.7% | 0.996705 | 1.321730 | 5.204 | 4096.000000 | 262144.000000 |
| stor crc32c strategy=direct->direct fiobuf=1048576 | 3 | 0 | 1.073 | +7.3% | 1.006710 | 1.145900 | 5.306 | 4096.000000 | 262144.000000 |
| stor crc32c strategy=direct->direct fiobuf=262144 | 3 | 0 | 1.073 | +7.2% | 0.898813 | 1.091330 | 5.408 | 4096.000000 | 262144.000000 |
| stor none strategy=auto->direct fiobuf=0 | 3 | 0 | 1.489 | +0.0% | 1.390910 | 1.501760 | 5.410 | 4096.000000 | 262144.000000 |
| stor none strategy=auto->coalesced fiobuf=1048576 | 3 | 0 | 1.417 | -4.9% | 1.382890 | 1.493170 | 5.485 | 1024.000000 | 1048580.000000 |
| stor none strategy=auto->coalesced fiobuf=262144 | 3 | 0 | 1.439 | -3.4% | 1.424860 | 1.472250 | 5.473 | 4096.000000 | 262144.000000 |
| stor none strategy=coalesced->coalesced fiobuf=1048576 | 3 | 0 | 1.354 | -9.1% | 1.305710 | 1.484770 | 5.421 | 1024.000000 | 1048580.000000 |
| stor none strategy=coalesced->coalesced fiobuf=262144 | 3 | 0 | 1.316 | -11.7% | 1.308070 | 1.393030 | 5.551 | 4096.000000 | 262144.000000 |
| stor none strategy=direct->direct fiobuf=0 | 3 | 0 | 1.493 | +0.2% | 1.385120 | 1.495240 | 5.510 | 4096.000000 | 262144.000000 |
| stor none strategy=direct->direct fiobuf=1048576 | 3 | 0 | 1.407 | -5.5% | 1.336280 | 1.523650 | 5.418 | 4096.000000 | 262144.000000 |
| stor none strategy=direct->direct fiobuf=262144 | 3 | 0 | 1.384 | -7.1% | 1.334710 | 1.394700 | 5.229 | 4096.000000 | 262144.000000 |


## Gate Conclusion

- Defaults remain unchanged in Phase 4K: POSIX backend, `posix_write_strategy=auto`, `file_io_buffer_size=0`, full final verify, every-16 manifest flush.
- A strategy should only be considered for a future default if private STOR and RETR medians improve by >=10%, variance is not worse, and write syscall metrics explain the gain.
- Best observed opt-in candidate is `retr crc32c strategy=direct->direct fiobuf=1048576` at 15.1% over its baseline; keep opt-in until both directions satisfy the default gate.
- Failed grouped rows: 0.

## Non-Goals Preserved

- No raw FTP STOR/RETR, no network io_uring, and no default io_uring.
- No default preallocate full, verified_chunks, final_only, or commit fsync.
- No checksum, manifest, resume, or final verify semantic changes.
