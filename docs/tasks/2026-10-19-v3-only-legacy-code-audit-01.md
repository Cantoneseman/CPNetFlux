# V3-ONLY-LEGACY-CODE-AUDIT-01

Date: 2026-10-19
Status: in_progress
Owner: current commander, single serial developer
Input branch: codex/V3-ONLY-PEER-SYNC-01
Input commit: 49bf0282f4729c9e1e722de312d795d31c9653d3

## Goal

Remove the old tree-transfer execution functions that remain compiled but have no callers: `runGlobalTreeScheduler`, `runTreeScheduler`, and `runPipelinedTreeScheduler`. First confirm each is unreachable from the public tree upload/download entry points, then remove the functions and any helpers used only by them. Keep V3 persistent execution, old command-line options that map cleanly to V3, and historical manifest/checkpoint readers used to resume existing data.

## Non-goals

- No change to V3 transfer behavior or wire protocol.
- Do not remove the single-file transfer APIs or shared scheduling, control, and manifest utilities used by V3.
- No physical NIC, local-bind, RDMA, or different-physical-link work.
- Do not change production authentication, service configuration, or user data.

## Acceptance

1. Source call-graph inspection confirms the old runner functions have no production callers; after cleanup, none of the three definitions remain.
2. Existing alias and unified-executor smokes continue to prove `--data-session-reuse off` and other retained inputs use `actual_mode=persistent_tree` with no fallback. Reuse those tests unless inspection finds a coverage gap.
3. Clean CMake builds from the same committed source succeed on Shenzhen and Shanghai; server/upload/download binary SHA-256 values match.
4. Full relevant CTest passes on Shenzhen; final Shenzhen↔Shanghai upload/download transfer summaries report V3 and matching file/tree hashes. Shanghai direct TCP/2121 remains known-blocked; reverse direction may use the isolated loopback SSH-tunnel setup documented by the preceding task.
5. Update the task board and record source SHA, build/test commands, output evidence, deployment/rollback state, and next handoff. Push each completed code/docs stage to the verified origin.

## Affected files

Expected: `src/core/io/tree_transfer_client.cpp`, this task record, `docs/coordination/BOARD.md`; test files only if the existing smoke coverage is insufficient.

## Initial inspection

The current code commit has definitions for all three runners inside `tree_transfer_client.cpp`; `git grep` finds no production call sites outside those definitions. Existing `cpnetflux_tree_control_reuse_smoke` passes `--data-session-reuse off` and asserts both retained control-reuse modes report `actual_mode=persistent_tree`; `cpnetflux_v3_unified_executor_smoke` checks upload and download both use V3. Confirm these tests on the input revision before editing.
