# LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-06

- Task ID: `LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-06`
- Route: `R2026-09-24.13/v1`
- Owner: 03 核心实现；独立复核：04 测试与质量
- Fixed source base: `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- Revision target: 03 已创建的隔离 worktree `D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`, branch `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`; preserve its current uncommitted implementation and revise in place. Do not touch the shared main worktree or its Git index.
- Prior gate: `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-qa-05-result.md` (BLOCKED).

## Goal

Close the independent QA blockers in the opt-in, default-depth-zero tree worker lookahead implementation so that candidate preparation cannot indefinitely hold scheduler shutdown, cannot terminate the process via an uncaught thread exception, and cannot claim resource limits based only on logical counters.

## Non-goals

- Do not change protocol frames, file transfer semantics, manifest/checkpoint/resume/checksum behavior, scheduler/compression/IO backend policy, external telemetry/schema, or default lookahead depth.
- Do not start cloud/SSH work, performance experiments, install tools, commit, stage, or integrate into the main worktree.
- Do not broaden the existing path whitelist without recording the reason and requesting 00 review in the result.

## Required implementation outcomes

1. Give candidate preparation an explicit cooperative stop/cancel path. Bound connect, TLS, authentication and blocking reads with existing timeout mechanisms where possible; ensure cancel/stop can wake waiters and close an in-flight socket without holding the scheduler mutex. Document any platform limitation in code and fail closed to ordinary depth-zero processing.
2. Catch all exceptions at the candidate thread boundary, publish a terminal failure state, notify waiters, and return the reserved manifest index exactly once. Cover thread-construction failure, preparation failure, cancellation racing with handoff, stale owner/generation, worker error, and scheduler stop without losing or duplicating work.
3. Make resource gates truthful. Count actual live candidate slots and owned control/socket resources at their acquisition/release points. If thread stack/TLS or other candidate memory cannot be bounded reliably under the current contract, disable control candidates (effective depth zero) rather than claim they fit the 1 MiB/candidate and 2 MiB/run caps. Keep pending ordinary controls <=2 and prove extra FD cap <=2 with a conservative gate plus Linux-observable assertions; roll back every counter on every exit.
4. Add focused tests for terminal notification and one-time index return across failure/cancel/stop races, plus resource counter rollback and fail-closed behavior. Tests should assert behavioral invariants, not duplicate helper implementation.
5. Keep worker-reuse candidates metadata-only. Candidate preparation must not issue per-file protocol commands or create a data connection. Preserve the normal path when a candidate is rejected or fails.

## Allowed files

Existing implementation whitelist only:

- `CMakeLists.txt`
- `include/cpnetflux/config/tree_transfer_options.h`
- `src/config/tree_transfer_options.cpp`
- `include/cpnetflux/core/io/tree_lookahead.h`
- `src/core/io/tree_lookahead.cpp`
- `src/core/io/tree_transfer_client.cpp`
- `tests/unit/tree_transfer_options_test.cpp`
- `tests/unit/tree_lookahead_test.cpp`

Do not edit other files without first reporting a concrete dependency and waiting for 00 to version the scope.

## Acceptance

- Independent source review shows bounded shutdown/cancel behavior, no uncaught candidate-thread exceptions, exactly-once candidate/index cleanup, truthful fail-closed resource gates, and no protocol/data-plane scope creep.
- Add/adjust focused tests for the outcomes above. Run them if a C++ toolchain is available; otherwise report exact commands and `NOT_RUN` reason.
- Run `git diff --check`, verify changed paths against whitelist, confirm fixed worktree HEAD and empty index, and record all results.
- Do not claim the task passes full QA or may enter Linux/cloud validation. 04 owns that decision in the next task.

## Result artifact

Write the detailed execution result and concise last-message summary only under the implementation worktree at:

- `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-result.md`
- `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-last-message.md`

Report changed paths, state/ownership reasoning, resource accounting, exact commands and exit codes, dynamic-test availability, remaining limitations, and handoff for independent 04 review.
