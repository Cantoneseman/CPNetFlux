# R770 plain TCP direct-to-mmap receive 01 result

Date: 2026-11-24

## Scope

Implement a bounded receive path that reads eligible plain TCP DATA bodies directly into the existing mapped download window. The runtime input and analysis baseline were commit `34ded615ea72ea2ee805b08075d39d945288f66d`; the route and experiment analysis were committed as `e19e0f88bc80a7af8778d7abd5d5de08da767cfb`.

This phase changes the receiver implementation, its unit tests, and smoke-test assertions that described the previous buffered mmap path. It does not change protocol bytes, DATA frame limits, CRC32C, pacing, network settings, RDMA, TLS, or recovery format.

## Implementation

- Split frame header validation from payload reading so the receiver can inspect a DATA header before allocating/reading the full payload.
- Direct receive is eligible only for inline mmap download, plain TCP without TLS, uncompressed DATA with CRC32C, and a payload contained in one writable mapping window.
- Read the four-byte CRC prefix first, then use the socket vectored-read path for the payload body into the mapped window.
- Verify CRC32C over mapped bytes before advancing session state, combining range checksums, recording verified ranges, or scheduling checkpoints.
- Keep the range write pin while the receive is in progress, release it on short read/error, and pair successful work-lease starts with finishes. A window-crossing frame falls back before consuming its body.
- Keep TLS, RDMA, compressed DATA, pwrite, other write modes, and other ineligible cases on the buffered path.
- Expose direct receive call/byte counters separately from buffered write/copy counters.

## Verification

- R770-1 build: `cmake --build /home/Su/projects/CPNetFlux/build -j2` passed.
- Direct receive unit filter: 8/8 passed. It covers valid and invalid CRC, truncated payload, mapped-range retry after CRC failure, retry after disconnect during direct receive, map-reclaim concurrency, unsupported upload/lane configuration, and mapping-window fallback.
- Targeted CTest: 6/6 passed for mapped-download fault recovery, mmap profile on/off/unobserved, pwrite profile, and private setup lifecycle.
- The implementation commit is `445de0eac7c64f3b29c331c9d799fd180f2d71b0`. It was merged with the already-pushed compression-controller fix `f72f2f227dff21eeedae6770f6da3c94c1c22cac` as source commit `6bfcd2e2e85e99fa10a53144a32c7286783c7929`.
- Full CTest on `6bfcd2e2e85e99fa10a53144a32c7286783c7929`: `env CPNETFLUX_TEST_TOKEN=<ephemeral-local-test-token> ctest --test-dir /home/Su/projects/CPNetFlux/build --output-on-failure -j1` completed with exit status 0; 852 passed, 12 conditionally skipped, 0 failed, and reached test 864/864. The final `LastTest.log` is timestamped 11:53 CST. `LastTestsFailed.log` remains timestamped 11:17 CST and contains two earlier profile failures, so it predates the final test log.
- `git diff --check` passed.

## Performance status (final result below supersedes the original pending handoff)

- R770-2 fetched and built the same source commit 335b98d82e26bdf160037530bb36c825da309c7a. Both endpoints use RelWithDebInfo with tests, TLS, native RDMA, io_uring, and ISA-L enabled.
- CPNetFlux project version on both endpoints: 0.1.0.
- Final runtime binary SHA-256 matched on R770-1 and R770-2: server 0665d79f73e8ec38c22f205f9d6558c1329159de50a9c389b6209b94d73e3e3c; download client 837ad39af497f76f1df2873a9676c24db2d485b1fbbd7f15d8bd9140fa734af7; upload client 8eab328866dd80e2a228b0469d348f1921965b312934080bc7e28467b3699713.
- R770-2 mapped/direct focused CTest passed 13/13 across the direct range unit filter and mapped receive/profile/fault tests.
- Final follow-up source commit: 335b98d82e26bdf160037530bb36c825da309c7a. It was pushed to origin/codex/V3-GRIDFTP-OPTIMIZED-RERUN-01; remote SHA readback matched.
- R770-1 build after the default-off gate and backing-extent check: cmake --build /home/Su/projects/CPNetFlux/build -j8 passed.
- Relevant final CTest filter passed 13/13, including the direct receive unit cases, profile modes, mapped fault/recovery smoke, and the new backing-truncation unit test.
- Full final CTest used CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE=on for direct-specific unit cases and an ephemeral CPNETFLUX_TEST_TOKEN. Result: 866/866 completed, 853 passed, 13 conditionally skipped, 0 failed.
- The two pre-existing untracked worker-state task documents were left untouched.

