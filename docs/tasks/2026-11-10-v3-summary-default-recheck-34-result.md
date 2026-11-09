# S1B Summary Default Recheck34 Result

Date:2026-11-10. Decision:HOLD_RESOURCE_RESIDENCY. One authorized attempt ended
before the first client/performance sample. Default original; cached remains opt-in.
Input:6cfb1bb7c516b7677dadb8a255f5476525b36e3d.
Measured preparation:2bb75d787be61b738327d93d1058ad86bb23a4b3.
Diagnostic correction:8a486d7f3997bbafa78f0c72cb6484261d04f6e0.
All pushed/read back; final documentation commit and peer closure:closure.json.
R1 serial writer; R2 fixed-commit build/test/source peer; no agents/worktrees.

## Authorized Attempt And Result

The new evidence relative to Gate23 was Ownership32/33's zero-swap interval.
Before this attempt, both peers built and passed19 relevant CTests and matched
commit/configuration/three binary SHA-256 values. Then both passed continuous
60-second windows with241 samples each at0.25s intervals:

| Preflight | R1 | R2 |
|---|---:|---:|
| Minimum observed MemAvailable |28.5443GiB|28.0014GiB|
| Host pswpout increment |0|0|
| Monitor owned VmSwap |0|0|
| /dev/shm free |more than9GiB|more than9GiB|

single-attempt.json records the fixed commit, command and preflight digests;
the exclusive attempt file was created once. The existing8GiB/five-pair driver
was invoked once, without performance-contract or resource-threshold changes.

The source was fully materialized8GiB, one random8MiB block repeated1024,
compression off. The saved write-stage resource record reports minimum available
21684768768 bytes (20.1955GiB), process VmSwap0 and host swap delta0. Source SHA
completed and the returned mincore observation reported2097152/2097152 pages
resident. However, the next pre-sample residency observation failed all_resident.
No client was started; the isolated sender reported zero file phases. There are
zero complete performance samples, no T_ready/Gbps result and no A/B evaluation.

The old driver did not save the failing residency object, so the exact later
resident-page count is unknown. Do not infer it from host swap counters or repeat
the allocation to reconstruct it. The following counter change is independently
preserved between the write-stage baseline and isolated server startup:

- R2 pswpout2207568 ->2292431:84863 pages,347598848 bytes (331.4961MiB).
- Server supervision started after that change and recorded a delta0 relative to
  its newly reset baseline, which did not cover the preceding preparation gap.
- This is a host-wide counter. It does not identify the swapped process/pages,
  or prove that331.5MiB of the source was swapped. Source SHA/residency/startup and
  unrelated activity overlap within the unobserved interval.

The source and both exact marked roots were removed; the isolated server stopped.
Original production identities remain R1 126766/1191040 and R2 164834/1679976.
New interface error/drop/missed deltas are0. Management SSH and a readiness TCP
control connection were used; there was no file-payload transfer. Scope17's
historical interface+94 remains open.

## Implemented Diagnostic Correction

The failed run established three driver defects; they are fixed without a second
preparation or performance attempt:

1. Seed diagnostics previously finalized before source SHA/mincore. Extend guards
   through the write/SHA/residency boundaries, record the failing phase and retain
   final host swap delta/materialized bytes even on failure. Do not publish a
   successful source receipt when the final preparation counter check fails.
2. A fresh server resource baseline could hide earlier swap-out. Each source probe
   now carries the original seed baseline, current host counter, available memory
   and probe VmSwap; save the report before rejecting residency or resource gates.
3. Cleanup always expected80 completed sender phases, masking a pre-sample abort.
   Enforce that count only for a completed matrix, preserve case-failure phase/
   sample/completed-count evidence, and still stop/clean on failure. No resource,
   SHA, chunk-coverage, pairing or performance acceptance condition is relaxed.

Write/hash/residency boundary checks are not a continuous CPU/memory timeline.
Memory and process-swap extrema are sampled, while the monotonic host swap counter
can detect an intervening increment. This correction is validated with bounded
fixtures; its behavior under another8GiB load has not been exercised this round.
Production C++/protocol/default/checkpoint/lease/ACK code and CMake are unchanged.

## Verification

Two regression cycles first failed against the preceding implementation, then
passed. The four added tests cover SHA-phase swap escaping the old guard,
saved/non-masked pre-sample residency failure, a fully resident source with
intervening host swap-out, and carrying the original baseline into a new probe.
The diagnostic suite now has12 tests. Fixture SHA-swap injection writes1024 bytes;
the pre-existing first-write-abort fixture writes8MiB, never another8GiB seed.

Relevant configured CTest group: tree hash, summary selectors and A/B/default
driver, baseline clock/ownership, ordinary mmap config/fault/unobserved, interrupted
resume, corrupt checkpoint and target-conflict/idempotency. Before the attempt:
R1 19/19 (13.10s), R2 19/19 (13.17s). After correction: R1 19/19 (13.06s),
R2 19/19 (13.11s), including the12-test Python suite. Both full builds pass and
same fixed8a486d7 configurations/three runtime hashes match the measured commit.

```sh
python3 tools/test/run_summary_default_gate_test.py
cmake --build build -j8
ctest --test-dir build -R "$(cat /home/Su/experiments/CPNetFlux/2026-11-10-v3-summary-default-recheck-34/ctest-pattern.txt)" --output-on-failure
```

These are correctness gates. They do not authorize rerunning the matrix. The
original failure, both expected red-test logs, green logs and peer receipts are
retained; no failed performance sample or resource gate was discarded.

## Evidence And Next Boundary

Evidence root:/home/Su/experiments/CPNetFlux/2026-11-10-v3-summary-default-recheck-34.
analysis.json separates write-stage observations, the later host counter gap and
the unknown failed residency count. peer-capture/ preserves the original R2
records; measurement-source/ preserves exact measured scripts. postmortem-r2.json
contains read-only memory/VM policy and current SSH cgroup/ancestor observations.
Those surviving ancestors show no finite memory.high/max or recorded limit/OOM
events; the original short-lived seed session was not captured. Post-cleanup
counters are not a causal timeline. No swap/cache/THP/cgroup/network policy changed.
Source/evidence digests and local mirror are checked; evidence is outside Git.

Do not retry from another idle stable window. Before any new separately scoped
acceptance, explain the loaded preparation's reclaim/swap behavior and resource
ownership, and establish a suitable unchanged-gate environment. Capture the same
preparation process/cgroup across write, SHA and residency if a future diagnostic
is authorized. Do not treat high MemAvailable, idle stability, VmSwap0 or current
unlimited cgroup settings as proof that a loaded tmpfs source stays resident.

S1 opt-in small-file gains retain their original scope; default promotion is not
accepted. Large-file receiver candidates remain paused under Budget33, reference
approximately27.3Gbps and30-50/70-85/100Gbps goals remain unpassed, RDMA remains
paused. This round adds diagnostic correctness and an environment finding, not a
new runtime speedup. No automatic preparation retry, new receiver profile or
parallelism sweep is scheduled.
