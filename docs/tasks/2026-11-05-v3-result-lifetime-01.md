# V3 Result Lifetime 01

Date: 2026-11-05. Input: 28d22ab7fbd8c3d9ec82c50ef64b0a455bb0f46d.
Writer: R770-1 /home/Su/projects/CPNetFlux, serial development on the existing branch.

## Scope

Implement separate bounded result/commit deadlines, receiver lease progress, task-deadline propagation, concurrent directory terminal observation and shutdown-aware RDMA completion waiting. Preserve first failure and all lease identity/recovery fences. The observed long-finalize rejection and control-EOF stall are the regression targets.

## Non-goals

No network, MTU, NUMA, production-service, verification-default, checkpoint-default, compression or scheduler changes. No legacy runtime, parallel writers, worktree, historical payload deletion or changes to Windows project sources. A correct published file alone is not a completed protocol task.

## Affected files

include/cpnetflux/core/io/{dynamic_file_queue,work_executor,persistent_tree_transfer}.h; detail/persistent_transfer_lifetime.h shares actual sender/receiver deadline handling; src/core/io/{dynamic_file_queue,work_executor,persistent_tree_transfer,tree_transfer_client,rdma_data_socket}.cpp; focused lease/persistent tests; CMakeLists.txt and tools/test/run_cpnetflux_rdma_control_loss_smoke.py register the opt-in hardware control-EOF regression; docs/DECISIONS/2026-11-05-v3-result-lifetime.md; this task/result and docs/ROADMAP.md.

## Acceptance

1. Deterministic regression first: 172 second active result wait succeeds under an explicit 5 minute budget; exact expiry, cancellation, stale epoch/attempt, repeated extension and outstanding IO remain rejected. Existing implicit-deadline behavior is preserved.
2. Loopback covers bounded result wait, receiver CommitPending before callback, and unchanged complete-file/range correctness. Native shutdown wait must terminate; full R770-1 CTest and matching peer gates pass.
3. Commit explicit files on R770-1, push verified CPNetFlux GitHub remote and read back SHA. Sync that fixed commit to R770-2, build the same configuration, compare versions and five binary SHA-256 values before transfers.
4. Preserve trace evidence. Run representative native RDMA file tasks only with enough space, same commits/configuration, explicit .1/.2 binds, full protocol termination, counters/routes and external SHA. Six 64 GiB repetitions remain the full gate; partial sampling must be reported as partial.

## Commands And Evidence

cmake --build build -j6; build/cpnetflux_unit_tests --gtest_filter='WorkLease*:WorkExecutor*:PersistentTree*:DataTransportTest.*'; ctest --test-dir build --output-on-failure. Hardware tests require CPNETFLUX_TEST_RDMA_BIND=.1/.2 and any test credential generated in-process without logging it.

Evidence: /home/Su/experiments/CPNetFlux/2026-11-05-v3-result-lifetime-01. Record failing regression, build/test logs, sync receipt, experiments, cleanup and exact SHAs. R770-1 initially has only 21 GiB free: do not start a fresh 64 GiB output without a documented storage plan. Keep the two pre-existing untracked 2026-10-14 task documents out of the commit.

## Validation Adjustment

The second full CTest run exposed a pre-existing race in tools/test/run_cpnetflux_tree_data_reuse_smoke.py: a closing fd disappears between /proc enumeration and readlink. Add only an ENOENT skip for that descriptor; keep socket leak assertions and process-directory errors. This script is an additional affected file.

## Storage Plan

Retain all historical log/manifest evidence. Copy the c4-03 diagnostic payload to R770-2 under this experiment using a committed, matching native-RDMA build; require a complete upload and external SHA-256 match before unlinking its R770-1 data copy and temporary hardlink. Keep the remote 64 GiB payload, original manifest and a location/hash receipt. This relocates one payload without discarding its contents and restores about 64 GiB of receiver space. Remove only fresh successful acceptance outputs after external SHA and checkpoint evidence are retained; preserve failed outputs for diagnosis. If the archive copy fails, leave the original payload intact and do not start a space-unsafe download.
