# DIR-V2-MULTICHANNEL-01: bounded persistent channels

- Task ID/version: `DIR-V2-MULTICHANNEL-01 v1`.
- Input commit: `414646cfc5812867ca6382ba8773dde6ef9c81f6`.
- Authoritative root: Shenzhen `/root/projects/CPNetFlux`, branch `codex/DIR-V2-TRANSFER-ENGINE-01`.
- Initial root status: 262 unrelated dirty entries; staged index empty. All implementation/test paths listed below were clean before this task. Preserve unrelated changes.
- Goal: extend opt-in persistent tree transfer to multiple independent, bounded control/data channels. Reuse `--file-parallelism N` as channel count for this mode, with a hard maximum of 8; each channel owns one TCP data session and receives a stable file-ID shard.
- Compatibility: keep one-channel V2 and legacy V1 behavior. Negotiate channel capability before starting payload; unsupported servers/configurations fall back before payload. Never replay after payload begins.
- Correctness: retain per-channel pending window (1..16), file ID/generation/path/size checks, destination no-replace commit, manifest updates, first-error cancellation, and bounded per-channel frame buffers. A channel failure shuts down sibling control/data sockets and leaves resumable checkpoints.
- Eligible scope: fresh tree transfer, checksum none, compression off, scheduler off, one stream per channel, data TLS off, max-files unset. Control TLS/auth remain governed by existing options.
- Non-goals: change defaults, enable data TLS/resume/checksum/compression/scheduler, implement dynamic work stealing or big-file range striping, alter wire framing for file payloads, run WAN/GridFTP experiments, touch live user services.
- Files: `src/core/io/tree_transfer_client.cpp`, `src/core/io/persistent_tree_transfer.{h,cpp}`, `src/protocol/control/control_server.cpp`, `src/config/tree_transfer_options.cpp`, `tests/unit/tree_transfer_options_test.cpp`, `tests/unit/persistent_data_session_test.cpp`, `tools/test/run_cpnetflux_tree_data_reuse_smoke.py`, and this task/result record. Add no BOARD edits because the shared board is already dirty.
- Acceptance: parser tests cover 1/4/8 and reject >8 in persistent mode; loopback upload/download with four channels reports exactly four data connections, transfers 128 x 1 MiB with matching independent tree SHA-256 and committed manifests; wrong-shard IDs, collision, disconnect/timeout and legacy fallback remain covered; focused tests and full CTest report skips/failures explicitly.
- Build from an exact source archive of the resulting commit plus only this task's patch; do not build from the dirty root. Commit only reviewed files and push to the verified GitHub branch; read back exact remote SHA.
- No cross-domain or GridFTP run in this task. A separate experiment task must refresh GSI proxy and verify both endpoint budgets before running.
