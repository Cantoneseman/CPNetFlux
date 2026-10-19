# V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01

Date: 2026-10-19 (Asia/Shanghai)
Status: complete
Owner: current commander, single serial developer
Input commit: 9d0595bc45f643399eab90285d5c43e70d11a522
Branch: codex/V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01
Scope/plan commit: 78fdfb759f83e77ce82f690023dd17c84057200e
Implementation commits: 98b3f8ca8e219950f4be97e6ec33090c07111887, 2efd1391228450445d5563c06dfd8e633e1dfeed, 5a2b2e2808f7b5614de6f5988d440a9cb406365a
Push/readback: all implementation commits are ancestors of the pushed branch head; origin readback returned 5a2b2e2808f7b5614de6f5988d440a9cb406365a.

## Actual problem solved

Single-file and directory transfers already entered the V3 persistent executor, and the previous task removed the three unused pre-V3 tree runners. The remaining control greeting still said V2. A peer could therefore appear to negotiate V2 while actually entering V3, and a client accepted a success code without checking which capability the server acknowledged.

This stage makes runtime selection explicit and fail-closed: clients request XCPNETFLUX V3, servers acknowledge CPNetFlux V3, the client checks that exact V3 response, and the old V2 greeting is rejected before directory payload starts. There is no automatic downgrade. The transaction schema suffix file-transactions/1 remains unchanged because it identifies the current transaction format, not an alternate executor.

The V3-only boundary is now consistent across runtime code and documentation: legacy command shapes may map to V3, and V3 may read earlier checkpoint/manifest data, but no pre-V3 executor is available as a runtime fallback. The two-server build/transfer rule is already recorded in root AGENTS.md.

## Scope and non-goals

- Trace single-file and directory entry points to the V3 persistent executor.
- Replace the active V2-labeled capability exchange with explicit V3 negotiation; reject V2 and reject a non-V3 success reply.
- Update raw-control smoke coverage, README, the current V3 decision, and the task board.
- Build the same committed source on Shenzhen and Shanghai before cross-host transfer tests.
- Keep file-transactions/1, legacy input readers, and old CLI aliases that map to V3.
- Do not change credentials, authentication policy, or production data. Physical NIC/local-bind/different-link testing remains deferred.

## Implementation

- Active request and acknowledgement labels now identify V3.
- Server rejects an XCPNETFLUX V2 greeting before directory payload can begin.
- Client verifies the response identifies CPNetFlux V3 rather than trusting only a 200 status.
- Raw-control smoke coverage checks V3 acceptance and V2 rejection.
- Resume/checkpoint comments and test naming now make clear that V3 itself consumes retained old-format input.
- The architecture decision and README state that old formats are inputs only and never select an old runtime.

## Changed files

- Runtime negotiation: src/core/io/tree_transfer_client.cpp and src/protocol/control/control_server.cpp.
- Raw-control regression checks: tools/test/run_cpnetflux_tree_data_reuse_smoke.py and tools/test/run_cpnetflux_tree_dynamic_queue_smoke.py.
- V3/checkpoint wording and focused test naming: src/core/io/persistent_tree_transfer.cpp and tests/unit/persistent_data_session_test.cpp.
- Product and project records: README.md, docs/DECISIONS/2026-10-19-v3-only-peer-sync.md, docs/coordination/BOARD.md, this task record, and the adjacent evidence JSON.

## Acceptance commands

On each host, configure and build the same committed source with:

    cmake -S <committed-source> -B <task-build-dir> -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=ON -DCPNETFLUX_BUILD_DESKTOP=OFF -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_CROSS_DOMAIN_TESTS=OFF
    cmake --build <task-build-dir> -j2 --target cpnetflux_unit_tests cpnetflux-gridftp-server cpnetflux-tree-upload-client cpnetflux-tree-download-client
    CPNETFLUX_TEST_TOKEN=<runtime-injected-test-value> ctest --test-dir <task-build-dir> --output-on-failure -j2

The token placeholder above is not a credential value; the test runner injects its local test-only value at runtime. The full logs are listed below.

The cross-host tree clients used CRC32C, two connections, file parallelism two, dynamic file scheduling, and data-session-reuse=tree. Upload and download JSON summaries are retained at the paths listed under Real transfer results. Reverse Shanghai-client tests used an SSH tunnel into the Shenzhen loopback-only test server; they do not represent a direct TCP/2121 route.

## Build and test evidence

The source archive was built from commit 5a2b2e2808f7b5614de6f5988d440a9cb406365a on Shenzhen and copied/extracted on Shanghai. Its SHA-256 is 6124545278e8dc573cd852f8a52732d38f71da420ca49f8901ae0a306669c53d. Shanghai's .codex-source-revision marker contains the same commit.

Both hosts used GNU 11.4 and CMake 3.22.1, Release, TLS ON, io_uring ON, desktop OFF, tests ON, cross-domain tests OFF. Both full CTest runs passed 393/393 with zero failures:

