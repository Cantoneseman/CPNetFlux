# Phase 4J POSIX Pipeline Diagnosis

## Inputs

- `tools/perf/results/20260517T154238Z_gridftp-private-matrix-smoke-summary.csv`
- `tools/perf/results/20260517T160524Z_gridftp-private-matrix-smoke-summary.csv`

## Median Summary

| case | repeat | fail | median Gbps | vs baseline | elapsed s | dominant stage | dominant share of measured stages |
| --- | --- | --- | --- | --- | --- | --- | --- |
| retr crc32c flush=every_n_chunks/16 fv=full->full fiobuf=262144 | 3 | 0 | 3.656 | +0.0% | 2.349370 | network send | 63.7% |
| retr crc32c flush=every_n_chunks/16 fv=verified_chunks->verified_chunks fiobuf=262144 | 3 | 0 | 3.442 | -5.9% | 2.495350 | network send | 63.6% |
| retr crc32c flush=every_n_chunks/256 fv=full->full fiobuf=262144 | 3 | 0 | 4.146 | +13.4% | 2.071760 | download write | 52.3% |
| retr crc32c flush=every_n_chunks/256 fv=verified_chunks->verified_chunks fiobuf=262144 | 3 | 0 | 4.170 | +14.1% | 2.059730 | download write | 54.6% |
| retr crc32c flush=final_only/16 fv=full->full fiobuf=262144 | 3 | 0 | 3.735 | +2.1% | 2.300050 | download write | 52.7% |
| retr crc32c flush=final_only/16 fv=verified_chunks->verified_chunks fiobuf=262144 | 3 | 0 | 4.337 | +18.6% | 1.980700 | download write | 53.1% |
| retr crc32c flush=final_only/256 fv=full->full fiobuf=262144 | 3 | 0 | 3.471 | -5.1% | 2.475020 | download write | 51.6% |
| retr crc32c flush=final_only/256 fv=verified_chunks->verified_chunks fiobuf=262144 | 3 | 0 | 4.577 | +25.2% | 1.876780 | download write | 52.7% |
| retr none flush=every_n_chunks/16 fv=full->full fiobuf=262144 | 3 | 0 | 3.591 | +0.0% | 2.392230 | network send | 65.2% |
| retr none flush=every_n_chunks/16 fv=verified_chunks->full fiobuf=262144 | 3 | 0 | 3.397 | -5.4% | 2.529010 | network send | 61.4% |
| retr none flush=every_n_chunks/256 fv=full->full fiobuf=262144 | 3 | 0 | 3.929 | +9.4% | 2.186270 | download write | 53.9% |
| retr none flush=every_n_chunks/256 fv=verified_chunks->full fiobuf=262144 | 3 | 0 | 3.899 | +8.6% | 2.203110 | download write | 55.1% |
| retr none flush=final_only/16 fv=full->full fiobuf=262144 | 3 | 0 | 4.140 | +15.3% | 2.074770 | download write | 54.6% |
| retr none flush=final_only/16 fv=verified_chunks->full fiobuf=262144 | 3 | 0 | 4.488 | +25.0% | 1.914190 | download write | 54.2% |
| retr none flush=final_only/256 fv=full->full fiobuf=262144 | 3 | 0 | 4.247 | +18.3% | 2.022360 | download write | 53.3% |
| retr none flush=final_only/256 fv=verified_chunks->full fiobuf=262144 | 3 | 0 | 4.203 | +17.1% | 2.043650 | download write | 53.6% |
| stor crc32c flush=every_n_chunks/16 fv=full->full fiobuf=262144 | 3 | 0 | 1.406 | +0.0% | 6.109060 | temp write | 89.7% |
| stor crc32c flush=every_n_chunks/16 fv=verified_chunks->verified_chunks fiobuf=262144 | 3 | 0 | 1.453 | +3.3% | 5.911130 | temp write | 89.3% |
| stor crc32c flush=every_n_chunks/256 fv=full->full fiobuf=262144 | 3 | 0 | 1.275 | -9.3% | 6.738830 | temp write | 84.0% |
| stor crc32c flush=every_n_chunks/256 fv=verified_chunks->verified_chunks fiobuf=262144 | 3 | 0 | 1.444 | +2.7% | 5.947430 | temp write | 94.6% |
| stor crc32c flush=final_only/16 fv=full->full fiobuf=262144 | 3 | 0 | 1.141 | -18.9% | 7.528660 | temp write | 73.0% |
| stor crc32c flush=final_only/16 fv=verified_chunks->verified_chunks fiobuf=262144 | 3 | 0 | 1.424 | +1.3% | 6.033080 | temp write | 94.0% |
| stor crc32c flush=final_only/256 fv=full->full fiobuf=262144 | 3 | 0 | 1.409 | +0.2% | 6.096050 | temp write | 90.8% |
| stor crc32c flush=final_only/256 fv=verified_chunks->verified_chunks fiobuf=262144 | 3 | 0 | 1.448 | +3.0% | 5.930500 | temp write | 94.9% |
| stor none flush=every_n_chunks/16 fv=full->full fiobuf=262144 | 3 | 0 | 1.419 | +0.0% | 6.053850 | temp write | 91.5% |
| stor none flush=every_n_chunks/16 fv=verified_chunks->full fiobuf=262144 | 3 | 0 | 1.427 | +0.6% | 6.019790 | temp write | 93.9% |
| stor none flush=every_n_chunks/256 fv=full->full fiobuf=262144 | 3 | 0 | 1.335 | -5.9% | 6.436480 | temp write | 89.7% |
| stor none flush=every_n_chunks/256 fv=verified_chunks->full fiobuf=262144 | 3 | 0 | 1.395 | -1.7% | 6.155870 | temp write | 96.3% |
| stor none flush=final_only/16 fv=full->full fiobuf=0 | 3 | 0 | 1.350 | -4.8% | 6.361390 | temp write | 86.7% |
| stor none flush=final_only/16 fv=full->full fiobuf=1048576 | 3 | 0 | 1.423 | +0.3% | 6.035620 | temp write | 96.5% |
| stor none flush=final_only/16 fv=full->full fiobuf=262144 | 3 | 0 | 1.408 | -0.7% | 6.099070 | temp write | 97.7% |
| stor none flush=final_only/16 fv=full->full fiobuf=262144 | 3 | 0 | 1.408 | -0.8% | 6.101140 | temp write | 97.7% |
| stor none flush=final_only/16 fv=full->full fiobuf=4194304 | 3 | 0 | 1.305 | -8.0% | 6.583340 | temp write | 83.5% |
| stor none flush=final_only/16 fv=verified_chunks->full fiobuf=262144 | 3 | 0 | 1.370 | -3.4% | 6.268580 | temp write | 91.4% |
| stor none flush=final_only/256 fv=full->full fiobuf=262144 | 3 | 0 | 1.417 | -0.1% | 6.062040 | temp write | 94.2% |
| stor none flush=final_only/256 fv=verified_chunks->full fiobuf=262144 | 3 | 0 | 1.396 | -1.6% | 6.153830 | temp write | 91.7% |

