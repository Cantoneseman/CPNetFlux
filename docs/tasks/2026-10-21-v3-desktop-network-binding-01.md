# V3-DESKTOP-NETWORK-BINDING-01

Date: 2026-10-21 (Asia/Shanghai)
Status: first_slice_complete (two-server transfer and physical path validation are not accepted by this record)
Owner: current commander only; serial development in Shenzhen
Input commit: 3660ac5d1a3abaa978da7312072d2d6cb66e0041
Branch: codex/V3-DESKTOP-NETWORK-BINDING-01 (root initially held a compression documentation branch; no compression code was changed)
Authoritative workspace: /root/projects/CPNetFlux

## Scope

Add the first backend slice for server-local desktop network configuration:

- inspect the local Linux network inventory through the desktop agent;
- generate a deterministic, non-privileged CPNetFlux binding plan;
- require an explicit confirmation and an inventory revision before applying it;
- persist only CPNetFlux local-address bindings for a remote node;
- expose verification of the persisted binding against the current local inventory;
- provide a desktop flow to inspect, review the recommendation, explicitly confirm, and verify;
- include cloud virtual NICs as usable bind endpoints while never claiming physical path proof;
- reject incomplete plans, link-local recommendations, and unimplemented per-lane peer mappings;
- pass the selected local addresses and matching V3 logical link count to control/data sockets;
- report the selected binding in structured transfer evidence.

## Non-goals

- changing system IP addresses, routes, interface state, firewall rules or NetworkManager settings;
- executing arbitrary shell commands supplied by the UI;
- remote management of the other server;
- claiming that a local address check proves a physical path or throughput result;
- changing formal services or credentials;
- reintroducing V1/V2 runtime paths.

## Acceptance

- schema/API version and feature negotiation cover inventory, plan, apply and verify;
- invalid, stale or unconfirmed plans are rejected;
- only configured CPNetFlux binding state is persisted;
- V3 CLI accepts one or more numeric local bind addresses and validates them;
- control and data connections use the selected lane address;
- existing non-binding transfers remain unchanged;
- focused unit/agent/contract tests pass, then relevant CTest passes;
- Shenzhen commit is pushed and read back before any Shanghai synchronization.

## Expected files

- desktop/agent/cpnetflux_agent.py
- desktop/schemas/desktop-agent.schema.json
- desktop/tests/test_agent.py, desktop/tests/test_contract.py, fixtures as required
- desktop/docs/rpc-contract.md, desktop/docs/product-and-state.md
- desktop/ui/Main.qml, desktop/ui/rpc_client.cpp/.h, desktop/ui/main.cpp
- desktop/tests/test_ui.py, tools/test/run_gridftp_tree_upload_smoke.py
- include/cpnetflux/config/tree_transfer_options.h
- src/config/tree_transfer_options.cpp
- include/cpnetflux/core/io/socket_utils.h, src/core/io/socket_utils.cpp
- include/cpnetflux/core/io/framed_data_socket.h, src/core/io/framed_data_socket.cpp
- src/core/io/tree_transfer_client.cpp
- tests/unit/tree_transfer_options_test.cpp
- CMakeLists.txt, tests/unit/socket_bind_test.cpp
- this task result record after verification

## Verification and boundaries

- Shenzhen core and desktop CMake builds passed.
- Core full CTest: 413/413 passed, including SocketBindTest observing 127.0.0.2 on the accepted TCP connection. Command: CPNETFLUX_TEST_TOKEN=cpnetflux-disposable-loopback-test ctest --output-on-failure -j2 (isolated local test value, not a deployed credential).
- The first full core run had two environment failures because CPNETFLUX_TEST_TOKEN was unset; both passed in the complete rerun with the disposable loopback test value.
- Desktop CTest: 4/4 passed, including contract, 16 agent tests, provisioning import, and eight Qt pages at 1180/900/760 px.
- Fixed a pre-existing offscreen screenshot gap: absent PNGs used to pass silently. Capture now uses the content-item render result, preserves the window background, and fails when image capture/save fails; all 24 screenshots must exist. Network page small-window capture inspected.
- Agent regressions cover confirmation/revision rejection, incomplete-plan rejection, persistence across restart, unsupported peer mappings, address-family/link-local filtering, and actual CLI lane-count/address arguments.
- Real Shenzhen inventory/plan/apply/verify passed in a temporary private state directory: eth0, source 192.168.10.22, one requested lane; verified=true and physical_path_verified=false. No deployed configuration was changed.
- Core build evidence: /tmp/cpnetflux-runs/V3-DESKTOP-NETWORK-BINDING-01-build/Testing/Temporary/LastTest.log
- Desktop build evidence: /tmp/cpnetflux-runs/V3-DESKTOP-NETWORK-BINDING-01-desktop/Testing/Temporary/LastTest.log
- Shanghai preflight: 71 GB available on / and /tmp; CMake 3.22.1, ninja and g++ available. Real inventory/plan/apply/verify also passed with eth0 source 192.168.10.138 in temporary state; physical_path_verified remained false.

## Fixed revision and peer synchronization

Implementation commit: 55897af909cb8ff3e90581d7236f265003175c0c, pushed to the verified CPNetFlux origin branch codex/V3-DESKTOP-NETWORK-BINDING-01; git ls-remote returned the identical SHA before synchronization.

Both hosts use that implementation commit. The Git archive SHA-256 is 5d9484c6b52148a34bc2b79f68882e7b18caab6daf722806caf58f100d764280 on both hosts. Shanghai peer source/build directory: /tmp/cpnetflux-runs/V3-DESKTOP-NETWORK-BINDING-01. It is an isolated build artifact directory, not a development checkout.

Shanghai core Release build passed with TLS and io_uring enabled. Related CTest passed 33/33; agent tests 16/16 and contract tests 8/8 passed. Qt6 development packages are absent on the peer, so no Shanghai desktop build is claimed; the desktop build and full desktop tests passed on Shenzhen.

The independent core builds produced identical SHA-256 values:

| Binary | SHA-256 on Shenzhen and Shanghai |
|---|---|
| cpnetflux-tree-upload-client | d53773ff9a699b4ccce615410663dbfa24ca37751fa1159d67fc3498f661d05f |
| cpnetflux-tree-download-client | 91e9bd68364d558f04f67cd5e1514d95182580cfa643c7868b449332f82ca463 |
| cpnetflux-gridftp-server | 6633aad6915683a1bae13e1c890b6256d4f268956fed1532af1ccb99a978ad6e |

The implemented workflow configures local source addresses. The kernel still selects egress interfaces using existing routes. Verification checks local availability only; it does not establish peer connectivity, physical path separation, performance, or production readiness.

## Next scope

1. Add retrieval/removal of persisted bindings and manual local-address selection, without changing system networking.
2. Design and implement actual per-lane remote endpoint mapping before accepting non-null peer_addresses; the current API rejects these instead of recording an ineffective mapping.
3. Align remaining historical desktop mode labels/contracts with the V3-only core; no older runtime executor is added by this task.
4. Perform desktop-to-agent-to-V3 two-server transfer acceptance on a fixed commit. Physical multi-NIC/path fault validation remains deferred until the hardware is configured.

## Handoff

Only the current commander writes this workspace. Existing unrelated worker-state task/result files remain untracked. The compression task/worktree is untouched. Shanghai receives an archive of the pushed commit into a dedicated build/peer artifact directory, not a new development workspace.