- Shenzhen log: /tmp/cpnetflux-runs/V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01/shenzhen-final5a-ctest.log (68.99 seconds).
- Shanghai log: /tmp/cpnetflux-runs/V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01/shanghai-final5a-ctest.log (71.56 seconds).

Matching executable SHA-256 on both hosts:

- cpnetflux-gridftp-server: d40a3932dffc24f2eff9f82020e3c8ba133a9d7ed7f67953b039000f3c1fea6e
- cpnetflux-tree-upload-client: 4566db8daade8b5a05c8a2788e83b2ff4b13bad9f8fe3a39dfe494c56e1e2ae8
- cpnetflux-tree-download-client: 9338e4e1871d9785ea682aa859fdc09c4dbe11bac135360d11b5572e70ebe47c

The production cpnetflux-demo.service on both hosts is active, and its running server SHA-256 is also d40a3932dffc24f2eff9f82020e3c8ba133a9d7ed7f67953b039000f3c1fea6e. The production binary already matched, so this task did not restart either service.

## Real transfer results

1. Shenzhen-built client to Shanghai production service: upload and download each passed for 3 files / 3,670,063 bytes. Both summaries report actual_transport=v3, actual_mode=persistent_tree, empty fallback_reason, and tree hash crc32c:5bf8aabd. The SHA-256 values of source, received, and downloaded payload files matched. Summaries are under /tmp/cpnetflux-runs/V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01/interop/ on Shenzhen; the final-commit records are final5a-shenzhen-to-shanghai-upload.json and final5a-shanghai-to-shenzhen-download.json.

2. Shanghai-built client to Shenzhen: upload and download each passed for 3 files / 3,355,469 bytes. Both summaries report actual_transport=v3, actual_mode=persistent_tree, empty fallback_reason, and tree hash crc32c:3d1dafa7. The three source, Shenzhen-received, and Shanghai-downloaded file SHA-256 values matched. Because Shanghai direct TCP/2121 to Shenzhen is still blocked, this direction used SSH tunnels to an isolated anonymous V3 server bound only to Shenzhen loopback (temporary port 2122 and data ports starting at 20300). It does not claim direct production-port connectivity. Summaries are under /tmp/cpnetflux-runs/V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01/interop/ on Shanghai: final-shanghai-to-shenzhen-upload.json and final-shanghai-from-shenzhen-download.json.

3. Both production services remained active. The temporary server PID 2362267 exited; its loopback root, the SSH tunnels, the task's temporary production test directory, and regenerable source/download payload trees were removed. Small transfer-manifest archives were retained:
   - Shenzhen: /tmp/cpnetflux-runs/V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01/interop/manifests-shenzhen.tar.gz, SHA-256 ea277e7875877697f06ec2f22b6a99b760d6b11ee729aadae1dbdf4953590381.
   - Shanghai: /tmp/cpnetflux-runs/V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01/interop/manifests-shanghai.tar.gz, SHA-256 bcbee9fe534624767b4894b3b603047f2b3f76573db17d264438110d8cae3997.

## Acceptance

- Public single-file and directory APIs use the V3 persistent executor; no earlier executor or automatic downgrade remains.
- V2 control greeting is rejected, and the client requires a V3 acknowledgement.
- Both hosts built the same source commit and produced matching server and client binaries before payload transfer.
- Full CTest passed 393/393 on each host.
- Cross-host upload and download passed from clients built on both Shenzhen and Shanghai, with matching payload hashes and V3 summaries.
- Scope, commands, outcomes, limitations, and next plan are recorded in this task file and the adjacent evidence JSON.

## Next planned development

V3-HISTORICAL-CHECKPOINT-INTEROP-01 addresses this practical question: if a transfer was interrupted before V3-only convergence, can the current V3 program safely continue the user's existing partial file rather than make them start over?

The current tests already cover V1 manifest parsing and a legacy chunk-checkpoint resume through the V3 persistent tree path at unit/loopback level. The remaining gap is a deterministic fixture taken from an actual earlier committed format and exercised through the current client/server path, including the user's existing partial bytes. The next task should:

- Identify the exact historical checkpoint/manifest schema and source revision from Git history; create a small credential-free fixture.
- Resume upload and download with V3 clients/server only, send only missing ranges, then compare final SHA-256.
- Verify malformed, corrupt, and unsupported checkpoint inputs fail clearly and never select a legacy executor.
- Build the same committed code on both hosts, confirm binary identities, then perform the cross-host case and retain concise evidence.

Acceptance will require the final summaries to identify V3 execution, no fallback, correct skipped/missing byte counts, and matching final hashes. No old runtime path will be restored. Physical NIC, local-bind, and different-link validation remain deferred.

## Handoff

Code implementation branch head: 5a2b2e2808f7b5614de6f5988d440a9cb406365a, pushed and read back from origin. Completion evidence is in docs/tasks/2026-10-19-v3-only-residual-runtime-audit-01-evidence.json and the task board. The next planned task is V3-HISTORICAL-CHECKPOINT-INTEROP-01.
