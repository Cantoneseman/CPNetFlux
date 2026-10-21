# V3-DESKTOP-PEER-MAPPING-01

Input: b8a0415f9e8ae4f4131f72e52b245a2e75d6fa18.
Owner: current chat, serial development in Shenzhen /root/projects/CPNetFlux.
Continue codex/V3-DESKTOP-NETWORK-BINDING-01. Preserve unrelated dirty files.

Scope: close the four-item desktop backend quote. Binding get/clear/manual
selection, V3-only contract and agent peer acceptance already exist. Implement
per-lane remote IP mapping through plan/apply/frozen task/CLI/control/data/reconnect
and return observed socket endpoints in final evidence. Update frontend handoff.

Non-goals: system network changes, physical multi-NIC validation, desktop page
implementation, pause, remote directory browsing, live range telemetry, unrelated
compression branches or production/performance readiness claims.

Contract: empty/all-null peer_addresses retain profile host; explicit addresses
must cover every requested binding and be usable numeric IPs matching their local
address family. A single binding may serve multiple logical channels. Every lane
uses its mapped destination on the shared profile control port and EPSV data port.
TLS identity remains the profile server name/host. All destinations must reach the
same V3 server instance and root. No fallback to the profile host on failure.

Affected: tree_transfer_options header/parser, tree_transfer_client, option and
socket integration tests/CMake, desktop agent/schema/tests/contract/handoff,
peer acceptance runner, roadmap/decision and task result.
Acceptance: invalid mappings rejected before launch; distinct loopback destination
IPs observed for control/data (including recovery); full relevant CMake/CTest gates;
fixed-commit same-source builds and binary hashes on Shenzhen/Shanghai before real
agent upload/download in both directions, tree SHA checks and retained evidence.
