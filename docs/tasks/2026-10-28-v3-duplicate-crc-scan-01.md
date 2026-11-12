# V3 duplicate CRC scan removal

- Date: 2026-10-28
- Task: P0-2, remove a second payload scan used to maintain the range FILE_END CRC.
- Input commit: 72ce9d62cd374c72baecf3deaafedbb5dc316e31 on codex/V3-GRIDFTP-OPTIMIZED-RERUN-01.

## Scope

Keep the existing per-frame CRC on the wire and continue rejecting a bad frame before storage writes or resume checkpoint updates. Reuse each verified frame CRC to build the range-level FILE_END CRC with CRC32C combine, replacing the second scan of the same logical payload on both sender and receiver. Preserve compression, sparse resume, range ordering, corruption injection, and checksum-none behavior.

## Design

- Add a checksum accumulator operation that combines a finalized checksum and its byte length into the current accumulator.
- Keep sender frame CRC calculation as the single payload scan; combine the result into the FILE_END accumulator.
- On receive, calculate and compare the frame CRC first. Only the matching CRC is passed to PersistentDataSession to update its FILE_END accumulator.
- Source inspection also found a separate checkpoint chunk scan in both the ranged coordinator and whole-file transaction. Reuse the verified frame CRC there too when the frame fits a chunk; retain per-piece scanning for a whole-file frame crossing chunk boundaries. Independent stored-file verification is unchanged.
- The final checksum remains CRC32C over the concatenation of transmitted logical DATA payloads, exactly as before. No wire field or checksum coverage changes.

## Files

- `include/cpnetflux/checksum/checksum.h`
- `src/checksum/checksum.cpp`
- `include/cpnetflux/core/io/persistent_data_session.h`
- `src/core/io/persistent_data_session.cpp`
- `src/core/io/persistent_tree_transfer.cpp`
- `tests/unit/checksum_test.cpp`
- `tests/unit/persistent_data_session_test.cpp`
- `src/protocol/control/control_server.cpp`
- `src/core/io/tree_transfer_client.cpp`
- `tools/test/run_cpnetflux_reconnect_smoke.py`
- This task record and its result.

## Non-goals

No frame/wire change, checksum weakening, protocol negotiation, P1 frame-size change, sendmsg change, compression change, production default change, server configuration change, or cross-server data transfer.

## Acceptance

- Red/green tests show combined accumulator equals one-shot CRC for multi-frame and uneven boundaries.
- Session FILE_END digest matches when supplied verified frame CRCs; mismatched, missing, or out-of-order frame data cannot advance verified state.
- Existing corrupt-frame/checkpoint, compression, sparse resume and tree transfer tests pass.
- Run relevant Release CTest and file-checksum smoke.
- Build the committed source on both servers and compare server/upload-client SHA-256. Do not start a Shenzhen-Shanghai transfer in this task.
- Performance acceptance is deferred at the user's request. Unit tests and local correctness smoke are allowed; do not claim R730/R750 performance or end-to-end throughput.

## Status

CRC reuse is implemented. The first full CTest found an intermittent corrupt-output recovery diagnostic failure. Source tracing established that DRAIN loses the original terminal error after directory release, and the client mislabels its 550 response as unsupported drain. Before changing those paths, extend the existing corrupt-output smoke to query DRAIN after failure and require the original integrity diagnosis. Retain the first failure log; do not weaken existing assertions. Then preserve the first non-transport terminal Status across directory release and propagate its 550 reply to the client. This does not change frame/checkpoint formats or recovery policy.

Final software acceptance on 2026-10-29: 456 passing test cases and one io_uring capability skip across the final full suite and two corrected-environment token-auth reruns. The strengthened DRAIN assertion also fails on the prior P0-1 binary and passes on the fixed binary, establishing the pre-existing diagnostic gap. Implementation commit, same-commit two-host builds, binary hashes, and GitHub readback are recorded in `2026-10-29-v3-duplicate-crc-scan-01-result.md`. Physical-pair performance remains deferred.
