# V3-ONLY-PEER-SYNC-01

Date: 2026-10-19 (Asia/Shanghai)
Status: in_progress
Owner: current commander, single serial developer
Authoritative source: Shenzhen /root/projects/CPNetFlux
Implementation baseline: fa87ddb611206876f37fb022be388f270dd16bdb

## Scope

1. Record the rule that code changes are synchronized to both hosts before transfer.
2. Build the current V3 baseline on Shenzhen, deploy the exact build to Shanghai, and
   verify the running executable and configuration without touching historical
   GridFlux-Beta or CPSS(DCC).
3. Remove the runtime use of pre-V3 per-file and legacy execution branches. Existing
   command inputs may remain as V3 aliases when that preserves a useful old invocation.
4. Run CMake/CTest and V3-to-V3 Shenzhen/Shanghai upload/download checks.

## Non-goals

Physical NICs, local source binding, RDMA, 10-link physical routing, production GSI
credential lifecycle, raw FTP compatibility, and throughput optimization are outside
this task.

## Acceptance commands and evidence

- git status, commit SHA, remote SHA, and source manifest on Shenzhen.
- CMake configure/build and relevant CTest on the exact Shenzhen revision.
- Same source/build manifest and binary SHA-256 on Shanghai.
- Service stop/start status, command line, listening port, and no change to existing
  credentials or transfer data outside the CPNetFlux service scope.
- V3-to-V3 upload and download with matching tree hash and JSON actual_transport=v3.
- A retained legacy command input either runs through V3 or fails before payload with
  a clear V3-only error; it must never silently run per-file legacy execution.

## Affected files

AGENTS.md, docs/coordination/BOARD.md, this task record, the V3-only decision record,
tree transfer option parsing/dispatch, relevant unit or integration tests, and the
small build/deployment evidence manifest.

## Handoff

Do not call the stage complete until the two hosts have the same built revision and
the cross-host transfer evidence is stored. If the Shanghai build or service restart
cannot be verified, report the exact blocker and do not use that host for V3
interoperability conclusions.
