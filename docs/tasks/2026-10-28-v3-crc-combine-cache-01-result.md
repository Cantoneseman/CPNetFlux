# V3 CRC32C combine operator cache result

- Date: 2026-10-28
- Input commit: `8e36a11d17480c421a13e8c179508e4cecdd880f`
- Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`
- Implementation commit: `5352b9006f9b43afe43a6e084c0f92fd5452434a`
- GitHub branch readback: `5352b9006f9b43afe43a6e084c0f92fd5452434a`
- Shanghai source archive SHA-256: `a8dfd675f2620cd0db4e5c7806b08de04c4200ca7ce7389b69e3d2837e28a1d1`

## Result

`crc32cCombine` now creates the 64 GF(2) operators for appending `2^n` bytes once per process and applies the operators corresponding to the set bits of the full 64-bit right-side length. It preserves the existing API and finalized CRC behavior. No protocol, frame, TLS, checksum strength, summary fallback, or default setting changed.

The supplied route said the matrix depends on left length and suggested caching by its low 16 bits. Both details are incorrect: the transform depends on the number of bytes appended on the right, and truncation would produce wrong answers. This implementation uses no length-keyed map or lock.

## Performance Evidence

On Shenzhen, the 4 GiB input combined 65,540 full frame CRC records of 65,532 bytes and one 16-byte tail record (65,541 calls total):

| Measurement | Original | Cached | Change |
|---|---:|---:|---:|
| 65,541 combine calls, isolated benchmark | 1,993.601 ms | 105.857 ms | 18.8x faster |
| 4 GiB verified-chunk summary, median of 5 | 1.00805 s | 0.106906 s | 89.4% lower |
| Full loopback TCP + control/data TLS process, median of 5 | 48.243 s | 47.137 s | paired median improvement 1.93% |

The five paired full-process runs alternated order. Both arms used the same source/runtime and differed only in `crc32cCombine`. All used 4 connections, 4 MiB chunks, 64 KiB buffers, CRC32C Auto, required control/data TLS, and `verified_chunks` summary. Every run produced tree hash `crc32c:c751c436`; the client independently checked destination SHA-256 against source SHA-256 `1afa078df0abbd296a2d96f5a77a0cfa9586458d522a3d5ea34590ccd744819c`.

The loopback transfer itself took about 47 seconds, so this change improves summary finalization but does not materially improve data-plane throughput. The earlier R730/R750 report measured a different environment and reported about 3.2 seconds of summary time; this loopback result does not replace a same-configuration R730/R750 rerun. The route's `3.2 s -> 0.05 s` estimate is not validated as a full summary-time estimate.

Evidence is retained outside the repository at `/tmp/cpnetflux-crc-combine-cache-01-20261028/result.json`. The prior source dataset was reused; all per-run destination directories were removed and free space returned to 7.4 GiB.

## Verification

- New reference test compares the cached function with the prior GF(2) algorithm for lengths 0, 1, 2, 255, 65,536, 4 MiB, `2^32`, `2^32+37`, `2^63`, and `UINT64_MAX`, across multiple CRC pairs.
- Existing combine boundary tests and tree-hash tests passed.
- Checksum/tree-hash CTest subset: 15/15 passed after final test-source layout.
- `cpnetflux_summary_crc_auto_smoke`: passed, 94.66 s.
- Full Release CTest: 449 tests; 446 passed, one io_uring capability test skipped, and two initially failed because `CPNETFLUX_TEST_TOKEN` was unset. Those two token/event-log tests were rerun with a process-only random test token and both passed, for 448 passing tests and one skip overall.
- Shenzhen and Shanghai both built commit `5352b9006f9b43afe43a6e084c0f92fd5452434a` in Release with GCC 11.4 and `-O3 -DNDEBUG`. Server SHA-256 matched at `caee34d3cd95ccd4cd656a2e253d1abb1d64b2001a5152b5a487664d78bd88ab`; tree upload client SHA-256 matched at `926f064de21662c1aa46b2e88fbd8d415871c4458d85512e6f1788161f780992`.
- `git -c core.whitespace=cr-at-eol diff --check`: passed.

Relevant commands:
```sh
cmake --build /tmp/cpnetflux-build-fd-release-01 --target cpnetflux_unit_tests cpnetflux-gridftp-server cpnetflux-tree-upload-client -j2
ctest --test-dir /tmp/cpnetflux-build-fd-release-01 -R 'ChecksumTest|TreeHashTest' --output-on-failure
ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j2
```

The paired integration evidence is in `/tmp/cpnetflux-crc-combine-cache-01-20261028/result.json`; the run driver used the existing summary matrix's `run_one` helper and required `verified_chunks` for both arms. Token/event-log reruns used a process-local random token that was not saved.

## Next Work

1. Completed: implementation commit pushed, GitHub SHA read back, and same-commit Release builds match on Shenzhen and Shanghai.
2. Rerun the 4 GiB summary measurement on the R730/R750 pair before claiming the route's cross-machine expected wall improvement.
3. Continue with P0-2 duplicate CRC analysis as a separate change; preserve the existing per-frame corruption detection and chunk verification semantics.
