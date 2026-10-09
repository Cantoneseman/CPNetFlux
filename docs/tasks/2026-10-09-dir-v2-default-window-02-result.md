# DIR-V2-DEFAULT-WINDOW-02 result

- Task version: `R2026-10-09.3 / v1`; authoritative root: Shenzhen `/root/projects/CPNetFlux`.
- Input commit: `13afdb493a2c9bf5f7d5ae6f1ffe61e4aa62336a`.
- Code commit: `8e1a0eaee5ecae6f62033d191cc4be2e22b184ca`.
- GitHub branch `codex/DIR-V2-TRANSFER-ENGINE-01` was pushed and `ls-remote` returned the exact code commit SHA above.

## Change

- The opt-in persistent tree data session now defaults `dataPendingWindow` to 2. `--data-session-reuse tree` remains opt-in; legacy mode and explicit window values are unchanged.
- A parser regression test requires the omitted option to resolve to 2. Existing tests still check explicit window 4 and reject 17.

## Verification

- RED: the new test against input commit plus test-only patch failed as expected: actual default 1, expected 2; CTest exit 8 for the single failing test.
- GREEN: Release CMake configure succeeded with TLS ON and io_uring OFF. The unit test target and server/upload/download clients built. `TreeTransferOptionsTest.*`: 13/13 passed.
- 128 x 1 MiB bidirectional TCP loopback with the window argument omitted: upload 128/128, 134217728 bytes, window 2, queue high-watermark 2, 0.459 s; download 128/128, same bytes and window 2, reported high-watermark 0, 0.590 s. Both independent tree hashes matched `1486055b894594ee4bb5acfe3c529c9efa72bdb40bda2e08e0b6dd1803f59c86`.
- Loopback result JSON SHA-256: `71e4ba99c970314426b8a853b7321f5c2df811f7b69b1e35e150744c6289a594`.
- Committed source archive SHA-256: `3ac4ee80e9835069ca21c90cc9523df9e1df4df279c00d93ae49e1993a0a27ba`.
- Modified header SHA-256: `cc99727243588f8ead8bb1b719c9faa012ebba7308aed0064f74abdd2e4897be`; modified test SHA-256: `8052cde98fc6443b00f5e7d8dae8ea709807b97a4c245cbaa8c6fbcabd4d5ea5`.
- Binary SHA-256: server `53a3027b6a16b2c52aa27901d8c2e0da89d7fe3dfdb2337066db6c34a556dd78`; upload client `bb2e7ad76f65b1649d5ba4f68cb06a086c41ebeb9e08b01879cd18d036e40bf3`; download client `bfd9d641d4658c71fb69a5d208ec7f27a60e4ee3d0f816070867a2316872cf4e`.

## Limits

- This task did not rerun the WAN/GridFTP comparison or full CTest. It changes only the implicit window for the already opt-in persistent mode; it does not claim a new WAN throughput gain.
- Build and test logs plus `loopback.json` are retained under `/tmp/cpnetflux-runs/DIR-V2-DEFAULT-WINDOW-02/` on Shenzhen. The full test suite and GSI path were not needed for this parameter-only change.
