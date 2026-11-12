# V3 Tree Manifest Coalescing 01: fixed event batching

Date: 2026-11-08
Status: both bounded coalescing candidates no-go; line closed; defaults remain threshold 32/max delay 100 ms.

## Purpose

The 50 per-file samples in Small-File Microbatch 01 show tree_manifest_write_seconds medians of 23.431/24.907 seconds for 4 KiB/16 KiB x 100,000. The current TreeManifestWriter coalesces on 32 state events or 100 ms. Candidate 1 tested event threshold 256 and was no-go. Candidate 2 keeps threshold 32 and tests a 500 ms maximum delay, with the production default remaining 100 ms.

This candidate batches tree manifest state snapshots. It is not a wire/data-frame microbatch and does not remove member FILE_BEGIN/FILE_END/FILE_RESULT. If it passes, protocol-level microbatch gets a separate scope.

## Scope and non-goals

- Authority: R770-1 /home/Su/projects/CPNetFlux; R770-2 only builds after a fixed commit.
- Add explicit CLI parameter --tree-manifest-event-threshold, range 1..65536, default 32; keep the 100 ms delay and default behavior.
- Put threshold and maximum delay in JSON summary. Event threshold remains 32 for Candidate 2; maximum delay is explicitly selected as 500 ms only for the candidate, while default remains 100 ms.
- Expected files: include/cpnetflux/config/tree_transfer_options.h, src/config/tree_transfer_options.cpp, src/core/io/tree_transfer_client.cpp, tests/unit/tree_transfer_options_test.cpp, plus CLI usage/tests if required.
- Do not change frame/wire protocol, member operations, checksums, atomic publish, member recovery, connections, file parallelism, chunk/buffer, RDMA, writer pools, production service, or defaults.
- Do not implement adaptive batching or concatenate member files. Do not raise the default max delay above 100 ms.

## Acceptance

1. Default parses as 32; explicit 256 is accepted; 0, 65537, and non-integers are rejected; help shows the option.
2. JSON summary records the threshold; existing manifest coalesce, crash resume, and save failure tests pass.
3. Both nodes build the fixed commit, relevant CTest passes, and upload/download/server hashes match before A/B.
4. Test 4 KiB x 100,000 and 16 KiB x 100,000, at least five interleaved baseline/candidate pairs each, <=2 GiB per sample, fixed TCP route 192.168.100.1<->.2 on eno17095np0.
5. Check counts, bytes, source/target tree SHA, T_ready, manifest snapshots/bytes/write time, CPU/RSS, MemAvailable, owned VmSwap, pswpout, interface counters, and cleanup. Any host swap-out, MemAvailable below 12 GiB, or integrity failure invalidates the sample.
6. Candidate 1 (event threshold 256) is no-go: T_ready did not improve materially; snapshots stayed timer-bound. Candidate 2 fixed event threshold at 32 and compared max delay 100 vs 500 ms in five interleaved pairs at each size. Promotion requires T_ready median down >=20%, paired p95 no worse than 10%, at least 4/5 pairs improved, and finish barrier/crash resume/save failure passing with documented <=500 ms persisted-state staleness. Any swap-out or integrity failure invalidates the sample. Otherwise the candidate is no-go, defaults remain threshold 32/max delay 100 ms, and this coalescing line is closed.

## Candidate A/B result

The implementation commit was 0a6d2a8a468a9c644cbaee200fd576dcaa258be9. R770-1 and R770-2 built that commit with identical upload/download/server SHA-256 values; the relevant CTest set passed 15/15 and the full R770-1 unit suite passed 538/539 with one pre-existing native-RDMA idle-receive test skipped by capability.

The interleaved 5-pair matrix completed all 20 transfers with client/server return code zero, exact 100,000-file counts, equal source/target tree SHA-256, and no owned client swap. The 4 KiB group had no host pswpout deltas. One 16 KiB candidate sample had host pswpout delta 104 and is invalid under the memory gate.

Using /usr/bin/time wall as the transfer-ready proxy, 4 KiB baseline/candidate medians were 35.56/35.55 s and 16 KiB medians were 38.44/38.62 s. The corresponding elapsed_seconds medians were 33.014/33.043 s and 35.903/36.075 s. Manifest write medians were 22.934/22.948 s and 25.314/25.397 s. Candidate pair improvements were 4/5 for 4 KiB and 2/5 for 16 KiB, but neither group approached the required 20% median reduction and the 16 KiB candidate included the invalid swap-out sample.

Candidate 1 is no-go. Raising the event threshold did not reduce snapshots: both variants reported about 200,001 state events and roughly 300-350 snapshots. The 100 ms timer is the active batching limit. A final bounded candidate will test 500 ms with the event threshold fixed at 32; default behavior stays threshold 32/max delay 100 ms unless all promotion gates pass. If Candidate 2 misses its gate, stop expanding manifest/coalescing work and move to a separately scoped protocol-level small-file microbatch study or another measured path. Evidence is compacted in 2026-11-08-v3-small-file-microbatch-02/metrics/compact-summary.json; per-file payload summaries and all temporary trees were removed.

## Candidate 2 result

Commit d731899b8d080dd9bcfa4c7511d5728d1ea4f284 added the bounded delay option. R770-1 and R770-2 built it with identical upload/download/server SHA-256 values, the relevant CTest set passed 15/15, and the full R770-1 unit suite passed 538/539 with the same one capability-skipped native-RDMA idle-receive test. A 64-file candidate smoke with max delay 500 ms passed upload/download, valid and corrupt resume checks, crash resume at two cut points, adaptive resume, and save-failure injection; the temporary evidence tree was removed.

The final interleaved 5-pair matrix completed all 20 transfers with return code zero, exact 100,000-file counts, equal source/target tree SHA-256, and client-owned VmSwap zero. One 16 KiB baseline sample had host pswpout delta 13,891 and is invalid under the memory gate; all other samples had zero host pswpout.

Using wall time as the transfer-ready proxy, 4 KiB baseline/candidate medians were 34.86/35.45 s and 16 KiB medians were 39.09/38.35 s. Candidate pair improvements were 2/5 and 4/5 respectively. The corresponding elapsed_seconds medians were 32.334/32.908 s and 36.529/35.796 s. Manifest write medians were 22.401/22.846 s and 25.821/25.201 s. Neither size met the required 20% median reduction; the apparent 16 KiB improvement is below the gate and includes an invalid baseline pair. Snapshot medians stayed near 300-350, so raising the delay did not remove the full-manifest write bottleneck.

Candidate 2 is therefore no-go. The measured bottleneck is full tree-manifest serialization and atomic write work, not the event threshold or timer alone. Defaults remain threshold 32 and max delay 100 ms. This scope closes the manifest coalescing line; further changes require a new scope for a different measured path or a separately designed protocol-level small-file microbatch, with member checksum, atomic publish, and recovery semantics preserved. Evidence is compacted in 2026-11-08-v3-small-file-microbatch-02/metrics/compact-summary.json and 2026-11-08-v3-small-file-microbatch-02-delay/metrics/compact-summary.json; large per-file summaries and all temporary trees were removed.

## Commands

cmake --build build -j2 --target cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux-gridftp-server cpnetflux_unit_tests
ctest --test-dir build --output-on-failure -R 'tree|directory|file_transfer_smoke'
sha256sum build/cpnetflux-tree-upload-client build/cpnetflux-tree-download-client build/cpnetflux-gridftp-server

Keep only compact metrics, logs, events, timings, and checksums; remove payload and exact temporary directories after each sample.
