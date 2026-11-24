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

## Performance status

No 512 MiB or larger performance run was started in this phase. The candidate must first be committed and pushed from R770-1, then built from that exact commit on R770-2. Verify both endpoints use identical configuration and that the three runtime binary SHA-256 values match before any transfer.

The next admission run is a single 512 MiB inline-mmap profile under the established TCP/tmpfs configuration. Accept it only if direct receive accounts for the full logical payload, buffered-copy bytes remain zero, protocol terminal state and external SHA/chunk checks pass, all mappings and temporary resources are released, and interface error/retransmission gates remain clear. Only after that run passes should the five-pair interleaved 1 GiB A/B proceed.

## Handoff

1. Push the current fixed source tree and verify the remote SHA readback; preserve the two pre-existing untracked worker-state documents.
2. Inspect R770-2 state, synchronize the exact branch tip, build with the matching configuration, and compare commit, version, and three runtime binary hashes.
3. Run the bounded 512 MiB admission experiment and record network counters before and after. Do not adjust NIC or kernel settings.
4. Only if 512 MiB passes every integrity, cleanup, and network gate, proceed to the five-pair interleaved 1 GiB A/B.
