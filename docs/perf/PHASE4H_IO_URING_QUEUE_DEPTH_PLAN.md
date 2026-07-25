# Phase 4H io_uring Queue Depth / Batching Plan

Phase 4H is a design-only preparation for an optional file-IO-only io_uring queue depth prototype. It does not change network epoll, the default POSIX backend, STOR/RETR framed data, checksum, manifest, resume, or final verify semantics.

## Goal

Phase 4G proved that the real liburing path is correct and comparable, but the v1 synchronous submit-and-wait backend is not enough to justify replacing POSIX. Phase 4H should test whether queue depth and batched SQE submission can reduce file IO wait while preserving `readAtAll` / `writeAtAll` equivalent behavior.

## Proposed Interface

- Keep `--file-io-backend posix|io_uring`; default remains `posix`.
- Add `--file-io-queue-depth <N>` only for `io_uring`, default `1`.
- Valid range: `1..256`; invalid values fail option parsing.
- `queue_depth=1` must remain equivalent to Phase 4G submit-and-wait behavior.
- Existing `--file-io-buffer-size`, `--file-io-advice`, `--preallocate`, checksum, and final verify options keep their current meanings.

## Backend Design

- Keep backend file-IO-only; sockets remain epoll.
- Extend `FileIoConfig` with `queueDepth`.
- Extend `FileIoStats` with:
  - `io_uring_submit_count`
  - `io_uring_wait_count`
  - `io_uring_completion_count`
  - `io_uring_partial_completion_count`
  - `io_uring_retry_count`
  - `io_uring_queue_depth_effective`
- Do not introduce virtual dispatch in the hot path; continue using concrete context + switch.
- Batch only contiguous regular-file IO generated inside one `readAtAll` / `writeAtAll` call.
- Preserve exact logical semantics:
  - `readAtAll` returns success only after the requested byte count is read.
  - EOF before the requested count is an error.
  - `writeAtAll` returns success only after the requested byte count is written.
  - partial completions continue until complete or error.
  - `EINTR` / `EAGAIN` retry; other errors become existing `Status`.

## Fair Comparison

Run Phase 4H with both storage bench and GridFTP-like matrix:

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off \
  --file-io-backends posix,io_uring \
  --file-io-advices off \
  --buffer-sizes 262144,1048576 \
  --iterations 3 \
  --output-dir tools/perf/results
```

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix,io_uring \
  --file-io-buffer-sizes 0 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results
```

For queue-depth comparisons, run the same commands with `queue_depth=1,4,16,64` after scripts expose that dimension. Report median, not best run.

## Acceptance Criteria For A Future Implementation

- Default build without liburing remains green.
- Real liburing build remains green.
- `queue_depth=1` matches Phase 4G correctness and semantics.
- `queue_depth>1` passes file IO unit tests, storage bench correctness, STOR/RETR sha256 validation, STOR resume, RETR resume, and RETR corrupt resume.
- No change to manifest, checksum, resume, final verify, or `verified_chunks` defaults.
- A default switch away from POSIX is not allowed in Phase 4H.

## Out Of Scope

- No socket io_uring.
- No raw FTP STOR/RETR.
- No TLS/GSI, MLST/MLSD, Mode E, SPAS/SPOR, third-party transfer, or multi-file directory sync.
- No default switch to `io_uring`, `verified_chunks`, `preallocate=full`, file IO buffer, or file IO advice.
