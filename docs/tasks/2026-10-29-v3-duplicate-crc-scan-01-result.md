# V3 duplicate CRC scan removal result

- Date: 2026-10-29
- Route task: P0-2
- Input commit: `72ce9d62cd374c72baecf3deaafedbb5dc316e31`
- Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`
- Implementation commit: `2bbe7f2feb11fb14d32ef5410f836b4453f4c54a`
- GitHub implementation SHA readback: `2bbe7f2feb11fb14d32ef5410f836b4453f4c54a`
- Source archive SHA-256 on both hosts: `80545fbf49568b4fa5f58ee09e41003a02370a47411536b04e3d9d76347a75dc`
- Status: implementation, software acceptance, GitHub backup, and same-commit two-host build comparison complete. Physical-pair performance acceptance remains deferred.

## Problem and implementation

The sender previously scanned each logical DATA payload twice: once for the frame CRC and once for FILE_END. The receiver scanned it three times: frame verification, FILE_END accumulation, and checkpoint chunk accumulation. The checkpoint scan was discovered during this task in addition to the two scans described by the supplied route.

`ChecksumComputer::combine` now appends an already finalized CRC and its full byte length. It reuses the cached CRC32C operators from P0-1 and restores the internal accumulator state, so raw updates, combined updates, finalize, and reset can be mixed without changing results.

The sender computes the frame CRC once and combines it into FILE_END. The receiver checks the frame CRC against logical bytes after decompression, then reuses the matching CRC for FILE_END and checkpoint chunk accumulation. No bad frame can reach this reuse path before the existing checksum comparison. The sender already bounds frames by chunk boundaries; those normal frames now require one payload scan per endpoint for these CRC uses.

Whole-file frames from another caller may cross chunk boundaries. Their frame CRC cannot represent each individual chunk piece, so the receiver retains per-piece scans for that case. The new test sends 6-byte and 4-byte frames into 4-byte chunks and independently checks all three stored chunk CRCs.

Sparse resume combines only transmitted missing payloads in transfer order. Verified checkpoint ranges are omitted exactly as before. Compression still checks the original logical bytes. Frame/checkpoint formats, CRC strength, corruption injection, V3 runtime selection, and independent stored-file verification are unchanged.

The session now validates checksum arguments before advancing its received offset, including the existing raw-payload API. A missing checksum payload or wrong checksum algorithm leaves progress unchanged.

## Failure found during acceptance

The first full 457-test run had 455 passes, one io_uring capability skip, and one corrupt-output reconnect smoke failure. Corruption was rejected, but a terminal batch error was reported as `server does not support safe batch drain` instead of the underlying committed-output checksum mismatch.

Source tracing found two linked causes: directory lifetime state retained only a terminal-error boolean after release, and the client treated a DRAIN 550 reply as unsupported drain. Isolated corrupt cases passed three times on both the prior P0-1 binary and the candidate, so repeated reruns alone did not establish a fix. The smoke was strengthened to query the detached batch directly and require the exact integrity diagnosis. It deterministically failed before the fix with `550 CPNetFlux prior batch has a non-retryable error`.

Directory lifetime state now retains the first non-transport terminal Status under a mutex. DRAIN reads it after channel detachment and returns its message in the existing 550 response; the client propagates that response. The deterministic test then passed, and it also failed on the prior P0-1 binary (`caee34d3cd95ccd4cd656a2e253d1abb1d64b2001a5152b5a487664d78bd88ab`), confirming that this diagnostic gap predates P0-2. Existing corruption assertions were retained. This mutex is outside the per-frame data path.

## Verification

- New checksum tests cover uneven finalized-CRC segments, algorithm mismatch without mutation, checksum-none validation, raw/combined updates, zero-length append, reset, and Software/Auto backends.
- New session tests cover FILE_END equality, wrong/missing checksum arguments, sparse ranges, out-of-order/replayed frames, and bad FILE_END digests.
- Existing bad-frame-before-checkpoint, compression, recovery, checkpoint batching, and publication-audit cases remain in the full suite.
- The initial new API tests failed compilation before implementation because the methods were absent; all four passed after implementation. This was an API compile-failure check, not an assertion-failure claim.
- Final Release build: passed.
- Final full CTest: 457 cases in 232.82 seconds, 454 passed, one io_uring capability skip, and two token/event-log smoke failures caused by a PowerShell quoting error in this task's process-only token setup. After correcting shell quoting and generating the token on Shenzhen, both failed smoke tests passed in 0.45 seconds. The combined final coverage is 456 passing cases and one skip; this is not a claim that the original full run was all-green. The reconnect matrix and file-checksum smoke passed in the final full run.
- Shenzhen/Shanghai same-commit Release builds: both passed, GCC 11.4, `-O3 -DNDEBUG`, TLS on, io_uring off. The binary SHA-256 values below match on both hosts.
- Whitespace check: passed with `core.whitespace=cr-at-eol`.

| Binary | Matching SHA-256 |
|---|---|
| cpnetflux-gridftp-server | `255d8e94156451313f390babde1810f2a9f086bf3819b5f5d32aa747932a1da7` |
| cpnetflux-tree-upload-client | `339009d2f72266021f987351f775c6adbe3f79815efbbb0ff09f4f2dc8529128` |

Build locations:
- Shenzhen: `/tmp/cpnetflux-build-fd-release-01`
- Shanghai exact-commit source: `/tmp/cpnetflux-duplicate-crc-scan-01-20261029/source`
- Shanghai candidate build: `/tmp/cpnetflux-duplicate-crc-scan-01-20261029/build-shanghai`
- Source archive on both hosts: `/tmp/cpnetflux-duplicate-crc-scan-01-20261029/source-2bbe7f2.tar.gz`

Shanghai's existing project directory and services were not overwritten or switched. This is a synchronized candidate build, not a service deployment. No CPNetFlux cross-server transfer was started.

Commands:
```sh
cmake --build /tmp/cpnetflux-build-fd-release-01 -j2
# Set a process-only test token before token-auth smoke tests; never log its value.
ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j2
python3 tools/test/run_cpnetflux_reconnect_smoke.py --build-dir /tmp/cpnetflux-build-fd-release-01 --case upload-fixed-corrupt --evidence-dir <owned-evidence-dir>
git -c core.whitespace=cr-at-eol diff --check
```

Raw build/test evidence is outside Git at `/tmp/cpnetflux-p0-2-evidence`. No payload, private test key, or build product is committed. The original failed CTest output and deterministic red/green outputs are retained there.

Log index: `/tmp/cpnetflux-p0-2-evidence/log-sha256.txt`, SHA-256 `aa017cb7d3e26b5bc419c3ef1d08ddfff182d4d154b95fc636386ef588e45b00`.

| Evidence file in that directory | SHA-256 |
|---|---|
| ctest-full.log (initial diagnostic failure) | `35571c65e3e9761de3506004bdc9cd0ccffc6ab99fad7333a8ec233738cddf3d` |
| ctest-accepted.log (final full run) | `bb0282dde04544772ffa1d2d8c5bd7cd49e0da8ae79bde24dab59ff9b5c9456f` |
| ctest-token-rerun.log | `24df57c41eedd59217e10310e9b49957457863bbfc06fda50f366537b94ebe66` |
| drain-red-valid-login.log / drain-input-baseline.log | `8a58feee81e3e96680924dbc8fac357a5bca825dc148d52248398866f3308c40` |
| drain-green.log | `86883de2ca3b11fc8ed16e58b7ba1919b61f6a08b290fcbf35c2a210ef4d9d01` |

## Performance boundary and next work

The user deferred physical-server measurement. No R730/R750 or Shenzhen-Shanghai transfer, paired throughput run, or CPU speedup acceptance was performed in this task. Eliminating duplicate scans is established by the source path and CRC-equivalence tests; the route's 15% CPU estimate and checksum core-seconds target remain unvalidated.

Next is P0-3: merge DATA header and payload writes with a shared scatter/gather API, handle partial writes and EINTR correctly, and preserve SIGPIPE and TLS behavior. The existing persistent sender uses two `writeAll` calls per DATA frame. Do not bypass TLS with a raw sendmsg call; the TLS path needs its own appropriate handling. Keep this as a separate implementation and measure syscall/throughput effects when the physical test pair becomes available.
