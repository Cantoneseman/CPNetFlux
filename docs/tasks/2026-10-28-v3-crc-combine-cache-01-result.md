# V3 CRC32C combine operator cache result

- Date: 2026-10-28
- Input commit: `8e36a11d17480c421a13e8c179508e4cecdd880f`
- Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`
- Implementation commit and remote readback: pending finalization.

## Result

`crc32cCombine` now creates the 64 GF(2) operators for appending `2^n` bytes once per process and applies the operators corresponding to the set bits of the full 64-bit right-side length. It preserves the existing API and finalized CRC behavior. No protocol, frame, TLS, checksum strength, summary fallback, or default setting changed.

The supplied route said the matrix depends on left length and suggested caching by its low 16 bits. Both details are incorrect: the transform depends on the number of bytes appended on the right, and truncation would produce wrong answers. This implementation uses no length-keyed map or lock.

## Performance Evidence

On Shenzhen, 4 GiB combined as 65,541 data-frame CRC records of 65,532 bytes plus the final tail:

| Measurement | Original | Cached | Change |
|---|---:|---:|---:|
| 65,541 combine calls, isolated benchmark | 1,993.601 ms | 105.857 ms | 18.8x faster |
| 4 GiB verified-chunk summary, median of 5 | 1.00805 s | 0.106906 s | 89.4% lower |
| Full loopback TCP + control/data TLS process, median of 5 | 48.243 s | 47.137 s | paired median improvement 1.93% |

The five paired full-process runs alternated order. Both arms used the same source/runtime and differed only in `crc32cCombine`. All used 4 connections, 4 MiB chunks, 64 KiB buffers, CRC32C Auto, required control/data TLS, and `verified_chunks` summary. Every run produced tree hash `crc32c:c751c436`; the client independently checked destination SHA-256 against source SHA-256 `1afa078df0abbd296a2d96f5a77a0cfa9586458d522a3d5ea34590ccd744819c`.

The loopback transfer itself took about 47 seconds, so this change improves summary finalization but does not materially improve data-plane throughput. The earlier R730/R750 report measured a different environment and reported about 3.2 seconds of summary time; this loopback result does not replace a same-configuration R730/R750 rerun. The route's `3.2 s -> 0.05 s` estimate is not validated as a full summary-time estimate.

Evidence is retained outside the repository at `/tmp/cpnetflux-crc-combine-cache-01-20261028/result.json`. The prior source dataset was reused; all per-run destination directories were removed and free space returned to 7.4 GiB.

## Verification

- New reference test compares the cached function with the prior GF(2) algorithm for lengths 0, 1, 2, 255, 65,536, 4 MiB, (2^{32}), (2^{32}+37), (2^{63}), and `UINT64_MAX), across multiple CRC pairs.
- Existing combine boundary tests and tree-hash tests passed.
- `cpnetflux_summary_crc_auto_smoke`: passed, 94.66 s.
- Full Release CTest: 449 tests; 446 passed, one io_uring capability test skipped, and two initially failed because `CPNETFLUX_TEST_TOKEN` was unset. Those two token/event-log tests were then rerun with a process-only random test token and both passed.
- `git -c core.whitespace=cr-at-eol diff --check`: passed.

## Next Work

1. Commit and push the named files on the current branch, then sync that exact commit to Shanghai and verify matching Release build hashes.
2. Rerun the 4 GiB summary measurement on the R730/R750 pair before claiming the route's cross-machine expected wall improvement.
3. Continue with P0-2 duplicate CRC analysis as a separate change; preserve the existing per-frame corruption detection and chunk verification semantics.
