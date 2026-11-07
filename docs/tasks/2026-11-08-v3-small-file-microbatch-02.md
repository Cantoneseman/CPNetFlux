# V3 Tree Manifest Coalescing 01: fixed event batching

Date: 2026-11-08
Status: scope converged; source not changed and candidate A/B not run.

## Purpose

The 50 per-file samples in Small-File Microbatch 01 show tree_manifest_write_seconds medians of 23.431/24.907 seconds for 4 KiB/16 KiB x 100,000. The current TreeManifestWriter already coalesces on 32 state events or 100 ms; this stage tests a fixed threshold of 256 while keeping the 100 ms timer.

This candidate batches tree manifest state snapshots. It is not a wire/data-frame microbatch and does not remove member FILE_BEGIN/FILE_END/FILE_RESULT. If it passes, protocol-level microbatch gets a separate scope.

## Scope and non-goals

- Authority: R770-1 /home/Su/projects/CPNetFlux; R770-2 only builds after a fixed commit.
- Add explicit CLI parameter --tree-manifest-event-threshold, range 1..65536, default 32; keep the 100 ms delay and default behavior.
- Put the threshold in JSON summary. Candidate uses 256; baseline omits the option and uses 32.
- Expected files: include/cpnetflux/config/tree_transfer_options.h, src/config/tree_transfer_options.cpp, src/core/io/tree_transfer_client.cpp, tests/unit/tree_transfer_options_test.cpp, plus CLI usage/tests if required.
- Do not change frame/wire protocol, member operations, checksums, atomic publish, member recovery, connections, file parallelism, chunk/buffer, RDMA, writer pools, production service, or defaults.
- Do not implement adaptive batching or concatenate member files.

## Acceptance

1. Default parses as 32; explicit 256 is accepted; 0, 65537, and non-integers are rejected; help shows the option.
2. JSON summary records the threshold; existing manifest coalesce, crash resume, and save failure tests pass.
3. Both nodes build the fixed commit, relevant CTest passes, and upload/download/server hashes match before A/B.
4. Test 4 KiB x 100,000 and 16 KiB x 100,000, at least five interleaved baseline/candidate pairs each, <=2 GiB per sample, fixed TCP route 192.168.100.1<->.2 on eno17095np0.
5. Check counts, bytes, source/target tree SHA, T_ready, manifest snapshots/bytes/write time, CPU/RSS, MemAvailable, owned VmSwap, pswpout, interface counters, and cleanup. Any host swap-out, MemAvailable below 12 GiB, or integrity failure invalidates the sample.
6. Promotion requires candidate T_ready median down >=20%, paired p95 no worse than 10%, at least 4 of 5 pairs improved, bounded 100 ms + 256 event staleness, finish barrier/crash resume/save failure passing. Otherwise no-go and default stays 32.

## Commands

cmake --build build -j2 --target cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux-gridftp-server cpnetflux_unit_tests
ctest --test-dir build --output-on-failure -R 'tree|directory|file_transfer_smoke'
sha256sum build/cpnetflux-tree-upload-client build/cpnetflux-tree-download-client build/cpnetflux-gridftp-server

Keep only compact metrics, logs, events, timings, and checksums; remove payload and exact temporary directories after each sample.
