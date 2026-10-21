# V3-PARALLEL-COMPRESSION-01

Date: 2026-10-21
Status: first implementation/code gates complete; lab acceptance pending (both SSH endpoints timed out)
Owner: current commander only; serial edits in Shenzhen; no subagents
Branch: codex/V3-PARALLEL-COMPRESSION-01
Input CPNetFlux commit: 390774cad2eaf3b58cb19e0a86ca3d3e02a2699b
Peer behavior reference: neoClav/Computing-Power-Network-Transmission, branch feature/parallel-compression-pipeline, commit c0a5159077a055d5a84ff1436a54e2ac760d5513
Authoritative source: /root/projects/CPNetFlux
Current isolated checkout: /root/projects/CPNetFlux-worktrees/v3-parallel-compression-01
Existing unrelated untracked files to preserve unchanged:
- docs/tasks/2026-10-14-v3-worker-state-01b-result.md
- docs/tasks/2026-10-14-v3-worker-state-01b.md

## Goal

Create an isolated CPNetFlux V3 feature branch that integrates the peer's parallel-compression capability into CPNetFlux's own V3 transfer path, then validate it on the R730/R750 lab pair. The frozen V3 candidate remains the rollback and comparison baseline; this branch is experimental until both correctness and lab evidence pass.

Use the peer branch as behavioral/algorithmic reference only. Its repository currently has no tracked LICENSE/COPYING/NOTICE file, so do not copy or transplant its source files. Reimplement the required behavior using CPNetFlux-owned APIs and protocol.

## Scope

1. Provide bounded parallel compression for V3 payloads, shared across concurrent transfer workers, with a global job/byte bound and backpressure; preserve each range's exact offset order, cancellation, resume, checksum, and transactional commit behavior.
2. Integrate codec selection and framing into CPNetFlux. If optional zstd/LZ4 are implemented, make build/runtime availability explicit and negotiate supported codecs before the first compressed payload. Reject unsupported explicit requests; never silently claim a codec or downgrade.
3. Keep raw transfer available and compare it to the feature using the same V3 binary. Do not change the frozen candidate/default branch.
4. Add unit and loopback tests first for codecs, malformed/bounds cases, concurrency limits, ordered writes, cancellation/failure, checksum and resume.
5. After all code gates pass, inspect R730/R750 reachability, storage, CPU, TCP route/interface and RDMA-link IP configuration. Run a bounded lab matrix only when both hosts and the selected interface are verified. Preserve all source/target SHA-256 values, actual codec/worker counts, compression ratio, wire bytes, elapsed time, CPU/pressure evidence and cleanup result.

## Non-goals

- No changes to V1/V2 or GridFTP; no GridFTP comparison in this task.
- No desktop UI, authentication-policy, remote directory semantics, or unrelated V3 recovery changes.
- No native RDMA verbs implementation or claim that TCP tests prove verbs performance. The experiment may use the RDMA-connected interface only if ordinary IP/TCP is verified on it.
- No default-policy promotion or modification of the frozen V3 candidate.
- No Shenzhen-Shanghai or long-duration experiment.

## Acceptance

### Code

- Compression work has explicit process-wide worker, queued-job and in-flight-byte bounds; simultaneous tree transfers cannot create an unbounded queue.
- Per-file/range payloads are emitted in exact offset order. Identity/range/checksum/manifest/finalize semantics remain unchanged.
- Cancellation, deadline, socket failure, and compression failure terminate without deadlock, lost range, unchecked output, or worker lifetime violation.
- Each available codec has round-trip and malformed/truncated/oversize tests; capability negotiation proves sender/receiver support before compressed data is sent.
- Targeted unit/loopback tests, full serial CTest, CMake build, and relevant release/script gates pass.
- Explicit file-only commits are pushed to verified origin and the remote branch SHA is read back.

### Lab experiment

- Both lab endpoints run binaries from the same feature commit/build configuration; binary hashes match and no formal service is replaced.
- A raw/no-compression control and parallel-compression mode are compared on a large-file, dense-directory, and mixed/compressibility workload. Each generated dataset stays within 10 GiB; use at least three measured repetitions after one warm-up where resources allow.
- At least source and destination SHA-256 match for every accepted run. Record per-run interface/route, actual workers/codecs, bytes, elapsed time, CPU/pressure and cleanup.
- Findings distinguish compression CPU cost, byte reduction, transfer time and link ceiling. No generalized cross-domain or native-RDMA claim is made.

## Allowed paths

