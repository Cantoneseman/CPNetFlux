# V3 Receiver Policy 01

Date: 2026-11-05. Input: 330b959d4d30b5e8b63cf049f3a8d61b5fc1bc31.
Writer: R770-1 /home/Su/projects/CPNetFlux, existing branch, serial development.

## Scope

Expose existing V3 local download receiver policies through TreeTransferOptions and CLI: final verification, manifest flush policy/interval, commit sync, and the existing range checkpoint override (auto/1/4/16). Preserve all defaults, including auto selecting the existing range environment/default. Forward the checksum backend to the local receiver. Report local configured/effective policies and observed range checkpoint batch in JSON; upload receiver policies remain explicitly remote/unobserved. Reject download-only CLI flags on uploads instead of silently ignoring them. A failing truncation regression exposed a pre-existing missing physical-size publication guard; add that guard for all policies and accurately report checksum=none as coverage/size only.

## Non-goals

No checksum/coverage/recovery/publication weakening, default policy change, wire-format extension, server-policy negotiation, system network/service change, RQ/CQ tuning, manifest algorithm/lock rewrite, new pipeline or microbatch. verified_chunks is an explicit alternative failure-coverage policy: it does not perform full's post-write reread. Correction to the prior plan/report: checksum=none invokes the full branch but both session implementations immediately skip checksum readback. It supplies no content integrity guarantee; do not invent a meaningless checksum-free reread or report it as full content verification. Preserve unrelated dirty/untracked work and historical evidence.

## Affected Files

include/cpnetflux/config/tree_transfer_options.h; src/config/tree_transfer_options.cpp; include/cpnetflux/core/io/persistent_tree_transfer.h and src/core/io/persistent_tree_transfer.cpp for actual range checkpoint reporting and the physical-size publication guard; src/core/io/tree_transfer_client.cpp; tests/unit/tree_transfer_options_test.cpp; tests/unit/persistent_data_session_test.cpp; tools/test/run_cpnetflux_tree_receiver_policy_smoke.py; CMakeLists.txt; docs/ROADMAP.md; docs/DECISIONS/2026-11-05-v3-receiver-verification-semantics.md; this task and its result. Experimental scripts/data remain outside Git.

## Acceptance

1. Observe a failing actual-client policy smoke before implementation. Test default preservation, valid/invalid policies, interval bounds, upload rejection and usage. Exercise actual V3 download policy forwarding, no-checksum coverage-only reporting, explicit range override precedence over environment, and honest upload summary scope. The observed truncation regression must reject publication for no-checksum/verified-chunk receivers.
2. Keep checksum/complete coverage, corruption rejection, resumed data revalidation and atomic publication gates. Run relevant persistent/session/recovery tests, full R770-1 CTest and focused peer gates, with generated test credentials kept out of logs/Git.
3. Commit/push explicit files on R770-1, read back verified CPNetFlux GitHub SHA, then sync that fixed commit to R770-2. Require same CMake options/version and five matching binary hashes before cross-host transfers.
4. First bound the performance comparison to single-lane 64 GiB downloads: alternate full/verified_chunks for three repeats each. Explicitly fix manifest every_n_chunks/16, commit_sync=none, range override=16, CRC32C, no compression/recovery and .1/.2 native RDMA binds. Keep other parameters/storage as the accepted previous experiment. Require exact bytes, both protocol/control terminals, native QPs, external SHA after the task timer and route/HCA evidence. Report task rate, finalization time, disk IO, CPU/RSS, medians and CV. Do not mix old-source controls or different durability policies.
5. Use a new evidence root, verify free space before each output, and remove only fresh successful outputs after external SHA and retained checkpoints. Keep failures. Inspect temporary processes/listeners and production PIDs after completion. Record exact commits, backup/readback, observations and the next action.

## Commands And Evidence

cmake --build build -j6; python3 tools/test/run_cpnetflux_tree_receiver_policy_smoke.py --build-dir build; build/cpnetflux_unit_tests --gtest_filter='TreeTransferOptionsTest.*:PersistentTree*:TransferSessionTest.*:DownloadSessionTest.*'; ctest --test-dir build --output-on-failure. Hardware gates use explicit CPNETFLUX_TEST_RDMA_BIND and in-process test credentials.

Evidence root on both hosts: /home/Su/experiments/CPNetFlux/2026-11-05-v3-receiver-policy-01. Initial code baseline is the prior accepted 78e1e4ab source plus documentation closeout 330b959d. Initial tracked trees are clean; R770-1 has two pre-existing untracked 2026-10-14 task documents. R770-1 has about 83 GiB free; the existing verified 64 GiB seed is retained on R770-2. Do not start a second concurrent output. No subagents or new worktrees.
