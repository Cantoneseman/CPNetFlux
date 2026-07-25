# Phase 5C Tree Alpha Hardening

## Inputs

- `tools/perf/results/20260518T084912Z_gridftp-tree-private-matrix-summary.csv`

## Dataset Matrix

| dataset | direction | checksum | file parallelism | repeat | fail | hash mismatch | files | bytes | completed | transferred | median Gbps | min Gbps | max Gbps |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| mixed | download | crc32c | 1 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.264512 | 0.264500 | 0.267635 |
| mixed | download | crc32c | 2 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.489268 | 0.489226 | 0.493807 |
| mixed | download | crc32c | 4 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.826199 | 0.818142 | 0.834954 |
| mixed | download | none | 1 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.266293 | 0.262324 | 0.267619 |
| mixed | download | none | 2 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.487775 | 0.477488 | 0.487776 |
| mixed | download | none | 4 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.843826 | 0.835137 | 0.844067 |
| mixed | upload | crc32c | 1 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.289718 | 0.286881 | 0.290016 |
| mixed | upload | crc32c | 2 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.562667 | 0.562490 | 0.567701 |
| mixed | upload | crc32c | 4 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 1.088710 | 1.072990 | 1.100220 |
| mixed | upload | none | 1 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.285576 | 0.285077 | 0.286893 |
| mixed | upload | none | 2 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 0.556227 | 0.548994 | 0.563680 |
| mixed | upload | none | 4 | 3 | 0 | 0 | 49 | 79750738 | 49 | 79750738 | 1.088570 | 1.081130 | 1.096160 |
| small | download | crc32c | 1 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.000712 | 0.000706 | 0.000713 |
| small | download | crc32c | 2 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.001402 | 0.001394 | 0.001405 |
| small | download | crc32c | 4 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.002668 | 0.002661 | 0.002688 |
| small | download | none | 1 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.000714 | 0.000711 | 0.000716 |
| small | download | none | 2 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.001404 | 0.001389 | 0.001405 |
| small | download | none | 4 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.002687 | 0.002675 | 0.002722 |
| small | upload | crc32c | 1 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.000721 | 0.000721 | 0.000723 |
| small | upload | crc32c | 2 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.001438 | 0.001428 | 0.001447 |
| small | upload | crc32c | 4 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.002861 | 0.002853 | 0.002867 |
| small | upload | none | 1 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.000722 | 0.000721 | 0.000722 |
| small | upload | none | 2 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.001435 | 0.001433 | 0.001445 |
| small | upload | none | 4 | 3 | 0 | 0 | 128 | 524288 | 128 | 524288 | 0.002869 | 0.002869 | 0.002876 |


## Phase 5C Checks

- Tree CLI JSON summary is enabled in the private matrix and the raw CSV records the summary path plus completed/skipped/failed/changed counts.
- Edge-case smoke covers special-character paths, deep directories, many small files, empty-directory non-preservation, symlink rejection, and same-size mtime drift fail-safe.
- Release manifest freshness is checked locally after the final manifest is written, before remote sync/verify.

## Recommendation

- Defaults remain unchanged: file_parallelism defaults to 1 and single-file transfer defaults remain POSIX/full-verify/every_n_chunks.
- Directory transfer remains alpha orchestration, not rsync. Permissions, owner, xattr, ACL, and empty directories are not preserved.
- All supplied grouped rows passed with matching tree hashes.
