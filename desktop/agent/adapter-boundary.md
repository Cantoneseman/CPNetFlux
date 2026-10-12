# Desktop agent adapter boundary

Desktop IPC is frozen at `desktop-agent API 2.0.0` in `desktop/docs/rpc-contract.md`. The current Python agent remains a demo prototype and does not yet implement that full contract or a real transfer adapter. An implementation must call `agent.getInfo` before other UI operations and reject mismatched versions.

The agent owns transfer process lifetime, idempotency records, persistent status, cancellation barriers and entity snapshots; the Qt UI owns presentation and reconnect behavior. An adapter may wrap supported CPNetFlux CLI/client interfaces only after checking their structured output and security options. Human-readable stdout is never proof of completion. Unknown backend facts map to unknown/null.

Never pass token/password/private key through JSON-RPC, argv, environment, logs, or persisted profile data. Profiles carry only opaque `credential_ref`; the agent retrieves secret material from an approved OS secret store. If a selected core client cannot accept credentials through a protected channel, real transfer start stays explicitly unsupported until a reviewed secure injection path exists. Do not invent a plaintext temporary-file workaround.

Keep transfer `policy` separate from backend-read `effective_policy`. Do not claim a mode, checksum, resume, TLS, scheduler or channel count from requested values, global capabilities or version strings. Use per-mode capability records only for offer filtering; actual `effective_policy` is authoritative after negotiation. Fallback is allowed only before payload and only if every requested integrity, recovery and transport-security constraint remains satisfied.

The agent does not implement scheduling or range slicing. It passes bounded queue/channel/range policy to the CPNetFlux core and reports stable transfer/attempt/file/range IDs, generation, checksum and state returned by the backend. File/range pages share an immutable snapshot and event watermark. Never let a delayed old attempt update the active attempt.

A cancel response may be accepted while state is `cancelling`; do not show cancelled until all backend workers and writes stop and durable final state is recorded. Resume requires validated checkpoint/source identity and starts a new attempt. Agent restart must reconcile owned processes and checkpoints without duplicate execution. Unsupported pause must leave state unchanged.
