# DIR-INTEGRATION-01 result

Authoritative root: /root/projects/CPNetFlux. Single serial writer; no new Git worktree.
Input root d5da3199c238247a87ad6da01701676107524399 includes accepted persistent-data
slice 53a6dfb. Imported accepted control pipeline input 00be35a, common base 47b0ca2.
Integration source commit: 6f99b4d1ad984dbd20273593fe60812609673d54.

The root now contains both independently selected paths: control pipeline depth
0/1/2/4 with N+1 exclusive slots and bounded pending, and opt-in single persistent
data session across files. Phase timing and short comparison tools are present.
Persistent V2 wire/protocol, per-file transaction identity/results and fallback are
retained. These are separate modes; explicitly selecting tree data reuse with
nonzero control depth is rejected rather than silently bypassing either feature.
Default switches remain off; V2 remains fresh/checksum-none/single-worker/single-stream
without data TLS. This integration is not a new combined V2 scheduler.

Overlap fixes: preserve untagged V2 control completion alongside transfer-id matching
for V1/pipeline; emit unique timing JSON keys with appropriate scopes; raw sidecar
visibility for V2 ownership scanning, preserving the default verified-sidecar filter.
Root full regression initially caught the manifest/temp scanning collision. The
original failing assertion was retained and passed after the correction (19 focused
checks). No unrelated dirty files, historical worktrees, services or experiments
were removed; affected bytes are preserved in external evidence/originals.

## Verified fixed commit

Build from git archive of the integration commit, not the historic dirty root:
Release / C++20 / g++ 11.4 / CMake 3.22.1 / TLS ON / io_uring OFF.
Commands: cmake -S fixed-source -B fixed-build -G Ninja -DCMAKE_BUILD_TYPE=Release
-DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF;
cmake --build fixed-build --parallel 2; ctest --test-dir fixed-build
--output-on-failure --timeout 90. Ephemeral test token injected at runtime and not logged.

244 CTest entries: 243 passed, 1 io_uring environment skip, zero failed; exit 0,
67.06 seconds. Includes tree upload/download/resume/parallel/control-reuse,
pipeline sidecar, persistent-data reuse, changed/edge/manifest, TLS/auth and soak.
Sidecar checks depth 0/1/2/4 with fp1/8 bidirectionally and validates actual slot,
pending, control-connect counts, hash and file set. V2 real TCP smoke checks observed
single data connection, independent hash, manifests, interrupted resume fallback,
collision/failure isolation and strict JSON keys with timing enabled.

Independent fault script: exit 0; bidirectional overlap check and upload pending
reject/disconnect/control timeout. Fault coverage is not every WAN/TLS combination.
43 Python comparison/schema/beta-matrix checks pass; both shell scripts pass bash -n.
The fixed source archive ran full CTest, fault and Python checks successfully.

Source archive SHA-256: 88952a8fdd700f9ab576e397f21ceb0a73003d4e661c2642b26904f3e8abff01.
Binary SHA-256 (fixed-build, the tested integration binaries):
- cpnetflux-tree-upload-client: 0b8585849fdbb62b1adbbde3845868a26277a4913ba8b7fd1716e3384fbd8877
- cpnetflux-tree-download-client: c57d3eea8a7c25f595bda7c65fbb925d3d51c9507bc7b2ab3b0fe5ca421172d7
- cpnetflux-gridftp-server: 8231f57bb93d45ec6f830df3a4c046f9258691f5c6ddcb01927edbac28b58b5a

Evidence directory: /tmp/cpnetflux-runs/DIR-INTEGRATION-01.
fixed-verification.json gives exit codes/log hashes; fixed-configure/build/ctest/fault/
python.log and scanner-focused.log preserve output. Logs remain outside Git.
No WAN transfer or GridFTP throughput acceptance was run. Previous GridFTP comparison
remains incomplete; no 90% or 100 Mbps claim. Old experiment directories retain their
original version meaning. Next performance run must pin this integration or a later
validated commit, rebuild/deploy matching binaries and check the actual mode/counts.
The old short depth script only measures the control-pipeline mode, not V2 reuse.
It still requires a clean fixed source version; do not silently choose an old worktree.

## Backup

Destination verified: git@github.com:Cantoneseman/CPNetFlux.git.
Current branch: codex/DIR-V2-TRANSFER-ENGINE-01. Exact source and this record committed;
push result and remote full SHA are captured externally in release.json and the
user-facing result. A source commit alone is not proof of push.