## Best Passing Stage Breakdown

| direction | case | median Gbps | elapsed s | stage shares |
| --- | --- | --- | --- | --- |
| retr | retr crc32c flush=final_only/256 fv=verified_chunks->verified_chunks fiobuf=262144 | 4.576960 | 1.876780 | sender read=0.166s/0.8%, network send=9.862s/45.9%, sender checksum=0.125s/0.6%, download write=11.313s/52.7%, receiver manifest=0.008s/0.0%, receiver finalize=0.001s/0.0% |
| stor | stor crc32c flush=every_n_chunks/16 fv=verified_chunks->verified_chunks fiobuf=262144 | 1.453180 | 5.911130 | data receive=0.090s/1.5%, temp write=5.294s/89.3%, checksum=0.125s/2.1%, manifest=0.400s/6.7%, finalize=0.023s/0.4% |

## Data Quality

- Failed grouped rows: 0
- High-variance grouped rows (max/min > 1.5): 2

## Gate Conclusion

- Defaults remain unchanged: POSIX backend, full final verify, preallocate off, every 16 chunk manifest flush.
- Keep `verified_chunks`, `final_only`, and commit fsync modes opt-in until the private median data is reviewed case by case.
- STOR: dominant median measured stage is `temp write` at 89.3% of measured stage time; prioritize that path next.
- RETR: dominant median measured stage is `download write` at 52.7% of measured stage time; prioritize that path next.

## Non-Goals Preserved

- No raw FTP STOR/RETR.
- No default io_uring, preallocate full, or verified_chunks.
- No change to checksum, manifest, resume, final verify, or framed data semantics.
