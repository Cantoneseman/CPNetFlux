# V3 Direct Receive 15 — bounded feasibility gate

Date: 2026-11-09. Input: 1bdd7bc5aac86f6f609594613b48f2be1f719d37.
Branch: codex/V3-GRIDFTP-OPTIMIZED-RERUN-01. Sole writer: R770-1.

## Scope and hypothesis

Scope14 attributes 4.618 thread CPU seconds/8GiB to mapped copy including first
touch, versus 0.148s map/unmap. Hypothesis: receiving TCP DATA directly into the
same bounded file window can remove an extra userspace copy. Kernel copy and
first-touch costs remain; no zero-copy or throughput promise is implied.

Implement one diagnostic candidate in the existing tcp-frame benchmark: explicit
mapped_copy versus mapped_direct sinks, real TCP/V3 frames, CRC32C, the actual
MappedFileWindow, and WorkExecutor IO leases. Defaults remain memory-only. A fresh
target of at most8GiB is partitioned between at most4 workers; each worker has at
most one64MiB mapping. Both arms use identical validation, leases, frame sizes,
whole-file checksums, and acknowledgement boundaries. No target prewarming.

Non-goals: integration into receivePersistentTree, checkpoint/recovery/publication,
runtime fallback, native RDMA, TLS, compressed/sparse frames, production readiness,
sender tuning, extra concurrency, THP/affinity/network changes, 100Gbps claims.
This is a real framed payload experiment, NOT a complete V3 file-task benchmark.

## Safety and lifetime contract

- Read/decode/validate the64-byte header before touching output. Require exact
  identity/generation/attempt/range, expected offset/size/flags/status and bounds.
- Acquire WorkExecutor.startIo BEFORE borrowing a writable mapping. Balance IO
  on success and every exception; reject failed lease. One synchronous owner per
  lane, no reclamation while that thread runs. Mapping dies before descriptor.
- Receive CRC prefix and first mapped span using TCP-only scatter read. Reject
  TLS/RDMA explicitly. Correctly advance partial reads, retry EINTR, fail on EOF.
- A borrowed span ends at file/window bounds and is invalid after next borrow,
  write or close. Compute CRC on each segment BEFORE replacing its mapping.
  Cross-window frames are segmented without keeping two windows alive.
- Written-but-unverified bytes are tentative. Only after complete frame CRC passes
  may PersistentDataSession advance; FILE_END whole checksum must pass before ACK.
  Bad CRC, short read, bad identity, stale attempt, or peer shutdown must fail with
  no success ACK. A fatal SIGBUS terminates the isolated diagnostic; no shared
  service recovery claim. No success file is published by this prototype.
- Runtime integration, if later justified, additionally needs RangeFileCoordinator
  writing pin held across network IO and CRC, attempt fencing, checkpoint and
  publication/recovery fault gates. A WorkExecutor lease alone is insufficient.

## Acceptance and gates

1. Record old executable failing the new mapped-sink behavioral contract. Add
   meaningful span/scatter tests and TCP smoke tests: uneven sizes,4 lanes,
   cross64MiB boundary, corruption, truncation and header identity/offset rejection.
2. Related CMake/CTest, including existing mapped/profile/resume tests, must pass.
   Commit explicit files, push verified GitHub remote and read back SHA. R2 only
   receives that fixed commit; same config/version/binary SHA before interhost IO.
3. Run5 interleaved fresh8GiB4-lane pairs if resources permit. Measure ready-to-ACK
   elapsed including faults, CRC, map retirement and lease completion; also record
   process wall/CPU/setup/readback separately. Independent full target readback
   checks the known sender pattern outside payload timing; cleanup every run.
   Record route, explicit bind and data-interface byte counters on both hosts.
4. Feasibility gate: >=10% median speed gain, each arm CV<=5%, integrity/resource
   gates all pass. Negative/inconclusive results close or defer this candidate;
   do not silently promote it. Positive result only authorizes a later scoped
   V3 runtime implementation and5 complete-task pairs with the same performance
   gate. Scope15 alone never passes the complete-task performance gate.
5. MemAvailable>=12GiB, owned VmSwap0, host pswpout delta0, output<=8GiB and
   active mappings<=4*64MiB. Monitor exact owned PIDs; abort/clean on violation.
   Preserve production PID/start R1:126766/Oct30 19:36:11, R2:164834/19:36:13.
   Preserve the two unrelated October14 untracked docs and all unrelated data.

## Intended files and evidence

include/cpnetflux/core/io/framed_data_socket.h,
src/core/io/framed_data_socket.cpp,
include/cpnetflux/storage/mapped_file_window.h,
src/storage/mapped_file_window.cpp,
tools/perf/tcp_frame_bench.cpp,
tests/unit/framed_data_socket_test.cpp,
tests/unit/mapped_file_window_test.cpp,
tools/test/run_cpnetflux_direct_receive_smoke.py, CMakeLists.txt,
this task/result and docs/ROADMAP.md. No runtime receiver changes.

Evidence: /home/Su/experiments/CPNetFlux/2026-11-09-v3-direct-receive-15.
Owned tmpfs: /dev/shm/cpnetflux-v3-direct-receive-20261109.
Tools checked: shell/terminal/apply_patch/chat-send exposed, SSH/SCP/Codex CLI
present. No chat dispatch, agents, worktrees or desktop use. Execution/debugging/
TDD/verification/planning skills applied within the authorized serial plan.
