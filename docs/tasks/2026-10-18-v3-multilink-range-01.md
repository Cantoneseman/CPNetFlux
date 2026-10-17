# V3-MULTILINK-RANGE-01: first range-to-independent-data-lane slice

status: done
date: 2026-10-18
input commit: 49eb3eea84c4455d4c407f07fe5ec1070e764825 (pushed V3-LINKSET-01)
worktree: /root/projects/CPNetFlux-worktrees/v3-multilink-range-01
branch: codex/V3-MULTILINK-RANGE-01

## Actual problem

V3-LINKSET-01 negotiates LINKS/LINK_CAPS on XCPNETFLUX V2, but the value did not drive the data plane: persistent tree still created connections from fileParallelism, large-file ranges had no stable lane link_id, and LINKS=2 could complete with one data connection while leases/events all reported link0.

## Scope

1. Make persistent data channel count at least the requested link count; preserve default single-link behavior.
2. Give every persistent worker a stable lane link_id; a multi-link request gets at least one independent TCP data session per lane.
3. Keep range and whole-file claims lane-affine in the normal path; preserve existing reclaim/retry takeover after a lane disconnect.
4. Record data-session count and per-lane range completion counts in the summary.
5. Add queue behavior tests and run existing CMake/CTest gates.

## Non-goals

- Do not call multiple TCP connections different NICs, switch paths, or 100Gbps physical links; this slice proves independent transport sessions/lanes only.
- Do not connect GlobalScheduler to multi-link LinkSet; scheduler=global with LINKS>1 is explicitly rejected rather than presenting one link manager as many.
- Do not implement endpoint lists, local-bind routing, weighted runtime selection, failure drain/cut, or ten-link stability; those are later adapter/flowcut work.
- Do not modify GridFTP services, historical directories, or production configuration.

## Acceptance

- With LINKS=N and N>1, persistent channel count is at least N; each lane has an independent worker/data socket and lane IDs are not all link0.
- Normal range work is claimed by its lane worker; the existing reclaim/retry path can hand work to a healthy worker after a lane disconnect.
- Summary records requested links, observed persistent data connections, and range completion count per lane.
- New queue behavior tests, relevant unit tests, and full CTest pass; failures are recorded honestly.
- Before cloud transfer, refresh Shenzhen/Shanghai resources and choose task-specific free ports. Verify bidirectional large-file range transfer with SHA-256 and retain both summaries/logs. Report this only as independent TCP-lane evidence, not as proof of different physical paths.

## Initial affected files

- include/cpnetflux/core/io/dynamic_file_queue.h
- src/core/io/dynamic_file_queue.cpp
- include/cpnetflux/core/io/persistent_tree_transfer.h
- src/core/io/persistent_tree_transfer.cpp
- src/core/io/tree_transfer_client.cpp
- tests/unit/dynamic_file_queue_test.cpp
- docs/tasks/2026-10-18-v3-multilink-range-01.md
- docs/coordination/BOARD.md
- docs/coordination/receipts/03-implementation.md

## Implementation result

- Persistent tree now creates at least `linkSetLinks` independent data channels, assigns stable `link0`/`link1` lane IDs, and maps range/whole-file queue claims to the preferred lane. A ready item can move to another accepting worker when its preferred lane is no longer accepting, preserving reconnect/reclaim behavior.
- The summary now records requested/accepted link counts, observed data connections, lane IDs, and per-lane completed range counts. The global scheduler plus multi-link combination is explicitly rejected until its LinkSet adapter exists; requests above the safe eight-channel limit are rejected.
- A queue behavior test covers two ranges being claimed by distinct lane workers. Existing single-link constructors and scheduling behavior remain compatible.

## Verification

- Intentional red test/build: `/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-red.log` records the expected compile failure before the new queue constructor was implemented.
- Final build: `cmake --build /tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-red-build --parallel 8`; log `/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-build-final-verification.log`.
- Final unit gate: `CPNETFLUX_TEST_TOKEN=V3_MR01_TEST_TOKEN /tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-red-build/cpnetflux_unit_tests`; 334 tests ran, 333 passed, 1 skipped because `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` was unavailable. Log: `/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-unit-final-verification.log`.
- Final CTest gate: `CPNETFLUX_TEST_TOKEN=V3_MR01_TEST_TOKEN ctest --test-dir /tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-red-build --output-on-failure -j 2`; 375/375, 0 failed, 1 skipped. Log: `/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-ctest-final-verification.log`.
- `git diff --check` passed after the final source/test changes.

## Shenzhen/Shanghai evidence

- Resource preflight immediately before the transfer phase: Shenzhen had 66 GiB available on / and 2.5 GiB available memory; Shanghai had 81 GiB available and 2.8 GiB available memory; both reported 2 CPUs. Task ports were checked free before starting temporary servers. Existing services and historical directories were not changed.
- Shenzhen -> Shanghai: an 80 MiB random payload completed with `result=pass`, `bytes_transferred=83886080`, `data_connect_count=2`, requested/accepted links 2/2, lane IDs [`link0`, `link1`], and per-lane range completions [2, 2]. Source and destination SHA-256: `b7e162d1c71e675c00834092a22150b1506a7d70837ec7e3d2b88ea8cbe8cd79`. Client summary: `/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-cross/logs/upload-shenzhen.json`; destination manifest recorded `completed_ranges=0-83886080` on Shanghai.
- Shanghai -> Shenzhen: an 80 MiB random payload completed with the same 2/2 link/session and [2, 2] lane evidence. Source and destination SHA-256: `ac20c938f665a81df00c80b06d3169125ce07efa2694139024e425df2e20f5b5`. Client summary: `/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-cross/logs/upload-shanghai.json`; destination manifest recorded `completed_ranges=0-83886080` on Shenzhen.
- The phase timing records show ranges 1 and 3 on channel/lane 0 and ranges 2 and 4 on channel/lane 1. This proves two independent TCP data sessions with stable range-to-lane assignment and end-to-end integrity. It does not prove separate physical NICs, switch paths, or line-rate scaling. A first reverse-direction attempt on a filtered task port timed out; the test was rerun on a verified free/reachable task port and passed without changing the implementation.

## Next handoff

The next development slice is V3-FLOWCUT-01 / the remaining V3-MULTILINK-RANGE work:
1. Add explicit endpoint and local-bind/path identity so a lane can be tied to an actual network path and the summary can distinguish logical lanes from physical paths.
2. Add a LinkSet adapter for the global scheduler and weighted runtime selection while preserving single-link compatibility.
3. Implement lane drain/cut, failed-lane reclaim, stale/duplicate epoch evidence, and deterministic recovery tests.
4. Run the 1/2/4/8/10-link matrix and repeat bidirectional cloud hashes; only then assess stability/performance claims.

The current slice is complete but does not claim physical multi-path performance, ten-link stability, or production readiness.
