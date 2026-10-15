# UI reliability stage: task creation and local list cleanup

- Date: 2026-10-15
- Input implementation: `039473f2b8478c572e23e1635c950915959c9a8e` (`codex/desktop-real-transfer-01`)
- Authoritative Shenzhen base checked: `41eb6e74e32cf13590776bba28560bece85db41b` (`main`), with only the pre-existing task-board and V3 worker-state receipt dirty files; those files are not touched.
- Worktree/branch: `/root/projects/CPNetFlux-worktrees/desktop-ui-reliability-01`, `codex/desktop-ui-reliability-01`

## Scope

Make the Qt desktop client reliable when creating a task and when removing old entries from the local task list. Creation must wait for a real agent response containing a transfer ID, current epoch and revision, and must report socket, timeout, API and validation failures without navigating as if a task existed. Reconnect and refresh must preserve the stale/unavailable distinction. Detail pagination must use a watermark per page kind so independent agent snapshots do not falsely invalidate one another. Because the current agent schema has no `transfer.delete`, add a clearly labelled, persistent local-list hide action with separate demo/real namespaces; it does not delete a remote file, directory, or agent task.

## Non-goals

No transfer protocol, schema, agent implementation, V1/V2/V3 state machine, core client, authentication policy, packaging dependency, performance experiment, server start, cross-domain run, remote-data deletion, or source-of-truth change. Existing dirty files in the authoritative root and all unrelated worktrees remain untouched.

## API/schema dependencies

Uses existing `agent.getInfo`, `transfer.subscribe`, `transfer.create`, `transfer.get`, entity page methods and lifecycle operations. No new wire method or schema field is introduced. `transfer.create` idempotency continues to use the existing `operation_id`; a timed-out retry reuses the same operation ID for the same form so a late/duplicate request cannot create a second task. Local hidden IDs are persisted in the desktop user's QSettings namespace, outside the agent protocol.

## Acceptance

1. Repeated create clicks are debounced; unavailable, incompatible, stale, timeout and validation paths show retryable failure and never claim success without a real transfer ID/epoch/revision.
2. A successful create upserts and immediately queries the real task, then navigates to its details. Reconnect restores the agent snapshot and marks details unavailable while stale.
3. Attempt/file/range detail pages accept their own agent watermarks and reject old transfer/attempt/epoch data.
4. A single task can be hidden from the local list after confirmation, with in-progress/success/failure text, persistence across refresh/restart, and explicit wording that remote data and agent tasks remain. Demo and real IDs are isolated.
5. Python contract/agent/UI regression checks, Qt offscreen rendering, CMake/CTest and `git diff --check` pass.

## Rollback

Revert the stage commit on `codex/desktop-ui-reliability-01` or reset the desktop worktree to `039473f2b8478c572e23e1635c950915959c9a8e`. No agent state, remote files, protocol records or root dirty files are modified.

## Verification record

This stage will not start R730/R750 or any long-running transfer/performance experiment. Record the exact commit, push SHA, commands, pass/fail output and any backend capabilities still unavailable after implementation in the handoff.
