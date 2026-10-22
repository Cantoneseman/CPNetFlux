# V3-DESKTOP-PEER-MAPPING-01 Result

Date: 2026-10-22 (Asia/Shanghai)
Status: accepted for the documented V3 desktop backend scope
Owner: current commander only; serial development in Shenzhen
Branch: `codex/V3-DESKTOP-NETWORK-BINDING-01`
Implementation commit: `55f7a21bb970cc0528d21a52901627aa96660b44`
GitHub remote readback: `origin/codex/V3-DESKTOP-NETWORK-BINDING-01` returned the same SHA.
Source archive SHA-256 on both hosts: `ce66b5e99191f88c790ca671e144cf785a4b7cf1658f55e542e5433540dc2490`.

## Scope Closed

The four items in the desktop backend closeout are complete:

1. Binding inventory, recommendation, manual local-address selection, apply,
   retrieval, verification and clear are exposed through the desktop agent. Tasks
   freeze the selected profile and binding when created.
2. The desktop API contract and agent use V3 only; runtime V1/V2 and compatibility
   fallback are not offered.
3. Explicit per-lane peer IP mapping now flows from network plan through frozen task
   input to V3 control/data sockets, including reconnects. Final evidence reports
   observed local and peer socket addresses. Mapping errors fail without falling
   back to the profile host.
4. The actual desktop-agent-to-V3 transfer was tested between Shenzhen and
   Shanghai in both directions on the same fixed source commit.

No system IP, route, firewall or NetworkManager setting was changed. Physical
multi-NIC and independent physical-path validation remain deferred as requested.

## Implementation and Verification

Changed behavior and contracts are in commit `55f7a21`; the task definition is
`docs/tasks/2026-10-22-v3-desktop-peer-mapping-01.md`. Core build options on both
servers were Release, TLS ON, io_uring ON, tests ON and desktop build OFF. Both
servers compiled the same archive. SHA-256 matched on both for:

| Artifact | SHA-256 |
|---|---|
| `cpnetflux-gridftp-server` | `6633aad6915683a1bae13e1c890b6256d4f268956fed1532af1ccb99a978ad6e` |
| `cpnetflux-tree-upload-client` | `9deb6e94873b35d7e058a0bb8ae9b096e5f687f5bc01c9a8fe5e0dfd6512d5d3` |
| `cpnetflux-tree-download-client` | `41ca99b76a0b422a5e98860de9dc74d033dcfeeb7a72eca72785839c544c8da2` |
| `cpnetflux-auth-probe` | `55b4796c494edca5dd66a1a8bb08f602ca64a23340ee9e39352ae53bc682ae96` |
| `cpnetflux_unit_tests` | `08d2754e99ec27180d2caa1c929126a192baaef03e40d3d832e2626633b43ba6` |

The new `cpnetflux_peer_mapping_smoke` CTest passed 5/5 cases on each server.
Peer-address parser unit tests passed 2/2 on each server. On the Shenzhen full
regression build for this implementation, the core CTest run excluding the
reconnect matrix passed 415/415; the reconnect matrix passed separately 1/1.
Desktop contract, agent and API Python suites passed 8/8, 18/18 and 18/18. Initial
test attempts exposed a misspelled test-token environment variable and an occupied
loopback fault-proxy port from an unrelated run; corrected/separate reruns passed.

`clang-format` was not installed on Shenzhen, so that optional formatter check could
not run. `git diff --check` passed before the implementation commit.

## Two-Server Acceptance

Runner: `tools/test/run_cpnetflux_desktop_agent_peer.py`. Both hosts ran the real
Unix-socket desktop agent and API 3 contract against a temporary V3 server on the
peer. Each direction performed one upload and one download of a four-file tree
(44,040,225 bytes). TLS identity and data TLS checks passed; both logical lanes
reported the configured peer IP for control and data sockets, and the selected
local bind IP for both socket types. Server-side uploaded tree hashes matched the
source-side and downloaded tree hashes.

| Initiating desktop agent | Peer | Result | Tree SHA-256 |
|---|---|---|---|
| Shenzhen | Shanghai `47.116.174.181` | upload + download passed | `9ccdbcbd5193be649604bebe9c57973797e027c353532fc08248568785024d52` |
| Shanghai | Shenzhen `120.25.121.51` | upload + download passed | `e99bd9871af84d00283dc37caf1af0e7d309a9372fd94775d0251099303f93f5` |

Final RPC and per-lane socket evidence:

- Shenzhen agent: `/tmp/cpnetflux-runs/V3-DESKTOP-PEER-MAPPING-01/evidence/shenzhen-agent/result.json`
- Shanghai agent: `/tmp/cpnetflux-runs/V3-DESKTOP-PEER-MAPPING-01/evidence/shanghai-agent/result.json`
- Both server logs, build logs, CTest logs, source archive and server event logs are
  in the same task directory on their respective host.

Temporary listeners on 24600/24610 (Shanghai) and 24700/24710 (Shenzhen) were
stopped. The existing service on 2121 remained listening. Disposable token, private
key, generated source/download trees and uploaded payload trees were removed after
hash verification; result JSON, manifests, logs, source archive and build evidence
were retained.

## Desktop Handoff

The desktop client can now implement binding review and confirmation using
`desktop/docs/rpc-contract.md`, `desktop/schemas/desktop-agent.schema.json` and
`desktop/docs/backend-handoff-api-3.md`. For an explicit per-lane endpoint, send a
numeric `peer_addresses` entry for every selected binding; leave the list empty (or
all null) to use the profile host. All mapped IPs must reach the same V3 service
instance and root. TLS certificate identity remains the profile server name. A
failed mapped address does not fall back to another address.

This acceptance proves the configured IPs were used by the logical sockets and
that real V3 data moved between the two cloud hosts. It does not prove distinct
physical egress paths, NIC failure independence, throughput, or production
readiness. The four quoted backend closeout items have no remaining implementation
work; the next project work is the desktop client using this handoff.
