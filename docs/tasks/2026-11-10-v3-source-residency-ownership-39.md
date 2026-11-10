# Source Residency Ownership Diagnostic 39

Date: 2026-11-10. Status: scoped; diagnostic not yet run.
Input: `1d1d5bf099b44b1f3c2ba6626b4814af5e22579c`.
R770-1 is the sole writer; R770-2 receives a fixed commit.

## Scope

First close the proposed Entry-lock CRC32C combine candidate. The current
256 KiB combine microbenchmark measured 0.1084 ms/GiB, versus about 0.85
receiver CPU seconds/GiB in the recent full-task samples. This is about 0.013%
of sampled receiver CPU, so no runtime prototype is justified. The older
7-12% `crc32cCombine` samples came from a 64 KiB DATA path and do not describe
the current 256 KiB path. Existing direct-range tests already cover an
independent range progressing during blocked I/O and a higher-attempt retry
draining a partial failed write before slot reuse.

Then add a bounded source-residency ownership diagnostic to the existing S1B
gate driver. It will capture the source-preparation process PID/start ticks,
cgroup membership, and cgroup-v2 memory/swap counters at write, SHA, mincore,
and the follow-up residency probe. Run exactly one 8 GiB source preparation on
R770-2, with no server start or file-payload transfer. Preserve existing
MemAvailable, host swap-out, process swap, and `/dev/shm` guards. Retain exact
residency counts on failure and clean only the marked diagnostic root.

Evidence belongs under
`/home/Su/experiments/CPNetFlux/2026-11-10-v3-source-residency-ownership-39`.

## Non-goals

- No C++ runtime, protocol, checksum, checkpoint, default, or concurrency changes.
- No performance matrix, server restart, or inter-node payload transfer.
- No swap, THP, cgroup, mount, NIC, or system configuration changes.
- No automatic retry after a failed resource or residency guard.
- This diagnostic cannot establish an 8 GiB throughput result or 100 Gbps readiness.

## Acceptance

- Add focused tests first for cgroup-v2 snapshot parsing, same-process phase
  identity, failure evidence retention, and exact cleanup; observe RED before
  implementation and GREEN afterward.
- Keep the existing 8 GiB size and all resource thresholds unchanged. Stop at
  the first failed guard, preserve evidence, clean the marked root, and do not
  retry in this task.
- Record preparation and probe PID/start ticks, cgroup membership, source
  cgroup leaf and ancestors, `memory.current`, `memory.swap.current`,
  `memory.swap.max`, `memory.events`, `memory.stat`, `memory.pressure`, exact
  mincore counts, and host `pswpout` deltas.
- Verify no payload server or data socket was started, the source root is absent
  on R770-2 after cleanup, and `/dev/shm` has returned to its pre-task state.
- Run relevant Python tests and CTest on both fixed peers; compare commit and
  runtime binary hashes before any network use (none is planned here).

## Affected files

- `tools/perf/run_summary_default_gate.py`
- `tools/test/run_summary_default_gate_test.py`
- `docs/tasks/2026-11-10-v3-source-residency-ownership-39.md`
- `docs/tasks/2026-11-10-v3-source-residency-ownership-39-result.md`
- `README.md`, `docs/README.md`, `docs/ROADMAP.md`
