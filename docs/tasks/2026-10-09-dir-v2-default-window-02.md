# DIR-V2-DEFAULT-WINDOW-02

- Route/task version: `R2026-10-09.3 / v1`.
- Input commit: `13afdb493a2c9bf5f7d5ae6f1ffe61e4aa62336a` on `codex/DIR-V2-TRANSFER-ENGINE-01`.
- Goal: make the opt-in persistent tree data session use a bounded pending window of two by default, based on the prior 1/2/4 short sweep and the latest three-repeat 128 x 1 MiB run at window 2. Keep legacy mode and the feature opt-in unchanged.
- Non-goals: no protocol change, no control-pipeline change, no multi-worker scheduling, no checksum/resume/TLS change, no default enablement of persistent data reuse, and no new GridFTP experiment.
- Allowed files: `include/cpnetflux/config/tree_transfer_options.h`, `tests/unit/tree_transfer_options_test.cpp`, this task and its result. Preserve every unrelated dirty file.
- Acceptance: a parser test proves the omitted option defaults to 2 and explicit values still override; focused CTest passes; a 128 x 1 MiB bidirectional loopback run omitting the option reports effective window 2, bounded high-watermark, complete file counts, and unchanged canonical tree SHA-256.
- Verification: build from a clean archive of the input commit plus only this task's explicit patch; record source and binary hashes. Commit only the reviewed files, push to the existing CPNetFlux GitHub branch, and read back the exact remote commit.
- Limit: the latest WAN case is already near the 100 Mbps link ceiling, so this parameter change is a conservative opt-in default; loopback correctness is not proof of additional WAN throughput.

## Result

Implementation and tests verified; final commit and GitHub receipt will be recorded in the result file.
