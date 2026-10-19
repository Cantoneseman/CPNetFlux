# V3-ONLY-PEER-SYNC-01

Date: 2026-10-19 (Asia/Shanghai)
Status: complete
Owner: current commander, single serial developer
Authoritative repository: Shenzhen CPNetFlux worktree
Input baseline: fa87ddb611206876f37fb022be388f270dd16bdb
Implementation commit: 10d05bcdf972faec15d3a19bae6230cf27905099
Implementation branch: codex/V3-ONLY-PEER-SYNC-01
Implementation push/readback: origin returned the same 10d05bc SHA before this evidence update.

## Actual problem solved

Before this change, directory transfers could choose between a V3 persistent executor and older per-file execution branches. That made “which engine actually moved the data?” unclear and allowed a peer/version mismatch to become a silent fallback. Shanghai was also running an older server binary, so the two production endpoints did not share the same code version.

The tree upload/download entry points now use the V3 persistent executor. Retained command options map to V3 where they have a clear equivalent; options that would imply old behavior fail before payload transfer. V3 peer negotiation failure is explicit and does not trigger an older wire path. Existing checkpoint/manifest readers remain available to resume old data, but do not select an older runtime executor.

## Scope and non-goals

- Build the committed Shenzhen source on both Aliyun hosts, upgrade both CPNetFlux services, and record source/build identities before transfer tests.
- Remove runtime dispatch to the pre-V3 per-file executor and make retained legacy-shaped inputs either V3 aliases or clear pre-payload errors.
- Verify full CMake/CTest and real cross-host upload/download behavior.
- Physical NICs, local bind, RDMA, different physical links, throughput optimization, production GSI lifecycle, and raw FTP compatibility are outside this task. Physical-link validation remains deferred as requested.

Affected areas: `AGENTS.md`, the V3-only decision, task board and this task record; tree transfer option parsing/dispatch; relevant tests and smoke checks. The implementation commit changed 20 source/test files; see `git show --stat 10d05bcdf972faec15d3a19bae6230cf27905099`.

## Source and build evidence

- The Shenzhen source commit was `10d05bcdf972faec15d3a19bae6230cf27905099`; branch `codex/V3-ONLY-PEER-SYNC-01`. The code commit was pushed and its GitHub SHA was read back exactly.
- Source artifact sent to Shanghai: `/tmp/cpnetflux-10d05bc.tar.gz`, SHA-256 `995fbac8452008feec4c42e8483ad8be938595de6e10ac5cdbbffec7f581b6a8`. Shanghai’s extracted source has `.codex-source-revision` with the commit and archive SHA. Its CMake build metadata says `source-archive` because the archive contains no Git metadata; the archive hash ties the build to the exact Shenzhen commit.
- Both clean builds completed in Release with TLS ON, io_uring ON, desktop OFF, tests ON, cross-domain tests OFF, x86_64 and GNU 11.4. CMake printed a non-fatal warning that the extra legacy variable `CPNETFLUX_BUILD_CROSS_DOMAIN_TESTS` was unused; the supported `CPNETFLUX_ENABLE_CROSS_DOMAIN_TESTS=OFF` setting was applied.
- Full gate on Shenzhen: `env CPNETFLUX_TEST_TOKEN=<local-test-only-value> ctest --test-dir /tmp/cpnetflux-runs/V3-ONLY-PEER-SYNC-01/final-shenzhen-build -j1 --output-on-failure`: 393/393 passed, 0 failed, 88.60 seconds. The test-only value was not a production credential.
- Server, upload client and download client binary SHA-256 matched byte-for-byte on both hosts:
  - `cpnetflux-gridftp-server`: `25de892e22793dfbfc218dd15967a818abf3f32af0f11dc4765cc169a1403654`
  - `cpnetflux-tree-upload-client`: `f6fe317515ccaedc284a6e057d53fc8b269c6206a5c09dc68f6e87161d69ecd9`
  - `cpnetflux-tree-download-client`: `a083c3d2575daf833fd2ae6b9b7fbcaf60742b76621a4a7b25f9a5290931759b`

Evidence manifest: `docs/tasks/2026-10-19-v3-only-peer-sync-01-evidence.json`.

## Deployment and transfer results

Both production `cpnetflux-demo.service` instances now run the matching V3 server binary and listen on port 2121. Service command lines, TLS configuration, authentication files and data roots were preserved. Each old server binary was backed up under `/var/lib/cpnetflux/`; both services were active after deployment. The temporary test service and SSH tunnels were stopped and their test ports were closed after testing.

Using the final builds, Shenzhen uploaded a 3-file, 8,388,685-byte tree to Shanghai’s production service, then downloaded it back. Both operations reported `result=pass`, `actual_transport=v3`, `actual_mode=persistent_tree`, an empty fallback reason and tree hash `crc32c:ed4c0e6d`. File SHA-256 values matched at the source, Shanghai destination and Shenzhen download.

Shanghai-to-Shenzhen direct TCP/2121 still times out. To test the reverse client direction without moving credentials or changing the production service, a temporary anonymous V3 server was bound to Shenzhen loopback and reached through SSH tunnels. The final Shanghai client uploaded 3 files totaling 4,194,379 bytes; the server reported V3/persistent-tree, no fallback, tree hash `crc32c:7af9d865`, and all file SHA-256 values matched. This proves the V3 data path works across the two hosts through the tunnel; it does not claim direct Shanghai-to-Shenzhen production-port connectivity.

An early upload probe used the wrong existing local test-token file and was rejected before payload (`bytes_transferred=0`). The retry used the already-present test credential file matching the Shanghai service; no credential value was printed, logged or copied.

## Next development plan

Next: `V3-ONLY-LEGACY-CODE-AUDIT-01`. In plain terms, inspect and remove old execution code that can no longer be reached, while keeping useful old command forms as V3 mappings and keeping old manifest readers for resume. Add a regression gate that makes it obvious if a future change reconnects an old runtime path. Then continue the software fault/recovery route. Physical NIC, local-bind and different-link testing remain deferred until the software behavior is finished.

## Handoff

Code input commit: `10d05bcdf972faec15d3a19bae6230cf27905099`.
Acceptance commands: CMake configure/build on both hosts; full CTest command above on Shenzhen; final V3 upload/download commands are represented by the JSON summaries listed in the evidence manifest.
Output evidence: this record, the evidence JSON and remote summaries under `/tmp/cpnetflux-runs/V3-ONLY-PEER-SYNC-01/interop-10d05bc/`.
Next owner: current commander; continue serially from the pushed branch after verifying its current remote SHA.
