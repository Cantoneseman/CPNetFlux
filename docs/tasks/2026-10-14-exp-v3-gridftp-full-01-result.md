# EXP-V3-GRIDFTP-FULL-01 Result

Date: 2026-10-14
Input commit: 80c576b10254937e7cac6b46e63befb2282f95d6
Worktree branch: codex/EXP-V3-GRIDFTP-FULL-01

## Outcome

Blocked at the full-profile compatibility gate. No GridFTP or CPNetFlux
performance payload was transferred, so this task has no throughput comparison.

The required CPNetFlux profile included token authentication, control TLS, data
TLS, CRC32C, dynamic file/range scheduling, persistent tree data-session reuse,
automatic compression, worker control reuse, and the global adaptive scheduler.
The zero-payload client invocation used those options and exited before opening
a control or data connection:

- Summary: result=fail, control_connect_count=0, data_transfer_count=0,
  total_bytes=1, transferred_bytes=0.
- Error: "V2 persistent tree configuration is unsupported; explicitly select
  --data-session-reuse off for per-file V3 orchestration".
- The committed persistent eligibility predicate requires scheduler mode Off;
  global scheduler uses the separate per-file V3 path. This prevents measuring
  the requested all-in-one V3 profile. No option was silently disabled.

## Environment

- R730 direct link: enp130s0np0, 192.168.100.1/30, 100000 Mb/s full duplex,
  MTU 9000; route to R750 uses this interface.
- R750 direct link: enp177s0np0, 192.168.100.2/30, 100000 Mb/s.
- Existing GridFTP GSI service remained listening on
  192.168.100.2:22522; it was not stopped or reconfigured.
- Before the gate, R730 had 62 GiB free and R750 had 77 GiB free.
- R730 GSI proxy was refreshed and verified; its reported expiry was
  2026-10-15 00:21:46 local time. No credential material was copied into the
  worktree or this record.
- This would have measured TCP/IP over Ethernet on the direct link, not native
  RDMA verbs.

## Build and tests

Configured a clean Release build from the input commit with TLS and io_uring
enabled:

- cmake -S /tmp/cpnetflux-runs/EXP-V3-GRIDFTP-FULL-01/src -B
  /tmp/cpnetflux-runs/EXP-V3-GRIDFTP-FULL-01/build -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON
  -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=ON
- cmake --build /tmp/cpnetflux-runs/EXP-V3-GRIDFTP-FULL-01/build --parallel 2
- CPNETFLUX_TEST_TOKEN=<test-only> ctest --test-dir
  /tmp/cpnetflux-runs/EXP-V3-GRIDFTP-FULL-01/build --output-on-failure
  --timeout 120

CTest: 295/295 passed. The TLS and io_uring implementations were enabled.
Logs are under /tmp/cpnetflux-runs/EXP-V3-GRIDFTP-FULL-01/.

## Next step

Integrate the global scheduler with persistent V3 workers and their queue/range
leases, then add compatibility and loopback regressions for the full profile.
The separate in-progress V3-WORKER-STATE-01B work explicitly excludes global
scheduler integration and is uncommitted; it was not used here. After that work
is committed and tested, rerun the requested matched GridFTP GSI comparison
under the same 10 GiB hard cap. The prior raw baseline is not a substitute for
this full-profile comparison.
