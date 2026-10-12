# Agent API acceptance and staged tests

## AGENT-API-01 (00, completed in this task)

Frozen API 2.0.0 schema and offline semantic checks:

```bash
python3 -B desktop/tests/test_contract.py
```

The Python demo agent implements `agent.getInfo`, the schema-validated API 2.0 request surface, bounded demo-only attempt/file/range snapshot pages, persisted attempt history, and explicit effective-policy state. Runtime checks:

```bash
python3 -B desktop/tests/test_agent.py
```

Demo entities and effective policy are synthetic. They never prove real file I/O, TLS, checksums or network capability. `node.capabilities` stays empty until a real authenticated probe is implemented. The contract vectors do not claim live backend behavior.

## DESKTOP-01 Linux shell/API2 acceptance (2026-10-12)

The Linux delivery slice implements the Qt/API2 shell and relocated installation. It does not implement a real transfer adapter.

- Exact API handshake, same-UID peer identity, incompatible/offline rejection and stale mutation gates.
- Atomic reset subscription after refresh, epoch changes and event gaps; reconnect without replaying mutation requests.
- Entity cursor chains keep separate snapshot IDs but share epoch/revision/sequence. Delayed requests, cursor mismatch and file generation mismatch are rejected.
- Transfer events preserve envelope and attempt identity. A repeated attempt number cannot replace an existing attempt ID.
- Completed requires scan/commit/integrity evidence, resolved actual mode, effective checksum/TLS, no error and nonnegative safe integer counts. Skipped files are included in file closure and shown in the task label.
- Five forged completion variants (error, negative counts, checksum downgrade, data TLS downgrade, control TLS failure) are regression cases. These targeted checks do not constitute a full UI-side JSON Schema validator or backend security proof.
- Seven Qt QML pages load in offscreen mode; smoke succeeds only with a connected, compatible, synchronized agent. The installed real agent remains unsupported for actual transfers.

```bash
cmake -S desktop -B /tmp/cpnetflux-desktop-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cpnetflux-desktop-build --parallel 2
ctest --test-dir /tmp/cpnetflux-desktop-build --output-on-failure
python3 -B packaging/tests/test_installed.py --prefix /path/to/installed/usr --fixtures desktop/tests/fixtures.json
```

The desktop suite includes contract, agent, Qt offscreen and C++ RPC behavior gates. Eight C++ scenarios cover refresh/restart, detail/event isolation, superseded pages, sequence gaps, stale actions/completion, forged completion, and envelope/attempt identity, and foreign page items. Tests use local synthetic peers; they never claim real TLS, payload or cross-host throughput.

## Remaining real product milestones

- DESKTOP-SECRETSTORE-01: approved OS secret-store lookup and secure core credential injection, no secrets in RPC/argv/env/log or plaintext temporary files.
- DESKTOP-ADAPTER-01: real authenticated node probes, root-relative browsing and V2/V3 transfer process/evidence/checkpoint reconciliation, dependent on the secure injection path.
- DESKTOP-LIVE-ACCEPT-01: physical Linux desktop interaction and short two-host transfer/permission/cancel/resume tests, after the adapter passes unit and loopback gates.

Those milestones remain pending. Installation, demo lifecycle and offscreen QML loading are not evidence of real desktop file transfer. Final revision and package verification are recorded in docs/tasks/2026-10-12-packaging-linux-desktop-01.md.

The headless software platform on the validated host cannot save a QQuickWindow screenshot (no Xvfb/physical display). An explicit failed screenshot request exits 4 rather than silently reporting success. Layout/pixel QA remains part of DESKTOP-LIVE-ACCEPT-01.