- Build/dependency integration: CMakeLists.txt, cmake/**, package/deb/** only where required by optional codec runtime dependencies.
- V3 compression and protocol: include/cpnetflux/core/io/** compression/payload pipeline/persistent-tree headers; include/cpnetflux/core/protocol/{compressed_data.h,frame.h}; include/cpnetflux/config/tree_transfer_options.h.
- Corresponding implementation: src/core/io/** compression/payload pipeline/persistent tree/client; src/core/protocol/{compressed_data.cpp,frame.cpp}; src/config/tree_transfer_options.cpp; src/protocol/control/control_server.cpp only for V3 codec negotiation.
- Tests: focused tests/unit/** compression/frame/persistent transfer/config; existing loopback test harnesses and narrowly scoped tools/test/** only as needed.
- Evidence: this task, its result/evidence under docs/tasks/, docs/DECISIONS/2026-10-21-v3-parallel-compression-01.md, docs/RESEARCH_BASELINE.md, and the current-status section of docs/coordination/BOARD.md.
- Experiment scripts/evidence under tools/perf/** and /tmp/cpnetflux-runs/V3-PARALLEL-COMPRESSION-01/**; never add payloads, credentials, generated builds or raw large logs to Git.

## Rollback

The feature branch is independent of the frozen candidate. Roll back only explicit feature commits with git revert, or return to the previous branch while preserving task evidence. Do not reset/clean the workspace, delete the two pre-existing untracked task records, change formal server services, or delete user data.

## Implementation handoff

See 2026-10-21-v3-parallel-compression-01-result.md for code changes, gates and the lab command.
The root checkout received unrelated desktop/network-binding work during this task. Those changes were preserved; this feature continues serially in its isolated Shenzhen checkout from the documented input commit. It does not include or replace the later desktop/network-binding branch.

The first feature uses the existing zlib wire codec with four process-wide workers, sixteen in-flight jobs, and a 256 MiB payload-buffer ceiling. Raw buffers and worst-case encoded output remain reserved until the result is sent or discarded. Fixed worker count separately bounds zlib workspace; the byte ceiling is not a total process RSS guarantee. Optional zstd/LZ4, adaptive codec selection, desktop controls and worker-count tuning are not implemented in this first slice.

Both V3 upload and download explicitly negotiate COMPRESSION=zlib. An unsupported peer causes a pre-payload failure; an already negotiated frame may use the existing raw format when compression is ineffective or fails, with the reason recorded. Default compression remains off.

Remaining acceptance: verified R730/R750 data-interface configuration, isolated deployment of the pinned commit and the measured lab matrix. No lab throughput result is claimed.

## Directory composition follow-up (2026-10-21)

Input commit: b88b0c17f450a81d0a302a52ba20f7d2b3421a91. The user specifically requested directory/compression integration. Continue serially in the same branch/checkout; preserve the separate root branch and its untracked documents.

Scope: exercise negotiated auto compression with dynamic directory scheduling, worker control/data reuse, global fixed/adaptive scheduling, mixed nested small/random/empty files, large-file ranges, actual data disconnects and bounded recovery failures. Reuse the existing real TCP fault harness; choose a compressed-wire byte threshold so a reset cannot be silently skipped when compression reduces traffic.

Non-goals: no new codecs, UI, authentication policy, native RDMA, performance experiments, formal-service deployment or general recovery refactor.

Acceptance: successful upload/download trees match SHA-256, codec and dynamic V3 mode are confirmed, large-file range dispatch is observed, the injected fault actually fires and existing retry-budget checks still hold. Exhaustion/deadline/identity/content failures must remain bounded failures. Run focused composition tests and the full serial CTest; commit only task files and push/read back origin SHA.

Expected paths: tools/test/run_cpnetflux_reconnect_smoke.py, CMakeLists.txt, tools/perf/run_parallel_compression_lab.py (explicit global scheduler policy/metrics), this task and its result record. A reproducible core fault, if found, may be fixed only in the previously allowed V3 compression/persistent-tree/control files with an appropriate failing regression first.

Rollback: revert the explicit follow-up commit on this feature branch. Keep b88b0c17 and the frozen candidate available; do not remove user data, touch formal services or reset other branches.


### Reproduced terminal-error fix

The compressed-directory corruption case intermittently reports recovery-budget
exhaustion instead of the committed-output checksum failure. A batch cancellation
currently shuts down its control sockets before the handler sends its terminal
reply. Add a socket-level regression, keep controls open for committed-output
integrity failures, and propagate the batch's integrity cause to sibling replies.
Additional allowed files: `src/core/io/persistent_directory_batch.cpp`,
`src/protocol/control/control_server.cpp`, and
`tests/unit/persistent_data_session_test.cpp`.
