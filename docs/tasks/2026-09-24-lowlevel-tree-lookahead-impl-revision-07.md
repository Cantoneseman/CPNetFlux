# LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-07

- Task ID: `LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-07`
- Route: `R2026-09-24.14/v1`
- Owner: 03 核心实现；independent QA: 04
- Worktree: `D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`, branch `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`, fixed HEAD `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`.
- Prior gate: `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-qa-result.md` BLOCKED.

## Goal

Fix the deterministic candidate control-resource cleanup blocker only. `releaseCandidateControl` currently accepts only `ControlPending`, while handoff is `InUse` and cancellation/failure are `Cancelled/Failed`; counters and storage therefore leak.

## Required change

- Make control-resource release ownership-based and exactly-once. A candidate whose `controlAcquired` is true must be releasable in every legal state reached after acquisition: `ControlPending`, `ControlReady`, `InUse`, `Cancelled`, and `Failed`, subject to owner/generation validation. A repeated release must return false and must not decrement again.
- Ensure handoff completion, failure, cancellation, exception, and destructor callback paths cannot double-release or leave `liveCandidateControls`, `liveCandidates`, or `candidateBytes` nonzero.
- Preserve socket-resource release ordering and the existing conservative production `reliableCandidateMemory=false` fail-closed behavior.
- Update focused unit assertions only if needed to express the corrected ownership contract; do not weaken them.

## Non-goals and allowed paths

- No protocol, file transfer, manifest/resume/checksum, scheduler/compression/IO backend, external schema, default depth, SSH/cloud, performance experiment, commit, stage, or main worktree edits.
- Allowed source/test paths remain: `include/cpnetflux/core/io/tree_lookahead.h`, `src/core/io/tree_lookahead.cpp`, `src/core/io/tree_transfer_client.cpp`, `tests/unit/tree_lookahead_test.cpp`, plus the two revision result files under this worktree.
- Keep current worktree and uncommitted state; do not create another worktree.

## Acceptance

- Static review proves release is legal and idempotent for all acquired states and all callers release resources exactly once.
- Run focused tests/build if available; otherwise record exact `NOT_RUN` reason. Existing Linux toolchain gap is not a pass.
- Run `git diff --check`, path whitelist, fixed HEAD, empty index, and result UTF-8/LF gates.
- Write `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-07-result.md` and `...-last-message.md` in the worktree. Do not claim QA pass; 04 will decide next.
