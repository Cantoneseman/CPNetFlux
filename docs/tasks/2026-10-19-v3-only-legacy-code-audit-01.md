# V3-ONLY-LEGACY-CODE-AUDIT-01

Date: 2026-10-19 (Asia/Shanghai)
Status: complete
Owner: current commander, single serial developer
Scope commit: 562842ee76a637d7a92564a6037fb268c91c1cf8
Implementation branch: codex/V3-ONLY-LEGACY-CODE-AUDIT-01
Implementation commit: a41ee88050eab021a9d286aa514351a29bdb573f
Push/readback: pushed to origin/codex/V3-ONLY-LEGACY-CODE-AUDIT-01; git ls-remote returned the same full SHA.

## Actual problem solved

After tree upload and download had been routed through the V3 persistent executor, the source still contained three compiled pre-V3 execution engines with no callers. That left dead transfer behavior in the main file and made it harder for a future change to accidentally revive an older path.

This change removed those three runners, their old per-file upload/download processors, the unused pipeline preparation/cancellation state, and their private scheduler dispatch helpers. The public tree APIs still choose V3 and still reject execution if V3 cannot be selected. Old command-line shapes continue to map to V3, and existing manifest/checkpoint preflight remains for resuming earlier data. The V3 global scheduler summary keeps its two needed counters in a small summary-only struct.

## Scope and non-goals

- Confirm there are no production callers for runGlobalTreeScheduler, runTreeScheduler, or runPipelinedTreeScheduler, then remove them and helpers exclusive to them.
- Preserve V3 persistent tree execution, old-input manifest/checkpoint readers, CLI aliases, and the V3 global-scheduler summary.
- No wire/protocol behavior change, authentication/configuration change, production data change, or physical NIC/local-bind/different-link work.
- Shanghai direct TCP/2121 remains blocked; reverse client direction is tested through isolated SSH tunnels.

## Implementation

- Source change: src/core/io/tree_transfer_client.cpp; 2,052 lines removed and three lines added (the summary-only counter struct).
- On the input commit, the only repository matches for the three runner names were their definitions. After the edit, no runner or old per-file execution definitions remain.
- A source call-graph scan of the edited translation unit found 30 reachable internal functions from the three public tree APIs and zero unreachable internal functions.
- The persistent executor still calls resume preflight, manifest persistence, and the scheduler summary where appropriate. The summary test/build caught and resolved the one shared data shape by replacing the old runtime struct with TreeSchedulerSummaryCounts.

## Build and test evidence

Source artifact sent from Shenzhen to Shanghai: /tmp/cpnetflux-runs/V3-ONLY-LEGACY-CODE-AUDIT-01/source-a41ee88.tar.gz, SHA-256 bf6f22188bfbe6ee6f8fa5c44555cb63332a12f3b0f6abd0e895a65f2c590720. Shanghai extracted that artifact and recorded the same source commit and archive hash in final-src/cpnetflux/.codex-source-revision.

Both hosts completed fresh Release builds with TLS ON, io_uring ON, desktop OFF, tests ON, cross-domain tests OFF, GNU 11.4. The server, upload client, and download client hashes matched between hosts:

- cpnetflux-gridftp-server: 25de892e22793dfbfc218dd15967a818abf3f32af0f11dc4765cc169a1403654
- cpnetflux-tree-upload-client: 5cec250b801735b165266e4b990bff7231b34b89c4bfc56eb53dbc1a5efb6546
- cpnetflux-tree-download-client: b6155d9823ce98ddc31f5babce5d88558c9eea41cb3d62c9e4cb31ff51272baa

Focused alias/executor/pipeline tests passed on both hosts: 3/3. The full Shenzhen gate passed: 393/393, 0 failed, 70.60 seconds. Persistent log: /tmp/cpnetflux-runs/V3-ONLY-LEGACY-CODE-AUDIT-01/final-shenzhen-ctest.log.

Acceptance commands:

    cmake -S <committed-source> -B <fresh-build> -DCMAKE_BUILD_TYPE=Release
      -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=ON
      -DCPNETFLUX_BUILD_DESKTOP=OFF -DCPNETFLUX_BUILD_TESTS=ON
      -DCPNETFLUX_ENABLE_CROSS_DOMAIN_TESTS=OFF
    cmake --build <fresh-build> -j2
    env CPNETFLUX_TEST_TOKEN=<local-test-only-value> ctest
      --test-dir /tmp/cpnetflux-runs/V3-ONLY-LEGACY-CODE-AUDIT-01/final-shenzhen-build
      -j2 --output-on-failure

The test token was supplied only through the local test environment; no credential value was recorded.

## Cross-host transfer results

Using the new Shenzhen client against the Shanghai production service with TLS and token authentication:

- Upload: 3 files, 8,388,685 bytes, pass, actual_transport=v3, actual_mode=persistent_tree, empty fallback reason, tree hash crc32c:ed4c0e6d.
- Download: same files/bytes, pass, V3 persistent mode, same tree hash.
- SHA-256 for all three source, Shanghai destination, and Shenzhen download files matched.
- Summaries: /tmp/cpnetflux-runs/V3-ONLY-LEGACY-CODE-AUDIT-01/interop/upload-final-summary.json and download-final-summary.json.

For the reverse client direction, a temporary anonymous V3 service was bound to Shenzhen loopback only. Two SSH tunnels bridged the Shanghai client to that isolated service; no production service or credential was used:

- Shanghai upload to Shenzhen: 3 files, 8,388,709 bytes, pass, V3 persistent mode, no fallback, tree hash crc32c:55ae0014.
- Shanghai download from Shenzhen: same files/bytes and tree hash; all file SHA-256 values matched.
- Summaries: /tmp/cpnetflux-runs/V3-ONLY-LEGACY-CODE-AUDIT-01/reverse-upload-summary.json and reverse-download-summary.json.

The production server binary already had the same SHA-256 as the new build on both hosts, so no service restart or replacement was needed. Both cpnetflux-demo.service instances remained active and retained their existing configuration. The temporary server, tunnels, and test payload directories were stopped/removed after summaries and hashes were saved. Direct Shanghai-to-Shenzhen production TCP/2121 is still not verified.

## Next plan

Plan V3-ONLY-RESIDUAL-RUNTIME-AUDIT-01: inspect remaining public transfer entry points outside directory transfer and identify whether any old executor or wire path can still be selected. Keep legacy-shaped inputs only when they map to V3, and retain old checkpoint/manifest formats only as input readers. Record an explicit merge/remove decision for any remaining runtime path before changing it. Physical NIC, local-bind, and different physical-link validation remain deferred.

## Handoff

Input commit: 562842ee76a637d7a92564a6037fb268c91c1cf8.
Implementation commit: a41ee88050eab021a9d286aa514351a29bdb573f, pushed and verified.
Evidence index: docs/tasks/2026-10-19-v3-only-legacy-code-audit-01-evidence.json.
