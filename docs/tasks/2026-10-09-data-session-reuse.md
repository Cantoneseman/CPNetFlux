# DIR-V2-TRANSFER-ENGINE-01: data connection reuse

Input: 410f8a026008e02d82584310ef23d5d86624e87e plus existing uncommitted v2 scaffolding.
Location: /root/projects/CPNetFlux, single serial writer; no new worktree.

Goal: opt-in tree upload/download on one persistent TCP connection, independent
file transactions, bounded frame buffers, explicit file identity/results and timeouts.
Keep existing tree/file manifests and default v1 path. Startup incompatibility falls
back before payload; a partial v2 failure is reported, never blindly replayed.

First slice: fresh transfers, checksum none, compression off, scheduler off,
one worker/one connection, data TLS off. Resume and unsupported configurations use v1.
Non-goals: compression/io_uring tuning, multi-stream v2, WAN performance claims.

Files: persistent_data_session.{h,cpp}, persistent_tree_transfer.{h,cpp}, frame.{h,cpp},
tree_transfer_client.cpp, control_server.cpp, tree_transfer_options.{h,cpp}, CMakeLists.txt,
focused unit/loopback tests, this record and the v2 decision.
Commander also updates docs/coordination/BOARD.md to supersede obsolete migration/worktree blockers.

Acceptance: Release build; frame/session unit tests; real TCP tree upload/download,
external SHA-256, manifests, empty/nested files, resume fallback, unauthorized command,
unsafe path, output collision, disconnect and timeout checks; v1 tree regression.
Commit explicit files and push, verify remote SHA. No WAN experiment in this stage.

## Delivery and verification (2026-10-09)

Upload/download now use XCPNETFLUX V2 negotiation, one EPSV/XDIR operation and one
persistent TCP socket per tree. FILE_BEGIN/DATA/FILE_END/FILE_RESULT isolate files;
DIRECTORY_END validates file count. Default off; unsupported parameters/resume use
v1, capability rejection falls back before payload, partial V2 errors are not replayed.

Red tests reproduced then fixed wrong DATA offset, completed generation replay and
NUL path acceptance. Tests cover matching file results, bounds, timeout, disconnect
checkpoint retention, failed middle file drainage/next commit, legacy bidirectional
resume parsing and owned-artifact exclusion without hiding ordinary user files.

Real TCP smoke: empty/nested files, independent SHA-256, actual manifest counts and
committed states, v1 output, complete tree resume fallback, authentication, symlink,
collision, interrupted V2 upload resumed by v1. 128 x 1MiB in each direction uses
one observed data connection. These are loopback checks, not WAN evidence.

Release commands (Shenzhen):

```sh
cmake -S /root/projects/CPNetFlux -B /tmp/cpnetflux-reuse-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON
cmake --build /tmp/cpnetflux-reuse-release --parallel 2
CPNETFLUX_TEST_TOKEN=$(openssl rand -hex 24) ctest --test-dir /tmp/cpnetflux-reuse-release --output-on-failure --timeout 90
```

Token is ephemeral, never printed/committed. Release: 222 total, 221 pass,
1 io_uring environment skip, 0 fail, exit 0, 39.38 seconds. Final focused rerun after
the timing-scope correction: 14/14 pass, exit 0, 5.66 seconds.
Final Release binary SHA-256 (built from the authoritative root):

- upload: 7f20c412fcb4fe3d508b35e7736bf6825981174f3443ddfb69909d458150ccb4
- download: c709b30db912fca69455179b5b64b74cddd87e28c3b95bb5bc93622eaa114c75
- server: 424b52bcdfcfdf255f1378cf7facf6678cc1633eac673dcaa9d790b4e7789fbb

Full-suite log SHA-256: e9cdeb001fcd49683c30bffc58366600e09d72c92a7ef15427ca706c77bb86dd.
Final focused log SHA-256: ff8f4808a77b9921a96cf0b39e2dfb7adaa6966915eb2acf3ba15455eb8b5918.
Logs: /tmp/cpnetflux-reuse-release-configure.log, /tmp/cpnetflux-reuse-release-build.log,
/tmp/cpnetflux-reuse-release-ctest.log; final build/focused logs use release-build-final
and release-focused-final names under the same prefix. Red: reuse-red-build.log,
reuse-integration-red.log; checkpoint smoke: reuse-checkpoint-smoke.log.

Limitations: in-flight depth 1; a per-file result RTT remains. Fixed-channel budget,
bounded pending, buffer-pool async IO and V2 multi-stream/checksum/TLS/resume remain
subsequent stages. Commit-sync none is not power-loss durability. Summary distinguishes
wait scopes and application-frame wire accounting. Current WAN/GridFTP ratio unknown.

No package installation, live service restart, unknown-data deletion, Shanghai SSH or
WAN performance run. Shenzhen root is authoritative; Windows temp files are connection
staging only. Commit exact reviewed files; preserve all unrelated dirty files.

Next: GitHub SHA confirmation; on experiment instruction prepare the short fixed-version
reuse off/on + GridFTP comparison after two-end disk/process and GSI proxy refresh.
Long runs remain script-driven, without status polling.
