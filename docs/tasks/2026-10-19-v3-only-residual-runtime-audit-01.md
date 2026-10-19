# V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01

Date: 2026-10-19 (Asia/Shanghai)
Status: in progress
Owner: current commander, single serial developer
Input commit: 9d0595bc45f643399eab90285d5c43e70d11a522
Branch: codex/V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01

## Problem

The previous audit removed uncalled pre-V3 directory runners. Remaining public single-file client/server adapters already enter sendUnifiedFile/receiveUnifiedFile, which use the V3 persistent transfer worker. The directory client and server also run the V3 executor, but their active capability greeting is still labeled XCPNETFLUX V2 / CPNetFlux V2. That makes the required V3-only negotiation unclear and allows an old-labeled control capability to select the current runtime.

## Scope

- Audit public single-file and directory entrypoints, confirm their runtime destination, and record any remaining alternate executor or wire selector.
- Change the active persistent capability request and response label to V3 on both client and server.
- Reject the old XCPNETFLUX V2 greeting before any directory payload can start; do not downgrade.
- Update raw-control smoke tools to exercise V3 acceptance and V2 rejection.
- Update the current architecture decision, README, task board, and this task evidence.
- Build the same committed revision on Shenzhen and Shanghai, record build configuration and executable SHA-256, then perform real V3 upload/download checks between the synchronized endpoints.

## Non-goals

- Do not change transaction frame layouts or the file-transactions/1 transaction schema identifier; that identifier describes the current transaction schema, not a selectable old executor.
- Do not change legacy CLI aliases that already enter V3, or historical checkpoint/manifest read behavior.
- Do not alter production credentials, access policy, or test payload outside this task's temporary run directory.
- Physical NIC, local-bind, and different-physical-link validation remain deferred.
- No subagents.

## Acceptance

1. Every public single-file and directory transfer API is traced to the V3 persistent executor; no old per-file runner or automatic fallback remains.
2. The code and smoke suite contain no active XCPNETFLUX V2/CPNetFlux V2 negotiation path; a V2 greeting is rejected, while V3 negotiation and transfers pass.
3. Relevant smoke tests and full CTest pass on the committed source.
4. Shenzhen and Shanghai build the same source archive/commit with matching server and client executable SHA-256 before test payload is transferred.
5. V3 upload and download in both client directions complete with matching tree and file hashes; any tunnel-mediated direction is reported as such.
6. Task record includes commit SHA, push/readback, commands, logs, binary hashes, interop summaries, and the next planned task.
