# V3 frame sendmsg batching

- Date: 2026-10-29
- Task: route P0-3, coalesce DATA header and payload writes.
- Input commit: `0fba62b81cdc77d8a959d14c940766dca12e004e`
- Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`
- Workspace: Shenzhen `/root/projects/CPNetFlux`, one writer, no subagents.

## Scope and design

Add `FramedDataSocket::writeFrame(header, headerLength, payload, payloadLength)` for two byte spans. Plain sockets use a two-element stack iovec with sendmsg(MSG_NOSIGNAL), retry EINTR, and advance across vector boundaries after positive partial writes. Errors including EAGAIN remain transport errors; no busy retry or implicit reconnect. Reject invalid nonempty pointers and overflow before sending any bytes; empty spans are supported.

TLS follows the route's explicit boundary: continue the existing header-then-payload SSL write path through writeAll. Never use sendmsg on an encrypted connection or add a plaintext fallback. Do not claim a TLS syscall reduction.

Use the API in the V3 persistent DATA producer's consumer and PersistentDataSession's common frame writer. Removing the latter's temporary concatenation vector is an immediate consequence of using scatter/gather; the DATA CRC-prefixed payload vector belongs to P0-4 and stays unchanged. Preserve frame bytes, checksum behavior, executor startIo/finishIo accounting, and frame-call metrics. Clarify the timing scope string; the existing socket_write_frame_calls field counts logical DATA frames, not OS syscalls.

The existing common metadata writer currently concatenates its spans for one TLS write. Preserve that TLS branch to avoid creating an extra SSL write there; only its plain-socket branch switches to scatter/gather. The new writeFrame API keeps the DATA sender's existing two SSL writes when TLS is enabled.

## Affected files

- `include/cpnetflux/core/io/framed_data_socket.h`
- `src/core/io/framed_data_socket.cpp`
- `src/core/io/persistent_data_session.cpp`
- `src/core/io/persistent_tree_transfer.cpp`
- `tests/unit/framed_data_socket_test.cpp` (new)
- `CMakeLists.txt` (Linux socket test target with test-only linker syscall wrappers)
- `src/protocol/control/control_server.cpp` (static transfer cleanup before terminal reply)
- `tools/test/run_cpnetflux_tree_data_reuse_smoke.py` (retain strict checkpoint assertion, improve failure evidence)
- `docs/ROADMAP.md` (P0 progress and next-task design correction)
- This task and its result record.

## Non-goals

No frame format/version/size change, CRC strength or prefix change, TLS staging buffer, TLS library change, scheduling/pipeline default change, system network configuration, service switch, new development checkout, or cross-server transfer/performance experiment. The research boundary in docs/RESEARCH_BASELINE.md still applies.

## Acceptance

- Tests added before implementation demonstrate that writeFrame is missing, then pass after implementation. Record API compile-red separately from assertion-red.
- Real socketpair bytes equal two writeAll operations; test-only linker wrappers count 2 send calls before and 1 sendmsg after for an uncontended frame.
- Exercise partial writes inside the header, exactly at its boundary, and inside the payload, plus EINTR, zero progress, EAGAIN, invalid input, and peer closure with default SIGPIPE behavior.
- Real backpressure test uses small socket buffers and a bounded reader to verify full output across multiple sendmsg calls.
- Full Release CMake build/CTest includes V3 CRC, compression, corrupt frame, recovery, publication, and existing TLS transfer smoke coverage. Supply process-only token-auth test configuration without logging its value.
- Preserve unrelated dirty files. Commit named files, push the existing verified GitHub branch, and read back its SHA.
- Archive the committed source, build it on Shanghai in a new isolated candidate directory, and compare server/upload-client hashes with Shenzhen. Do not start a Shenzhen-Shanghai data transfer.
- Physical-pair throughput/CPU acceptance remains deferred by the user. Syscall-count evidence is not an end-to-end performance result.

## Status

Implementation, full software acceptance (466 passes and one capability skip), implementation GitHub backup, and same-commit Shanghai build/hash comparison are complete. See `2026-10-29-v3-frame-sendmsg-01-result.md` for revision, commands, evidence, limitations, and next work. Physical-pair performance acceptance remains deferred.

## Regression discovered during acceptance

The first full CTest run had 465 passes, one intentional io_uring skip, and one existing interrupted static-upload checkpoint assertion failure. Static receive transactions remain owned by integrity-check closures until the local context is destroyed. runPersistentDirectory sent the terminal 550 before that destruction, so a client could observe a checkpoint before its failed state was saved. A separate 12-case immediate/20-ms diagnostic did not reproduce the race; do not claim deterministic reproduction or attribute its origin to sendmsg. The fix releases the exclusively owned static context before the terminal reply. Dynamic batch contexts retain their shared lifetime for sibling channels and recovery. The existing strict immediate assertion is preserved, with observed checkpoint text added to failures. The focused 11-case socket/interrupted-transfer regression passed after the fix. This is an acceptance fix, with no protocol change.
