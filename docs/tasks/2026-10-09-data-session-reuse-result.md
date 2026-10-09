# Directory persistent data connection: phase-one acceptance

Date: 2026-10-09. Author: 00 commander, single serial developer on Shenzhen root.
Code: `53a6dfbf46792e26a613059b9da5df615873f641`.
GitHub branch: `codex/DIR-V2-TRANSFER-ENGINE-01` at
`git@github.com:Cantoneseman/CPNetFlux.git`; push succeeded and `git ls-remote`
returned the exact code SHA. Input was `410f8a0` plus reviewed framing scaffolding.

## Exact-commit verification

After push, `git archive HEAD` created `/tmp/cpnetflux-reuse-53a6dfb/source.tar`;
extracted into `src` in that same directory (a build input, not another development
worktree). No unrelated dirty root files enter this archive/build.

```sh
cmake -S /tmp/cpnetflux-reuse-53a6dfb/src -B /tmp/cpnetflux-reuse-53a6dfb/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON
cmake --build /tmp/cpnetflux-reuse-53a6dfb/build --parallel 2
CPNETFLUX_TEST_TOKEN=$(openssl rand -hex 24) ctest --test-dir /tmp/cpnetflux-reuse-53a6dfb/build --output-on-failure --timeout 90
```

Configuration, 113-step build, CTest and outer SSH command returned 0.
CTest: **222 total, 221 passed, 1 skipped, 0 failed**, 39.25 s.
Skip: `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable`, optional unavailable
backend in this build; not an io_uring pass. Ephemeral test token was never printed.
Earlier final root-focused suite: 14/14 pass, 5.66 s.

Evidence remains under `/tmp/cpnetflux-reuse-53a6dfb`: `configure.log`, `build.log`,
`ctest.log`, `source.tar` and exact binaries under `build`. SHA-256:

| Artifact | SHA-256 |
|---|---|
| source.tar | 39d6672d3ea8b34524f69669a0c6ba74a988cf50769754bc2a7801faec683a46 |
| cpnetflux-tree-upload-client | 7f20c412fcb4fe3d508b35e7736bf6825981174f3443ddfb69909d458150ccb4 |
| cpnetflux-tree-download-client | c709b30db912fca69455179b5b64b74cddd87e28c3b95bb5bc93622eaa114c75 |
| cpnetflux-gridftp-server | 424b52bcdfcfdf255f1378cf7facf6678cc1633eac673dcaa9d790b4e7789fbb |
| ctest.log | 3fdec8045b90d9a2b7ef67f1e7eaefd7288a1061efba7bf8c7f6b1f6210f5d64 |

The three exact-commit binaries match the final authoritative-root binary hashes.
Full logs stay on the server; this small manifest is the GitHub-backed evidence index.

## Verified behavior and remaining design

Explicit `--data-session-reuse tree` uploads and downloads 128 x 1MiB over **one
observed TCP data connection per directory**. No per-file EPSV/STOR/RETR. Independent
external SHA-256, file counts/bytes and each file's committed manifest match.
Tests include empty/nested files, v1 off baseline, negotiation rejection fallback,
complete tree resume fallback, interrupted V2 upload resumed by v1, bidirectional
checkpoint parser compatibility, authentication, symlink/path rejection, replay,
wrong identity/offset, bounded frame allocation, timeout/disconnect, collision and
continued next-file completion. Owned checkpoints are not retransmitted as payload;
ordinary user files with similar names remain visible.

First slice is fresh/no checksum/no compression/no scheduler, one worker/one stream,
depth 1, no data TLS; all other requests retain v1. Every file waits for an independent
result before the next; this remaining per-file RTT is explicit. No newly verified
Shenzhen-Shanghai throughput or GridFTP ratio exists for this version.

The approved overall design remains: fixed total channel budget, multiple small
files sharing persistent channels, bounded pending/buffer pools, big-file range
parallelism, async read/network/write overlap and backpressure. Those parts are
subsequent work, not delivered by phase one. Next short WAN experiment must use the
fixed code/binaries, recheck both disks and active workloads, refresh/validate GSI
proxy before GridFTP, and run via scripts without repeated status polling.

No production readiness/100G claim, unknown-directory cleanup, existing service
restart, Shanghai modification, or WAN experiment occurred. Unrelated dirty files
are preserved; only reviewed code/task/decision files were included in the code commit.
