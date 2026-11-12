# Desktop agent adapter boundary

Desktop IPC is at `desktop-agent API 3.0.0` in `desktop/docs/rpc-contract.md`, independently versioned from the V3 transfer engine. The Python agent exposes an explicitly selected demo adapter and a real authenticated tree-client adapter. Unsupported methods remain explicit. UI operations require an `agent.getInfo` handshake and reject mismatched API versions.

The agent owns transfer process lifetime, idempotency records, persistent status, cancellation barriers and entity snapshots; the Qt UI owns presentation and reconnect behavior. An adapter may wrap supported CPNetFlux CLI/client interfaces only after checking their structured output and security options. Human-readable stdout is never proof of completion. Unknown backend facts map to unknown/null.

Never pass token/password/private key through JSON-RPC, argv, environment, logs, or persisted profile data. Profiles carry only opaque `credential_ref`; the agent retrieves secret material from an approved OS secret store or a user-owned 0600 token file created by the reviewed provisioning-bundle importer. If a selected core client cannot accept credentials through a protected channel, real transfer start stays explicitly unsupported until a reviewed secure injection path exists. The importer may write the bundle token once to a user-owned 0600 file under the private CPNetFlux configuration directory; it never sends token contents through JSON-RPC, argv, environment, logs, or profile data. Do not accept arbitrary shared or group-writable credential files.

Keep transfer `policy` separate from backend-read `effective_policy`. Do not claim a mode, checksum, resume, TLS, scheduler or channel count from requested values, global capabilities or version strings. The V3 core is the runtime authority; do not add older runtime executors or downgrade paths. The form uses API 3 policy defaults/schema and never offers older runtime modes.

The agent does not implement scheduling or range slicing. It passes bounded queue/channel/range policy to the CPNetFlux core and reports stable transfer/attempt/file/range IDs, generation, checksum and state returned by the backend. Each attempt/file/range query has its own immutable snapshot and event watermark; continuation pages must match that query. Never let a delayed old attempt update the active attempt.

A cancel response may be accepted while state is `cancelling`; do not show cancelled until all backend workers and writes stop and durable final state is recorded. Resume is enabled only for an agent-observed V3 tree manifest candidate with matching version, direction, source root and checksum policy. The core validates checkpoint/source identity on resume and starts a new attempt; candidate availability is not proof of identity or a missing-range preview. Agent restart must reconcile owned processes and checkpoints without duplicate execution. Unsupported pause must leave state unchanged.


## Local network binding

The real adapter exposes server-local inventory, deterministic source-address plans, explicit confirmation and local-availability verification. It stores bindings keyed by remote node. It does not alter system IPs, routes, interface state or firewall rules; OS routes choose egress. Incomplete plans are rejected. Per-lane remote endpoint mappings are supported for numeric addresses belonging to the same V3 service and authorized root. physical_path_verified is always false in this API; actual connectivity and physical paths require separate evidence.
