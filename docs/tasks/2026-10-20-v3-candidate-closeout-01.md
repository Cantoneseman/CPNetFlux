# V3-CANDIDATE-CLOSEOUT-01

Date: 2026-10-20 (Asia/Shanghai)
Status: planned; execute after Slice E and software route gaps are accepted
Owner: current commander only; no subagents
Input implementation: 63e5fcc6f59db60c09b2ccb64d6bb063761ced2d
Authoritative workspace: Shenzhen
Branch: codex/V3-RELEASE-STABILITY-01 (create a task-specific branch/worktree for new implementation)

## Goal and completion boundary

Finish the current V3 software candidate, with a fixed acceptance checklist instead of adding one more unrelated feature after every smoke test. This does not claim completion of the full 1 Tbps research programme.

## Confirmed entry gap (2026-10-20 inventory)

The actual persistent channel cap remains 8 in persistent_tree_transfer.h and PersistentDirectoryBatch; client/server admission also enforce that cap. A model that accepts ten profiles is not evidence of ten working data lanes. V3-LOGICAL-10LINK-01 must close this software gap before the ten-link acceptance matrix can pass. EDGE-STREAM route coverage still requires an implementation/evidence inventory; do not treat its absence from recent task summaries as completion or as proof that no partial implementation exists.

## Scope

1. Reconcile the 2026-10-16 V3 route against current code and task evidence. In particular, do not treat old task-board planned text as current implementation status. Explicitly identify any software-only gaps in EDGE-STREAM, link/range routing, supported composition, and default settings.
2. Run one fixed 1/2/4/8/10 logical-link functional matrix for upload/download, small-file/mixed/large-range datasets, supported TLS/checksum combinations, and selected interruption/restart/timeout cases. Capture file/tree SHA, lease/recovery counts, and summary mode. Physical-NIC binding is excluded.
3. Run bounded repeated transfers and resume checks on the same candidate. Record elapsed time, resources, correctness, and failures. This is candidate stability evidence, not a throughput benchmark or heavy-soak claim.
4. Produce candidate packages from an identified committed source, inspect package contents, and test installation/configuration/client-server transfer in task-owned isolated staging paths on both cloud hosts. Do not modify formal service or credential files.
5. Reconcile README/usage/defaults with the accepted V3 behavior; document commands, prerequisites, known limitations, and reproduced failures. Record package/source hashes, test commands, logs and GitHub push/readback evidence.
6. Freeze the candidate only after required checks pass. If a defect is found, fix that defect and rerun affected gates; do not expand scope to new optimizations during closeout.

## Non-goals

- Physical NIC/local-bind/different physical paths: deferred by user.
- 100G/1Tbps performance claims, hardware tuning, RDMA/SmartNIC, or long-duration research benchmarks.
- Reintroducing V1/V2 runtime or compatibility paths.
- Formal-service rollout, credentials, old GridFlux-Beta trees, CPSS(DCC), or unrelated worktrees.
- Redesigning desktop UI or adding new product features.

## Acceptance

- Current-route inventory lists implemented, validated, deferred and missing items with evidence.
- Fixed software matrix passes from one committed source; same source on Shenzhen and Shanghai, both builds complete and critical binary hashes match before cross-cloud tests.
- Full serial CTest passes on both hosts; skipped/failed/retried results are recorded honestly.
- Package installation/staging and representative V3 transfer/resume checks pass without changing formal services.
- Candidate artifacts, SHA-256, usage instructions, known limitations and next physical-network validation handoff are recorded.
- Explicit-file commits are pushed to the verified CPNetFlux GitHub remote and remote SHA is read back.

## Expected files

- New bounded acceptance runner(s) only where existing runners cannot express the fixed matrix.
- CMakeLists.txt and relevant behavior files only if evidence proves a defect.
- This task, evidence JSON, docs/coordination/BOARD.md, and candidate usage/package notes.

## Schedule estimate

Planning budget for the remaining software route is 4-7 working days, conditional on the route inventory and no major defect: roughly 1-2 days for ten-lane admission/negotiation and functional coverage, 2-3 days to resolve stream-path software gaps and bounded stability coverage, and 1-2 days for fixed acceptance, candidate packages, usage notes and freeze. These are engineering estimates, not measured completion dates. The earlier 1-3-day estimate applied only to closeout checks and omitted confirmed remaining implementation. Physical NIC/path setup and 100G/1Tbps research goals remain outside this estimate. Re-estimate after inventory if substantial new work is found.

## Handoff

Start by verifying Slice E implementation/result commit and resource/service state. Read the current BOARD top section and the 2026-10-16 decision. Run the route inventory before implementing additional code. Physical-network validation is a separate user-deferred stage.
