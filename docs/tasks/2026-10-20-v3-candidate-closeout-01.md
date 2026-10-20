# V3-CANDIDATE-CLOSEOUT-01

Date: 2026-10-20 (Asia/Shanghai); result recorded 2026-10-21
Status: complete for the software-candidate boundary
Owner: current commander only; no subagents
Input implementation: 3ef398c8d896a7ca534ce89de6c21043bf115ca0
Authoritative workspace: Shenzhen
Development: directly in /root/projects/CPNetFlux on the current codex branch; single commander, serial edits/tests/commits; no new development worktree

## Goal and completion boundary

Finish the current V3 software candidate, with a fixed acceptance checklist instead of adding one more unrelated feature after every smoke test. This does not claim completion of the full 1 Tbps research programme.

## Entry-gap update after ten-lane acceptance (2026-10-20)

V3-LOGICAL-10LINK-01 closed the eight-channel runtime cap and the mixed-size lane-EOF defect. Both hosts passed the preceding full CTest gate and the real cross-cloud upload/download matrix. V3-EDGE-STREAM-01 then closed the bounded stream-path work. The candidate package and fixed acceptance evidence are recorded below.

## Scope

1. Reconcile the current V3 route against implementation and task evidence.
2. Run the fixed 1/2/4/8/10 logical-link functional and recovery matrix. Physical-NIC binding is excluded.
3. Produce candidate packages from an identified committed source and verify installation/configuration/client-server transfer in task-owned isolated staging paths on both hosts.
4. Record package/source hashes, commands, logs, known limitations and the next physical-network validation handoff.
5. Freeze the candidate after the required checks pass. Do not expand scope to new optimizations during closeout.

## Non-goals

- Physical NIC/local-bind/different physical paths: deferred by user.
- 100G/1Tbps performance claims, hardware tuning, RDMA/SmartNIC, or long-duration research benchmarks.
- Reintroducing V1/V2 runtime or compatibility paths.
- Formal-service rollout, credentials, old GridFlux-Beta trees, CPSS(DCC), or unrelated worktrees.
- Redesigning desktop UI or adding new product features.

## Acceptance and execution result

The required software-candidate checks all passed:

- Shenzhen desktop-enabled build: CTest 413/413.
- Shanghai same-source core build: CTest 409/409. Shanghai lacks Qt6 development configuration, so the desktop target was not rebuilt there; the same Shenzhen-built DEB/TGZ was hash-verified, installed in an isolated prefix, and its installed checks passed.
- Shenzhen installed-package checks: 14/14 local logical-link cases; interruption/resume, timeout/resume, and service-restart/resume checks passed.
- Shanghai installed-package checks: package hashes and installed checks passed; interruption/resume passed.
- Shenzhen↔Shanghai using the same installed candidate package: 1/2/4/8/10 upload and download cases passed, 10/10 total, with coverage/checkpoint/hash checks.
- No formal service or credential files were changed.

Exact package names and SHA-256 values, command paths, evidence files, and the internal-candidate boundary are in `docs/tasks/2026-10-21-v3-candidate-closeout-01-result.md`.

## Remaining work

Remaining items for this software candidate closeout: **0**.

The next separate stage, if authorized, is physical-NIC/local-bind/different-physical-path validation after the servers are configured for those links. That stage is intentionally outside this software closeout.

## Handoff

The candidate package is frozen from source commit `3ef398c8d896a7ca534ce89de6c21043bf115ca0`. The result document and board update are evidence-only documentation commits after the package freeze; they do not change the package binaries.
