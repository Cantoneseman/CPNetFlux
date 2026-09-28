# LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-07-QA

- Task ID: `LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-07-QA`
- Route: `R2026-09-24.14/v1`
- Owner: 04 测试与质量
- Worktree: `D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`, branch `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`, fixed HEAD `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`.
- Prior result: `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-qa-result.md` BLOCKED; revision-07 result is in the implementation worktree.

## Goal

Independently verify that revision-07 closes the deterministic control-resource leak without weakening lifecycle or scope contracts.

## Required review

1. Inspect `releaseCandidateControl`, `releaseCandidateSocket`, `finishCandidate`, `cancelCandidate`, `failCandidate`, `releaseCandidateStorage`, destructor callbacks, handoff completion, cancellation, failure, exception and stop cleanup.
2. Confirm control release is owner/generation and ownership based, idempotent, legal for `ControlPending`, `ControlReady`, `InUse`, `Cancelled`, and `Failed`, and cannot double decrement. Confirm storage is released only after control/socket resources.
3. Run focused unit/build checks if Linux tooling is available. Otherwise report exact `NOT_RUN` command and preserve the dynamic evidence gap.
4. Recheck whitelist, no forbidden protocol/file/manifest/schema changes, default depth and production fail-closed behavior.

## Restrictions

No source/test edits, no main worktree/index changes, no SSH/cloud/experiments, no commit. Write only the two QA result files in shared main:

- `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-07-qa-result.md`
- `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-07-qa-last-message.md`

## Decision

Use `PASS` only if the leak is closed and no new critical issue is found; otherwise `BLOCKED`. A PASS still authorizes only the next 05 fixed Linux build/preflight task, never a performance or cross-domain conclusion.
