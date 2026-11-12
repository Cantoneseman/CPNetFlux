# Source Residency Ownership Diagnostic 39 Result

Date: 2026-11-10. Status: bounded diagnostic complete; no transfer or throughput sample.
Plan commit: `3aeff8bf50a487a7cfc87828c22a303ffc706522`.
Implementation commit: `48eba5bbe660781bdcc098a1ab80e725417db008` (pushed; GitHub SHA readback matched).
R770-1 was the sole writer; R770-2 ran the fixed commit.

## Decision

The proposed Entry-lock CRC32C combine prototype is NO-GO for the current 256 KiB DATA path.
Its measured combine work is about 0.1084 ms/GiB, compared with roughly 0.85 receiver CPU
seconds/GiB in recent full-task samples, or about 0.013% of that sampled CPU budget. Existing
range-write tests already cover independent progress while another write blocks and draining a
partial failed write before retry slot reuse. Older 7-12% combine samples came from 64 KiB DATA
and do not describe the current path. No C++ or checksum runtime change was made.

The new bounded diagnostic completed one R770-2 source preparation. The 8 GiB tmpfs source was
fully resident at both mincore observations; the preparation and follow-up probe used the same
PID/start time and cgroup. Host pswpout did not increase, the process and cgroup leaf had zero
swap, and all resource thresholds passed. This clears the source-preparation uncertainty for this
single observed run only. It does not explain the prior 331.5 MiB host swap-out, prove stable
residency across runs, or provide a file-transfer rate.

## Diagnostic Evidence

The source was one random 8 MiB block repeated 1024 times, compression off. Its SHA-256 was
`fdbabde8331e1f0e4f875e77c3338dbf48ae46027668fd5459653692291215a8`.
Both mincore observations reported 2,097,152 of 2,097,152 4 KiB pages resident.

| Observation | Result |
|---|---:|
| Preflight MemAvailable | 30,214,492,160 B (28.14 GiB) |
| Minimum MemAvailable during write/SHA/mincore | 21,662,035,968 B (20.18 GiB) |
| /dev/shm free before / after | 16,382,631,936 B / 16,382,631,936 B |
| Host pswpout before / after | 2,292,431 / 2,292,431 pages; delta 0 |
| Preparation process swap peak / final / probe | 0 / 0 / 0 B |
| Preparation PID / start ticks | 1,339,341 / 97,927,352 |
| Cgroup membership | `/user.slice/user-1002.slice/session-26234.scope` |
| Payload root / cleanup | absent after run; cleanup passed |

Four cgroup-v2 snapshots were taken in the same process: before write, after write, after SHA,
and after mincore. The leaf memory.current values were 16,240,640 B, 8,634,245,120 B,
8,634,335,232 B, and 8,634,404,864 B. Leaf memory.swap.current stayed 0 B; memory.events
high/max/OOM counters stayed zero; leaf memory pressure totals stayed zero. The user slice
parent `/user.slice/user-1002.slice` showed 50,077,696 B of pre-existing memory.swap.current
throughout, with no change to the preparation process's leaf or the host pswpout counter. The observed leaf/parent memory.max
and memory.high values were unlimited. These readings describe this execution and do not identify
the cause of the earlier run's host-wide counter change.

The diagnostic created no server and transferred no file payload. Its report says
`outcome=prepared_resident`, `residency_gate_pass=true`, `same_preparation_process=true`,
`same_preparation_cgroup=true`, `cleanup_pass=true`, and `dev_shm_free_delta_bytes=0`.
No performance matrix was run.

Evidence on R770-2:
`/home/Su/experiments/CPNetFlux/2026-11-10-v3-source-residency-ownership-39/`.
A local read-only mirror of the JSON evidence is under `_codex_stage39/evidence/`.
The main report SHA-256 is
`ced5d6f0b107f3a515f36a980df73966039df622921d7a8199e0a89449813c4f`; local and R770-2 copies match.

## Implementation And Verification

`tools/perf/run_summary_default_gate.py` now records cgroup-v2 leaf and ancestor memory/swap
counters and pressure with the same PID/start ticks through preparation and follow-up mincore.
If an older matrix invokes its probe from a different SSH process after the original cgroup leaf
has disappeared, the tool records that the leaf is missing and still captures surviving
ancestors. Failed write/SHA/mincore guards save the phase error, exact materialized bytes,
host pswpout delta, process swap, and cgroup snapshots before cleanup. Preflight blocks an
already-swapped diagnostic process. The dedicated action is local to R770-2 and starts no server.

Tests were added first and observed RED for missing failure-phase evidence, then GREEN.
The Python suite passed 17/17 on both peers. The relevant CTest pair passed 2/2 on both peers:
`cpnetflux_summary_default_gate_test` and `cpnetflux_owned_process_profile_test`.
CMake builds passed with no work to do. Both peers used RelWithDebInfo, RDMA/TLS/ISA-L/io_uring
enabled, and the same fixed commit. Three runtime binary SHA-256 values matched:

- server: `125db9bf5e21cf98de5ef674d097b894cdcaabe9ed5dbadebdf3c586570e45a5`
- download client: `20f600140276a636740b43deb172d5ecb8bab81f593733452b7e22a91c76c30c`
- upload client: `b6c9e9c53360ca0dd0ef8a071518834dd80208ac66a3c1fe38af365fb3fb76b0`

The diagnostic action ran once on R770-2 at commit `48eba5bbe660781bdcc098a1ab80e725417db008`;
no retry was made. The marked source root was removed, and /dev/shm free space returned exactly
to its pre-run value.

The exact diagnostic command was:
```sh
python3 tools/perf/run_summary_default_gate.py --action resource-diagnostic \
  --evidence /home/Su/experiments/CPNetFlux/2026-11-10-v3-source-residency-ownership-39 \
  --payload-root /dev/shm/cpnetflux-s0-source-residency-ownership-39 \
  --commit 48eba5bbe660781bdcc098a1ab80e725417db008
```

These verification commands ran on both fixed peers at the implementation commit:
```sh
python3 -m py_compile tools/perf/run_summary_default_gate.py tools/test/run_summary_default_gate_test.py
python3 tools/test/run_summary_default_gate_test.py
cmake --build build -j4
ctest --test-dir build -R 'cpnetflux_summary_default_gate_test|cpnetflux_owned_process_profile_test' --output-on-failure
```

## Next

The 8 GiB full V3 task gate remains unmeasured; the last comparable full-task reference is still
27.291 Gbps from scope17. The 1 GiB gains from Milestone35 cannot replace that gate. Follow the
Milestone35 route: perform one bounded correlation of the existing `rx_missed_errors` event with
per-queue/IRQ service evidence, then prepare a separate, explicit 8 GiB full-task plan with the
unchanged resource thresholds and at least five interleaved pairs. Keep every interface event and
failed sample. Do not repeat the source-only preparation as a performance result or claim that
the prior swap-out cause is solved.