The five-pair interleaved 1 GiB A/B is complete. Both arms used the same TCP/tmpfs workload and R770-1/R770-2 committed builds. Evidence is archived under /home/Su/experiments/CPNetFlux/2026-11-24-r770-direct-mmap-admission-01/; the machine-readable decision is ab-result.json with SHA-256 aedc871c56ab1c59e1c5012f7e4f185ac8816f25e99c6dda88b212ef39010950.

| Arm | Median | CV |
| --- | ---: | ---: |
| Existing buffered mmap | 35.0833 Gbps | 1.6337% |
| Direct TCP readv to mmap | 22.2117 Gbps | 3.5276% |

The candidate median regressed 36.6888%. Each of five interleaved pairs favored buffered mmap. All ten transfers passed external SHA-256 and 1,024 verified-chunk checks. Interface error counters had zero deltas; TCP retransmission delta was at most two per sample. The 512 MiB admission completed with 2,048 direct receives and zero buffered writes, correct SHA-256 and 512 verified chunks, zero network error/retransmission deltas, and cleaned tmpfs outputs. Its elapsed time was 0.233229 s, about 18.45 Gbps, so it established correctness only and did not meet the performance gate.

Decision: reject direct receive as a performance optimization. Keep it available only through CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE=on; the default is buffered mmap. The direct path retains its CRC, truncation, retry, and recovery tests. MappedFileWindow now checks the backing file extent after creating a mapping and releases a stale mapping with an I/O error when the file was truncated before the new window became usable. A unit test exercises truncation at a mapping-window transition. This check does not protect against an unrelated process truncating an already validated mapping concurrently.

## Scope and Acceptance

- Input implementation baseline: 34ded615ea72ea2ee805b08075d39d945288f66d. Analysis and route baseline: e19e0f88bc80a7af8778d7abd5d5de08da767cfb.
- Scope: add a bounded direct receive experiment, determine whether removing one payload copy improves complete-task performance, and retain the candidate only if integrity, cleanup, and network gates pass without a material regression.
- Non-goals: changing protocol/frame/CRC format, chunking, pacing, NIC or kernel settings, TLS, compression, RDMA, or recovery semantics.
- Acceptance: relevant unit/smoke tests pass; matching committed source/config and runtime binaries on both endpoints; 512 MiB correctness admission; five-pair 1 GiB A/B; median improvement without network errors; cleanup verified.
- Acceptance outcome: correctness and environment gates passed. The performance criterion failed, so the candidate is rejected as a default optimization and remains opt-in for controlled diagnostics.

## Handoff

1. R770-1 is the only writer. The implementation and A/B source were fixed at commit a404e78b15d9458bb0e5d75184618b75c0e7d88a; baseline comparison used 34ded615ea72ea2ee805b08075d39d945288f66d.
2. Three runtime binary SHA-256 values at a404e78 matched across endpoints: server 5d935faddbe826023de0a9f53bdc9d8e5b4178d8c298ae23f409bbc8b28b4e0d, download client 2f981290eed59dc4a21a4279fa2aa2775dff96b1b2b9a19aecf9387bf7714fae, upload client 8ecba261ddb934105d867a5a008006c7bc8a50901998a9970f5ec0a8825a7f3d.
3. Do not pursue this direct receive path as the next throughput optimization. Return to receiver CPU and bounded pacing feedback evidence before selecting another candidate. No NIC or kernel setting was changed.
4. The 1 GiB A/B artifact includes raw per-sample network counters, checksums, rates, and cleanup. The 5 GiB run remains optional and was not needed after the clear regression.
