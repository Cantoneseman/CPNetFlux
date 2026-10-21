# V3-DESKTOP-AGENT-ALIGN-01 Result

Date: 2026-10-22 Asia/Shanghai
Status: accepted for the documented desktop API 3.0 scope
Owner: current commander only; serial Shenzhen root development
Input: fb9fd4e6a8f6326b6c18524532d090925c8cb115
Branch: codex/V3-DESKTOP-NETWORK-BINDING-01
Verified implementation: 0aeff2ef7f2d3a7649ce7d284c1a58e42918f22d

## Delivered behavior

- API 3.0 and the demo/Qt shell use V3 only. Runtime V1/V2/auto and compatibility
  fallback are rejected. Old API state directories are preserved, not auto-migrated.
- agent.getContract supplies method availability/reasons, policy defaults/schema
  and limits; transfer.validate previews supported combinations without mutation.
- Transfer requests map channel, window, pipeline, compression, reconnect/deadline,
  checksum/resume and global scheduler settings into real core CLI arguments.
- Network detection/recommendation now supports explicit local selections,
  persisted get, revision-guarded clear and checks against current inventory.
- Tasks freeze profile and binding at creation; queued task behavior remains stable
  when the saved profile/binding changes. Core start rechecks local availability.
- transfer.evidence exposes allowlisted final facts. Real file/range pages use core
  file_timings with explicit observed-only coverage. Desktop attempt and core retry
  indices are separate. Missing mtime/byte details remain null.
- Completion requires successful V3 summary and matching TLS/checksum/channel,
  scheduler/window/queue/resume/retry policy plus closed file and byte counts.
- Resume committed bytes include already skipped logical bytes. Each attempt retains
  its own summary path. Internal summaries are not rejected at the RPC frame limit.
- Core summary adds successful TLS verification facts and fixes queue capacity
  inflation caused by cumulative reconnects.

## Gates and evidence

Shenzhen Release core build passed; full core CTest 413/413 passed, 157.25 s.
Command: CPNETFLUX_TEST_TOKEN=cpnetflux-disposable-loopback-test ctest
--test-dir /tmp/cpnetflux-runs/V3-DESKTOP-NETWORK-BINDING-01-build
--output-on-failure -j2.
The environment value is a disposable loopback test fixture, not a deployed credential.

Final desktop build and CTest 5/5 passed, 31.36 s:
8 contract tests, 18 socket/agent tests, 15 V3 API tests, provisioning import,
24 offscreen renders at 1180/900/760 px.
Evidence: /tmp/cpnetflux-agent-align-core-ctest.log and
/tmp/cpnetflux-agent-align-desktop-ctest.log.

Shanghai Release TLS/io_uring build passed. Related core CTest 35/35 passed;
contract 8/8, socket agent 18/18 and final V3 API 15/15 passed.
Peer has no Qt development packages; no peer Qt build is claimed.

The final source archive was synchronized before final transfers:
source-0aeff2e.tar.gz SHA-256
ed9316a3ff4bf770c35533cdcaf6b9e975a97c892248d5c4353ab5dcd417c345.
Agent and core artifacts match on both hosts. Independent core binary SHA-256:

| Artifact | Both Hosts |
|---|---|
| tree-upload-client | 28258070ed3a520e4c21e5fc9ce8fbb8f3c11a14a78875a50f87788718d904c0 |
| tree-download-client | b3bbf771ec01ac416244d01daad7ef7b29223b1fc28adc9de3ca00be9532dce5 |
| server | 6633aad6915683a1bae13e1c890b6256d4f268956fed1532af1ccb99a978ad6e |

## Real Two-Server RPC Acceptance

Runner: tools/test/run_cpnetflux_desktop_agent_peer.py.
Fixed implementation: 0aeff2ef7f2d3a7649ce7d284c1a58e42918f22d.
Each endpoint ran the real Unix-socket agent and validated every request/response
against the shared schema and semantic contract. No scp payload path was used for
the tested uploads/downloads.

| Initiating Agent | Upload + Download | Files / Bytes | Source, Remote and Returned Tree SHA-256 |
|---|---|---|---|
| Shenzhen | 2/2 pass | 4 / 44040225 | c9c4b84c58652d1e4c246935891e4ab7d955bfc8f394c3de1c79587ead0ffa6c |
| Shanghai | 2/2 pass | 4 / 44040225 | 636ecac1ce95bd75477d221de0f01c36557b519a3802bd2c855fa0f5f345b5c8 |

Each direction observed V3, control/data TLS verified, two accepted logical lanes,
auto compression, global adaptive scheduling, queue capacity 8, effective window 2,
zero consumed retry budget, the configured local bind and three ranges for the
40 MiB file. Random data differs between the two independently generated datasets.
All three copies within each roundtrip have identical hashes.

The runner also checks connection authentication without claiming data-port readiness,
network manual selection/apply/get/verify/clear, and queued profile/binding freezing.
Final evidence directory on both hosts:
/tmp/cpnetflux-runs/V3-DESKTOP-AGENT-ALIGN-01/.
Primary results: shenzhen-agent-accepted2/result.json,
shanghai-agent-accepted2/result.json (also copied as shanghai-agent-result-final.json
on Shenzhen), shenzhen-server-hash-final.json and shanghai-server-hash-final.json.

## Failures Found and Resolved

The first real start rejected --scheduler-mode: the RPC field must map to
core --scheduler. A failing regression was added before correction.
The next upload passed in core but agent rejected the window readback:
shared pipeline depth overrides pending window. Preview now resolves the actual
window and returns an explicit warning; completion keeps strict readback checks.
Final range mapping separately retains zero-based core_attempt, and does not invent
ranges for whole-file length=0 timing records.

A 1 MiB scheduler work-item budget used by an exploratory test was too small to
admit the 16 MiB V3 large-file ranges; core failed explicitly with a link admission
error. The accepted matrix uses shipped scheduler defaults. Arbitrary advanced
budget combinations are not claimed safe by transfer.validate; preview does not
scan/plan all payload. Keep this limitation visible in desktop tuning UI.

One test run hit the Unix-socket bind/listen startup race. Test clients now retry
connection refusal only during their bounded startup window. Final CTest passed.

## Cleanup and Limits

Only owned test server PIDs were stopped: final Shenzhen 192522, Shanghai 381041.
Dedicated ports 24700/24600 are closed. Temporary payload directories and disposable
token/private keys were removed; summaries/logs/hash reports/source archives remain.
Final cleanup removed about 132 MB per host in the final run.
Available space remained about 29 GiB Shenzhen and 71 GiB Shanghai.

No physical-path independence, physical NIC failure, high-bandwidth performance,
production readiness or unmerged parallel-compression branch is accepted here.
Pause, secure remote directory listing, authenticated single-file adapter,
per-lane peer endpoint mapping, system network changes and real-time byte/range
telemetry are not silently advertised as supported.

## Desktop Handoff

Develop against desktop/docs/rpc-contract.md and
desktop/schemas/desktop-agent.schema.json. Start with getInfo/getContract,
initialize forms from policy_defaults/schema, preview before create/start,
and use method support/reasons and observation/coverage to drive controls.
Detailed next scope: desktop/docs/backend-handoff-api-3.md.
