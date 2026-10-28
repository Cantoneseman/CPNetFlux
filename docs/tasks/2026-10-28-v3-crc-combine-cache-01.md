# V3 CRC32C combine operator cache

- Date: 2026-10-28
- Task: P0-1, reduce repeated GF(2) work in tree-summary CRC combination.
- Input commit: 8e36a11d17480c421a13e8c179508e4cecdd880f on codex/V3-GRIDFTP-OPTIMIZED-RERUN-01.
- Baseline evidence: 2026-10-24 R730/R750 optimized rerun reports about 3.2 s in 4 GiB summary combination; the existing summary-CRC reuse work already avoids rereading verified file contents.

## Scope

Optimize `crc32cCombine` while preserving finalized-CRC semantics and every resulting tree hash. Add focused coverage for ordinary boundaries and 64-bit appended lengths, and capture a repeated-combine baseline and candidate measurement. Update this record with exact commands, timings, and commit evidence.

## Design correction to the supplied route

The combine operator depends on the number of bytes appended on the right (`rightLength`), not the left input length. Truncating that length to 16 bits is not valid. The implementation will precompute the 64 operators for appending `2^n` zero bytes once, then apply operators for set bits of the full 64-bit `rightLength`. This avoids a mutable length-keyed map and its locking, while preserving the existing public API.

## Files and boundaries

- Expected source: `src/checksum/crc32c.cpp`.
- Expected tests: `tests/unit/checksum_test.cpp`.
- Task record: this file.
- No protocol/frame/TLS, tree-hash format, checksum strength, summary fallback policy, defaults, or server configuration changes.

## Acceptance

- Existing CRC and tree-hash output remains byte-for-byte identical.
- Compare the candidate against the previous GF(2) algorithm for 64-bit length boundaries, including high bits.
- Release unit tests and relevant CTest gates pass.
- Repeated 4 MiB combine benchmark improves materially; report raw baseline and candidate timings. Do not infer whole-transfer gains from the microbenchmark.
- Commit only named task files and source/test changes, push the current task branch, and read back the remote commit SHA.

## Status

Implemented and locally measured. See docs/tasks/2026-10-28-v3-crc-combine-cache-01-result.md. Pending named-file commit, GitHub readback, and Shanghai same-commit build.
