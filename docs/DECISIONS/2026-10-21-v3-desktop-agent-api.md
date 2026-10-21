# V3 Desktop Agent API 3.0

Date: 2026-10-21; implementation/acceptance continued 2026-10-22.
Decision: V3 is the only runtime. The historical desktop API 2.x mode/fallback
contract is superseded, not a requirement to keep old executors.

Expose supported core options without silent substitution, including channel count,
pending/pipeline windows, compression, global scheduler, reconnect and timeout.
Use a non-mutating preview plus an authoritative final-summary readback.
Machine-readable method support and limitations gate frontend controls.

Network configuration remains server-local and non-privileged: inventory, recommended
or manually selected bindings, explicit apply, get, verify, revision-guarded clear.
Freeze endpoint and binding when creating a task. Report source binds and logical
lanes without claiming separate physical paths.

Do not invent pause, secure remote browsing, live byte/range progress, per-lane peer
mapping or single-file TLS/auth support. These remain explicit unsupported features
or documented telemetry limits, not frontend assumptions.

Scope, tests, fixed-commit peer evidence and continuation are recorded in
docs/tasks/2026-10-21-v3-desktop-agent-align-01.md. Desktop implementation follows
desktop/docs/rpc-contract.md and desktop/schemas/desktop-agent.schema.json.
