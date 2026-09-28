# LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-06-QA

- Task ID: `LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-06-QA`
- Route: `R2026-09-24.13/v1`
- Owner: 04 测试与质量
- Implementation worktree: `D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`, branch `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`, fixed HEAD `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` with uncommitted revision-06 changes.
- Implementation result: `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-result.md` in that worktree.

## Goal

Independently decide whether revision-06 closes QA-05 blockers sufficiently for a later 05 fixed Linux build task. This is an acceptance review, not a code-edit task.

## Review scope

1. Inspect the active worktree diff against `3b0820d`, allowed paths, and the implementation result.
2. Review candidate preparation stop/cancel: ownership of socket/TLS objects, cross-thread interrupt, bounded nonblocking connect/read, scheduler stop, auth/TLS failure, exception publication, exactly-once index return and storage release. Look for data races, use-after-free and deadlocks.
3. Review resource accounting: reservation/live candidate/control/socket counters, FD headroom, conservative `reliableCandidateMemory=false` production fail-closed behavior, and unit-state coverage.
4. Confirm worker-reuse remains metadata-only and candidate path does not issue per-file commands, data FDs, manifest writes, event/summary/schema writes, or alter default depth.
5. Run build and focused tests if a Linux/C++ toolchain is available. If unavailable, report exact command and `NOT_RUN`; do not turn static evidence into a pass for dynamic behavior.

## Non-goals and restrictions

- Do not edit source/tests, do not modify main worktree or index, do not SSH, do not build on cloud, do not commit, and do not start performance experiments.
- Do not approve cloud validation if a critical lifecycle/data-race/resource issue remains. Conservative depth-zero may be accepted as a correctness fallback, but must be stated as no performance activation.

## Acceptance gates

- `PASS`: no unresolved critical lifecycle/race issue; scope/default/protocol boundaries hold; focused tests pass or are explicitly blocked only by missing toolchain with static review sufficient for a later Linux build task. Record that runtime/build evidence remains required before performance claims.
- `BLOCKED`: any unresolved candidate thread/socket ownership, cancellation, exception, exactly-once cleanup, or scope violation; or evidence is insufficient to safely hand to 05.

Write:

- `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-qa-result.md`
- `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-qa-last-message.md`

in the shared main worktree. Include findings first, exact paths/lines, commands/exit codes, implementation worktree state, dynamic-test availability, and a clear PASS/BLOCKED decision. Do not alter BOARD/ROSTER.
