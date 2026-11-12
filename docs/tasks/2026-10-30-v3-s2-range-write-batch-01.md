# V3 S2: Range write batching slice

- Stage: S2 (S0-S8 performance route)
- Task: 2026-10-30-v3-s2-range-write-batch-01
- Authoritative directory: Shenzhen server /root/projects/CPNetFlux
- Starting commit: bb920fff68eb5e9def423cee19fdbe9d46e9eb8f

## Scope

1. Add an explicit, default-off receiver-side range write batch buffer:
   CPNETFLUX_RANGE_WRITE_BATCH_BYTES=<bytes>.
2. Reuse the existing BufferedFileWriter. It applies only when no explicit
   file-io-buffer-size is configured and direct write strategy was not selected.
3. Keep the 64 KiB wire DATA frames, per-frame checksums, per-chunk verified
   records, and checkpoint semantics unchanged.
4. Export the effective range batch size in range observations.
5. Add behavior tests for the switch, default-off behavior, exported fields,
   and data correctness.

## Non-goals

- No wire-frame enlargement, protocol-header change, or TLS data-path change.
- No Entry lock sharding, range ownership change, or checkpoint group commit.
- No kTLS, zero-copy, network io_uring, NUMA, or physical-link work.
- Do not add nested batch and file-write timers as a single total.

## Acceptance

- Batch size zero preserves the existing direct or explicit file-IO behavior.
- The environment value accepts off or an integer from 64 KiB through 16 MiB;
  invalid values fail before a range begins.
- An explicit --posix-write-strategy direct takes precedence.
- Run related CMake/CTest, range-observation script tests, range loopback
  correctness, and compare exported counters before and after enabling.
- Use measured evidence before deciding whether to extend S2 or enter S3.

## Expected files

- include/cpnetflux/core/io/persistent_tree_transfer.h
- src/core/io/persistent_tree_transfer.cpp
- tests/unit/persistent_data_session_test.cpp
- docs/ROADMAP.md
- this task record and its result record
