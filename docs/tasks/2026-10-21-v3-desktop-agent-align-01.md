# V3-DESKTOP-AGENT-ALIGN-01

Date: 2026-10-21
Status: complete; API 3.0 desktop scope accepted, see matching result record
Input: fb9fd4e6a8f6326b6c18524532d090925c8cb115
Branch: codex/V3-DESKTOP-NETWORK-BINDING-01
Owner: current commander only, serial edits in /root/projects/CPNetFlux

## Scope and decision

Freeze desktop-agent API 3.0 around the existing V3 runtime. The old desktop
V1/V2 execution/fallback contract is superseded; it never describes a supported
runtime. Expose validated transfer options, authoritative result evidence,
network get/clear/manual selection, and machine-readable method availability.
Freeze a task's node/binding inputs when created. Do not silently ignore options.
Preserve unsupported operations as explicit errors for frontend feature gating.

## Non-goals

No system network changes, physical-path claims, new runtime compatibility,
parallel-compression branch integration, arbitrary shell RPC, or UI redesign.
No claim of live per-byte/range telemetry when core only exports final evidence.

## Files and acceptance

Affected: desktop agent/schema/tests/fixtures/contract, minimal UI contract defaults,
desktop CMake packaging, core tree summary evidence and associated test scripts.
Run failing API behavior tests first; then contract/agent/desktop CTest and relevant
core CTest. Verify real TLS/token V3 uploads/downloads through the agent on a fixed
commit after synchronizing and building both servers. Record hashes and peer results.

## Local verification

Core Release build passed; full core CTest 413/413 passed (157.25 s).
Desktop Qt build passed; desktop CTest 5/5 passed, including RPC contract,
socket lifecycle, V3 policy behavior, provisioning import and 24 offscreen renders.
The completion guard is additionally checked against queue/window mismatches,
resume mismatch and excess recovery budget. Final desktop rerun follows this guard.
Core queue capacity now uses configured workers, not cumulative reconnect count.
Remote evidence runner: tools/test/run_cpnetflux_desktop_agent_peer.py.
Physical path/performance/production readiness are outside this acceptance.
