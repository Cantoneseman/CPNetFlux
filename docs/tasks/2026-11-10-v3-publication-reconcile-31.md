# V3 Publication Reconciliation31

Date:2026-11-10. Status: next task, not started. Priority: correctness prerequisite.
Input:Audit30 final docs commit in evidence closure.json; runtime remains
8a781d134b551d36385c4e6c6bd3e506768d2c1f. R1 sole writer, R2 fixed-commit peer.

## Problem And Scope

[Audit30](2026-11-10-v3-receiver-transaction-audit-30-result.md) reproduces the
link-before-committed-save gap through the actual V3 receiver: upload and download
resume reject output+owned temp pointing at the same verified inode because the
persisted checkpoint still says transferring. Before link and after committed
save controls succeed. This is a staged-state reproduction, not a completed CLI
crash test. Preserve failing artifacts; never delete output to force a retry.

Repair only a provable interrupted publication under the existing format, CRC,
coverage, lease and ACK rules. Select implementation details after reading all
whole-file/range and CLI preflight paths. Do not accept every transferring
checkpoint as committed. Missing/corrupt records, mismatched identities or
unproven ownership remain rejection/recovery cases, never silent success.

No performance claim, save removal, default overwrite policy, thread/window
change, journal/schema design, microbatch, RDMA or system policy change. The
per-file performance attribution branch stays paused. commit_sync=none is not
a power-loss contract; overwrite/rename has a different state and cannot inherit
hard-link proof by assumption. Record supported repair cases and remaining gaps.

## Required Development Order

1. Freeze scope/non-goals/acceptance/edit files before code. Start with a failing
   regression for the staged upload/download gap; preserve before-link and
   committed/lost-result positive controls. Include real file CRC/coverage,
   metadata, manifest body CRC and actual V3 response/lease behavior.
2. Establish ownership of a published output using the existing checkpoint and
   safe open descriptors. For the default link path, validate the expected owned
   temp name, regular non-symlink paths, descriptor/path inode+device, exact size,
   mtime, file/chunk/source identity and complete verified content. A different
   inode with identical bytes is not proof of owned publication. Recheck relevant
   state when concurrent replacement can invalidate it; retain fail-closed errors.
3. Persist committed state atomically before success/empty resume acknowledgement,
   respect requested sync policy, then retire only the proved owned temp alias.
   Save/rename/unlink failures and a second interruption must be retryable without
   deleting user data or acknowledging incomplete state. Keep locks/observation
   lifetime and parallel range ownership coherent.
4. Cover corrupt manifest/body/chunk CRC, incomplete coverage, replaced target,
   symlink temp/output, missing temp, identity/mtime mismatch, output conflict,
   actual save failure and cancellation. Existing committed resume must retain
   its strict checks; no legacy runtime fallback. Include a bounded CLI crash or
   faithful state-restore smoke in addition to local unit tests before acceptance.
5. Run relevant CMake/CTest, record red/green and failures, commit/push/readback on
   R1. Only then sync R2, same config/build and3 binary SHA checks before any
   inter-node run. No benchmark matrix is needed for a correctness-only fix.

Likely affected files: src/core/io/persistent_tree_transfer.cpp, persistent data/
range recovery unit tests, one focused V3 CLI smoke and CMake registration if
needed, task/result/route docs. Avoid unrelated abstractions or shared global locks.

## Acceptance And Handoff

Proven interrupted publication resumes correctly without payload retransmission;
unproven/conflicting/corrupt cases reject without destructive mutations. No OK
FILE_RESULT/empty-mask success before required saved state; repeated recovery is
idempotent. Test current overwrite/range behavior and state explicit limits.
Retain raw logs and payload hashes outside Git, clean exact owned fixtures,
preserve production identities and unrelated dirty files. GitHub verified backup,
two-node source/config/binary receipt and local recovery mirror complete delivery.
Future performance direction remains a separate explicit architecture decision.
