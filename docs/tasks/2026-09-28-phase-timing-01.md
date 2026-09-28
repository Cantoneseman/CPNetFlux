# PHASE-TIMING-01

Date: 2026-09-28
Role: 03 implementation
Input baseline: ce5c5a9f2ceea08bd0fc0097867133cef27552cd
Branch: codex/DIR-PERF-TIMING-01
Worktree: /tmp/cpnetflux-runs/DIR-PERF-TIMING-01/implementation

Goal: add default-off directory phase timing for A/B/C only. Do not optimize yet and do not touch the live Shenzhen root or run network experiments.

A is metadata/control work, B is manifest update/lock/save work, C is file transfer wall and post-transfer control completion wait. Use steady_clock and thread-safe counters. Existing JSON output must remain unchanged when the flag is off. When enabled, emit fixed parseable fields, units, schema marker, counts and accumulated seconds. Do not use cross-host clock subtraction. Do not change protocol, scheduler, compression, checksum, resume, IO backend, defaults or manifest format.

Add a clear default-off CLI option following existing conventions, such as --phase-timing, and wire it to the tree JSON summary/diagnostic output. Instrument actual call boundaries, not an outer coarse wall. Distinguish missing/not-applicable from real zero where the existing output permits.

Add minimal unit/parser coverage for default off and summary fields. Run safe relevant checks; if the live experiment makes build/test resource use unsafe, stop and record not run. Commit and push this branch to the verified GitHub remote.

Write docs/tasks/2026-09-28-phase-timing-01-result.md with changed files, exact semantics, validation, commit/push SHA, and a short manual command template.