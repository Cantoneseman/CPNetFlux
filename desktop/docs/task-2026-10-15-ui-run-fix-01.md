# UI/agent run-start repair: demo recording flow

- Date: 2026-10-15
- Input: `f04b98f03e525dd43ec5d559cae36f15005a82d1` (`codex/desktop-ui-reliability-01`)
- Shenzhen authoritative base checked: `41eb6e74e32cf13590776bba28560bece85db41b` (`main`); pre-existing task-board and V3 worker-state receipt dirty files are untouched.
- Worktree/branch: `/root/projects/CPNetFlux-worktrees/desktop-ui-run-fix-01`, `codex/desktop-ui-run-fix-01`

## Scope

Repair the desktop create-to-run flow using the existing `transfer.create` and `transfer.start` agent API. A confirmed create response remains a real queued task, then the desktop immediately submits the existing start operation with the returned revision. The UI reports starting, running, start failure, capability mismatch, timeout, revision conflict and stale state explicitly; queued is never presented as completed or running. The existing manual start button remains available when automatic start fails. Refresh and event updates continue to come from agent snapshots. Keep local task hide/delete semantics local and recoverable.

## Non-goals

No protocol/schema change, no server or core tree-client modification, no synthetic progress, no replacing backend capability evidence, no remote file deletion, no long-running cross-domain or performance experiment, and no changes to unrelated dirty files or worktrees.

## Existing API dependency

`transfer.create` returns a queued snapshot with `transfer_id` and `revision`; `transfer.start` accepts `transfer_id`, `operation_id` and `expected_revision`, returning an operation snapshot or a structured error. `transfer.subscribe`, `transfer.get` and `transfer.event` remain the source of truth for task state. Current real adapter may still report manifest/file pagination as unsupported; the detail view will show that as backend capability unavailable while preserving real task state.

## Acceptance

1. Creating a valid task sends exactly one create and, after a real create response, exactly one start with the returned revision; the details page shows starting/running or an actionable start failure.
2. Start errors such as unsupported/capability mismatch, revision conflict, timeout and disconnect are visible in the detail action status; the task is not marked successful and remains queued/stale as returned by agent.
3. Agent state/events refresh the task to scanning/connecting/transferring/terminal; no UI-local progress or mode is invented.
4. Manual start can retry a queued task after automatic start failure; demo and real mode policy selection remains isolated.
5. Local task hide continues to affect only the local recoverable list record and does not call an agent delete method.
6. Python contract/agent tests, Qt RPC integration, Qt offscreen, CMake/CTest, package dependency inspection and `git diff --check` pass.

## Rollback

Revert the commits on `codex/desktop-ui-run-fix-01` or reset this worktree to `f04b98f03e525dd43ec5d559cae36f15005a82d1`. No agent state, remote files, protocol records, or authoritative-root dirty files are changed.

## Verification boundary

R730/R750 are not started. This stage verifies the local agent/API and UI integration only; real Shanghai transfer execution and the final noVNC recording must be repeated when the servers and credentials are available.
