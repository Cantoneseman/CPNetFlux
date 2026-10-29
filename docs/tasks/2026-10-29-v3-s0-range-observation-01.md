# V3 S0: Range Observation And Evidence Export

Status: complete
Input commit: `539131544cbcaf83aa7cf2324c0b42359ec16966`
Authoritative worktree: Shenzhen `/root/projects/CPNetFlux`

## Scope

Complete the existing server-side per-range observation so an experiment can distinguish accepted DATA frames and logical bytes, entry-lock wait/hold, buffered-writer call time, lower-level file write time, data flush, manifest-record updates, manifest flush, and manifest serialization/temp-write/publication counts and times. Give the exported metrics machine-readable units, attribution scope, and overlap semantics. Preserve transfer behavior.

Extend the formal experiment analyzer and focused observation runner to emit sanitized range-level JSON/CSV and integrity metadata. Missing historical fields must remain unknown; explicitly disabled observation must remain distinct from missing observation. The focused runner will support paired observation-off/on runs at identical settings and record the measured overhead without making a throughput claim.

## Non-goals

- No frame-size, pipeline, lane, lock, checkpoint policy, storage, TLS, kTLS, zero-copy, or protocol optimization.
- No lock sharding or checkpoint batching changes.
- No physical-link or cross-server performance claim.
- No credentials, private keys, or experiment payload in the repository, logs, or exported bundle.

## Acceptance

- Release CMake configure/build succeeds on Shenzhen.
- Relevant CTest gates cover file-write observation, per-range counters and scopes, enabled/disabled behavior, evidence export, and analyzer compatibility with old/missing data.
- The experiment analysis unit gate passes; run the focused five-pair observation off/on calibration and verify result integrity and safe bundle contents.
- Server observation fields appear in the analyzer's formal range export, not only raw server logs.
- Commit only the files listed below for this task, push the current branch to the verified CPNetFlux GitHub remote, and read back the remote commit SHA.

## Expected files

- `include/cpnetflux/core/io/persistent_tree_transfer.h`
- `src/core/io/persistent_tree_transfer.cpp`
- `include/cpnetflux/storage/file_io.h`
- `src/storage/file_io.cpp`
- `include/cpnetflux/checkpoint/manifest_save_stats.h`
- `src/checkpoint/manifest_store.cpp`
- `src/core/io/tree_transfer_client.cpp`
- `src/protocol/control/control_server.cpp`
- `tests/unit/file_io_test.cpp`
- `tools/test/run_cpnetflux_range_observability_smoke.py`
- `tools/perf/range_observation.py`
- `tools/perf/test_range_observation.py`
- `tools/perf/run_cpnetflux_range_observation.py`
- `tools/perf/experiments/v3_gridftp_raw_stage_01/analyze.py`
- `tools/perf/experiments/v3_gridftp_raw_stage_01/test_analyze.py`
- `CMakeLists.txt`
- `docs/tasks/2026-10-29-v3-s0-range-observation-01.md`

Any change to this list will be recorded here before adding the corresponding implementation.


## Results

- Release configure/build passed in /tmp/cpnetflux-s0-baseline with desktop and io_uring disabled and TLS enabled.
- Related CTest selected 39 tests: 38 passed and the io_uring runtime smoke was skipped because the backend is unavailable. The selection covered FileIoTest, ManifestStoreTest, TransferSessionTest, RangeCheckpointBatchTest, DownloadManifestTest, range-observation export, analyzer compatibility, and enabled/disabled/checkpoint-batch end-to-end smokes.
- Python gates passed: tools/perf/test_range_observation.py (7 tests) and tools/perf/experiments/v3_gridftp_raw_stage_01/test_analyze.py (7 tests); Python syntax checks and git diff --check passed.
- Shenzhen loopback TLS A/B completed five paired 512 MiB transfers per arm. All ten transfers passed independent SHA-256 verification. The export contains 80 range rows (40 enabled, 40 disabled); enabled rows report 2,684,354,560 logical bytes and matching writeAtAll requested bytes.
- The paired median delta was -5.16% process wall and -5.24% client elapsed, but individual pair differences ranged from roughly -48% to +7% and changed direction. Treat the overhead result as inconclusive; it does not establish zero overhead or a performance gain.
- Evidence bundle: /tmp/cpnetflux-s0-observation-overhead-delivery/range-observation-evidence.tar.gz. SHA256 sidecar and every manifest file hash were verified. The archive allow-list contains only result.json, observation-overhead.json, range-observations.json, range-observations.csv, and its evidence manifest. Logs, certificates/private keys, and payload are excluded.
- Measurement limitations: loopback TCP, shared 2-vCPU host, host cache and scheduling were not controlled. writeAtAll wall time is not durable-media latency; manifest publication time measures rename and excludes directory fsync.

## Handoff

Use the exported enabled range rows to compare lock wait/hold, writer-call time, writeAtAll time, data flush, and checkpoint/manifest subphases per range. Preserve each metric as a separate distribution because timers nest or overlap. Choose the next bottleneck experiment from those measurements; this S0 run is instrumentation validation, not a basis for lock sharding, checkpoint batching, or any throughput change.
