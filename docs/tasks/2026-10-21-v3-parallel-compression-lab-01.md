# V3-PARALLEL-COMPRESSION-LAB-01

Date: 2026-10-21 (Asia/Shanghai)
Owner: current commander, serial execution, no subagents
Feature branch: codex/V3-PARALLEL-COMPRESSION-01
Runtime input: 2f78eca7f59f6b524e9aeccf31176e9be8ffffd6
Input code gates: 426 CTest passes, one expected io_uring skip, zero failures.

## Scope

Deploy the same pinned Release/TLS compression feature to isolated locations on
R730 and R750, verify matching pair binary hashes, exercise real authenticated
directory transfers and measure compression off versus auto. Preserve existing
installed services and all unrelated projects/data. Windows is only the SSH and
artifact transport bridge; authoritative records/analysis remain in Shenzhen.

Verified management: Su@192.168.1.112 (R730), Su@192.168.1.110 (R750).
Verified data: R730 enp130s0np0 192.168.100.1/30, R750 enp177s0np0
192.168.100.2/30. Both interfaces report 100000 Mbps. Recheck routes and counters
at launch. This experiment uses ordinary IP/TCP over the RDMA-capable interfaces.

Matrix: short 5-second iperf3 baselines in both directions; then four shapes
(large repeated, large pseudorandom, 2,048-file dense, 514-file mixed), both sender
directions, compression off/auto, one warm-up plus three measured repetitions
per cell. Use 1,024 MiB large files, eight file/range channels, global fixed
scheduler, dynamic file claims, worker control reuse, tree data reuse, 4 MiB
chunks, required TLS/data TLS, token authentication and CRC32C. Each dataset
stays below 10 GiB. Mode order alternates to reduce ordering bias. Source cache
is warm; no global cache drop or host-wide tuning. Optional bandwidth-limited
follow-up requires an explicit recorded profile and isolated, reversible rules.

## Non-goals

No GridFTP comparison, core/runtime feature changes, native verbs implementation,
desktop packaging, default rollout, service replacement or cross-cloud transfer.
Off/auto quantifies this feature's total effect, not parallel versus serial codec
speedup. Synthetic datasets cannot establish all scientific-data compression gains.

## Acceptance and Evidence

Build succeeds, pin/archive/binary SHA-256 are recorded, intended route/interface
is used, TLS/token works, actual V3/compression/reuse modes are verified, every
accepted transfer has matching source/destination SHA-256, concurrency bounds
hold, and generated payloads/credentials/services are cleaned up. Record raw
logs/JSON outside Git and a compact result report in docs/tasks. Analyze median
time, spread, speedup, protocol and NIC byte reduction, client CPU/RSS and receiver
CPU delta/RSS; distinguish traffic savings from elapsed-time improvement.

Allowed tracked paths: this plan and its result/evidence under docs/tasks; existing
tools/perf/run_parallel_compression_lab.py only if actual execution exposes a
runner issue, with focused self-tests before use. Lab outputs are under the unique
/tmp/cpnetflux-runs/V3-PARALLEL-COMPRESSION-01 workspaces. Persistent optional
binaries/manifests are under /home/Su/.local/opt/cpnetflux-compression/<input SHA>.
Rollback: stop only owned experiment processes, remove owned temporary rules and
generated payloads/credentials, retain logs and isolated versioned binaries. Revert
only explicit task commits if needed. Never reset/clean other source trees.

## Deployment Prerequisite: Bounded Receiver File Descriptors

The first R730 to R750 raw dense warm-up failed after 992 reported commits
(995 executor completions) with five failed files and no reconnect budget. The
R750 SSH process soft RLIMIT_NOFILE is 1024. FileTransaction objects are retained
for published-output integrity validation and currently keep their open temporary
file after successful publication. Investigate with a low-limit loopback test
before changing runtime behavior; do not count the failed run as performance.

Narrow scope extension: release the writer and file handle after successful
publication/checkpoint finalization, while retaining output identity and manifest
integrity callbacks. Add raw and TLS low-descriptor-limit upload/download tests.
Allowed extra paths: src/core/io/persistent_tree_transfer.cpp,
tools/test/run_cpnetflux_tree_fd_bound_smoke.py and CMakeLists.txt. This supersedes
the no-runtime-change non-goal only for this prerequisite defect. No protocol,
retry policy, integrity policy, host-wide limit or formal service changes.
Acceptance: regression fails on input code, passes with the fix, focused
compression/recovery/terminal integrity tests and full serial CTest pass; then
commit/push/read back SHA and rebuild both lab endpoints from that exact commit.

The focused reconnect gate also exposed an intermittent duplicate-range-begin
failure. Shared recovery increments the work item's attempt, but the sender can
overwrite it with an unchanged healthy lane's attempt when work migrates. Extend
the same prerequisite scope to preserve the greater work/lane attempt and cover
the migrated range through an actual socket sender/receiver test in
tests/unit/persistent_data_session_test.cpp. Do not relax receiver replay checks.
The original full run also lacked the private CPNETFLUX_TEST_TOKEN and failed
two authentication fixtures; supply that test-only variable without logging it
for the final full suite. Existing successful runs do not erase these failures.

The corruption fixture races parallel dispatch: its fault hook assumes a.bin
already committed while another worker can hit the reset threshold first.
Allow tools/test/run_cpnetflux_reconnect_smoke.py to serialize only this fixture
with one lane and one pending file; the large-range/recovery scenarios retain
their multi-lane coverage. Confirm repeated corruption rejection still reports
checksum_mismatch rather than counting a missing fixture file as a core failure.

## Explicit Lab Scheduler Profile

The first fixed runtime 4a51e6e run passed all eight dense cells but rejected the
1 GiB large-random warm-up before any payload. Its 16 ranges are 64 MiB each;
the global scheduler's default 64 MiB aggregate admission budget cannot hold
eight concurrent ranges. Correct the harness profile (no core change) to a
512 MiB metadata reservation budget, capacity 100 Gbps, and conservative 1 ms
RTT for both modes/directions. Record the runner SHA separately from runtime
commit, verify observed range completions, and restart the comparison matrix.
