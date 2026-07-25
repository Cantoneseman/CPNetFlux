# Phase 5B Tree Dataset Matrix

## Inputs

- `tools/perf/results/20260518T071018Z_gridftp-tree-private-matrix-summary.csv`

## Median Summary

| dataset | direction | resume | checksum | file parallelism | repeat | fail | hash mismatch | files | bytes | median Gbps | min Gbps | max Gbps | median seconds |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| mixed | download | 0 | crc32c | 1 | 3 | 0 | 0 | 49 | 79750738 | 0.266167 | 0.263096 | 0.266187 | 2.397010 |
| mixed | download | 0 | crc32c | 2 | 3 | 0 | 0 | 49 | 79750738 | 0.487340 | 0.487337 | 0.491863 | 1.309160 |
| mixed | download | 0 | crc32c | 4 | 3 | 0 | 0 | 49 | 79750738 | 0.833951 | 0.808582 | 0.842795 | 0.765040 |
| mixed | download | 0 | none | 1 | 3 | 0 | 0 | 49 | 79750738 | 0.267951 | 0.267494 | 0.268403 | 2.381050 |
| mixed | download | 0 | none | 2 | 3 | 0 | 0 | 49 | 79750738 | 0.488943 | 0.488840 | 0.495215 | 1.304870 |
| mixed | download | 0 | none | 4 | 3 | 0 | 0 | 49 | 79750738 | 0.829606 | 0.825298 | 0.842661 | 0.769047 |
| mixed | upload | 0 | crc32c | 1 | 3 | 0 | 0 | 49 | 79750738 | 0.286074 | 0.285575 | 0.287637 | 2.230210 |
| mixed | upload | 0 | crc32c | 2 | 3 | 0 | 0 | 49 | 79750738 | 0.568577 | 0.566509 | 0.570668 | 1.122110 |
| mixed | upload | 0 | crc32c | 4 | 3 | 0 | 0 | 49 | 79750738 | 1.073540 | 1.045460 | 1.096100 | 0.594300 |
| mixed | upload | 0 | none | 1 | 3 | 0 | 0 | 49 | 79750738 | 0.288154 | 0.283041 | 0.289184 | 2.214120 |
| mixed | upload | 0 | none | 2 | 3 | 0 | 0 | 49 | 79750738 | 0.562555 | 0.556691 | 0.569150 | 1.134120 |
| mixed | upload | 0 | none | 4 | 3 | 0 | 0 | 49 | 79750738 | 1.088320 | 1.075890 | 1.088520 | 0.586229 |


## Recommendation

- Defaults remain unchanged: file-level parallelism defaults to 1 and single-file transfer defaults stay POSIX/full-verify/every_n_chunks.
- Directory transfer remains alpha orchestration, not rsync: no permissions, owner, xattr, ACL, or empty-directory preservation.
- Best passing download row in this input: dataset=mixed checksum=crc32c file_parallelism=4 median=0.833951 Gbps. Treat as opt-in guidance only.
- Best passing upload row in this input: dataset=mixed checksum=none file_parallelism=4 median=1.088320 Gbps. Treat as opt-in guidance only.
- All grouped rows passed with matching tree hashes in the supplied summaries.

## Boundaries

- No raw FTP recursive transfer, no MLST/MLSD, no TLS/GSI/production auth, and no third-party transfer.
- Each file still uses the existing GridFlux framed STOR/RETR path and per-file manifest/verified_chunks semantics.
- Changed-file handling remains fail-safe: changed files are marked and the transfer exits nonzero.
