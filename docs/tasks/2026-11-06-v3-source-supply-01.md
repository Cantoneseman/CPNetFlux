# V3 Source Supply 01

Date: 2026-11-06 Asia/Shanghai. Input 5732a7cf587333a93d910812805a5e42893a6889.
Single writer R770-1 /home/Su/projects/CPNetFlux; existing branch, no agents/worktrees.

## Scope

Execute the next step after manifest serialization: establish the source storage/read
service rate of the fixed 64 GiB file on R770-2, then compare read plus CRC32C supply
with the existing native sender observations. Preserve the seed and production services.
Extend the existing storage benchmark rather than implement another transfer engine.
Correct read-mode cleanup so a read-only workload never removes its caller-owned input,
including failure. Add explicit optional CRC32C/auto read processing and report its
resolved backend, digest, timed checksum work, total bytes actually read, CPU and
physical I/O through an external runner. CRC is a diagnostic, not transfer integrity.

## Non-goals

No transport, wire, lease, read/write runtime defaults, checkpoint cadence, schema,
final verification, buffer/pipeline expansion or system cache/network settings change.
No production readiness or 100G claim. CQ/RNR lifecycle attribution remains separate.
Do not change CPSS(DCC), old untracked October docs, history or payloads. No native
transfer from unmatched/dirty versions. Keep source/measurement evidence separate.

## Affected Files

src/apps/cpnetflux_storage_bench.cpp; focused Python storage read smoke and CMake entry;
this task/result, docs/ROADMAP.md, and a scoped source-supply runner under tools/perf.
External evidence: /home/Su/experiments/CPNetFlux/2026-11-06-v3-source-supply-01.

## Acceptance And Decision

1. Before fix, demonstrate successful and truncated read-mode input deletion using only
   temporary test fixtures. Add digest oracle for non-aligned payload/buffer sizes and
   invalid option/mode cases. Verify read mode preserves inode, content and length.
2. Build and run focused storage/checksum/benchmark gates on R770-1, full suite if shared
   behavior changes, scoped commit/push and exact GitHub SHA readback. Sync fixed source
   to R770-2, build and test. Storage read and read+CRC modes must not write the seed.
3. Fixed seed size 68719476736, SHA-256
   054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1.
   Read-only 64 KiB / 1 MiB / 8 MiB buffers x three repetitions, rotate order by round,
   POSIX, advice off, queue/batch one, no cache dropping, no concurrent native timing.
   Then three CRC32C/auto reads at the production frame-sized buffer (65532 bytes) to
   compare source processing; report processing difference and cache/order limits.
4. Capture source commit, benchmark SHA/options, process read_bytes and CPU, elapsed,
   logical read calls/bytes, block-device counters, memory and production PID state.
   Require file identity/length/mtime/ctime unchanged and matching independent SHA
   outside timed reads. Do not call buffered reads cold or storage device maxima.
5. Choose prefetch/read-size implementation only if evidence shows useful headroom.
   If all reads are near observed ~4 Gbps and source wait dominates actual transfer,
   retain measurement/safety improvements and mark unproven buffer optimization deferred.
   Compare existing actual sender read/checksum/frame/queue intervals, never sum overlaps.
6. Finish with result/route/next-input records, scoped commits/push/readback, peer docs
   sync, no temporary process, original production PIDs 126766/164834 and seed intact.

Tool audit: exec_command/write_stdin/apply_patch and dedicated chat messaging exposed.
No delegation requested/called. R770-2 codex CLI check returned not found. Only SSH
shell execution is used, per user request; no desktop automation.

## Outcome

Implementation 5b8be5af0c034451c117f372e346c049228299cc committed/pushed/read back;
both builds and related 51/51 CTest gates passed. Seven hashes/config/version match,
and six transfer-related hashes are identical to previous native measured source.
All 12 source-read cases passed, source identity and independent SHA unchanged.
64 KiB/1 MiB/8 MiB pure read medians 4.04734/3.95836/3.96494 Gbps; 65532-byte
read plus hardware CRC median 4.04570 Gbps, checksum/combine subset 2.68731 seconds.
No useful larger-read-block gain; runtime defaults/pipeline remain unchanged. This
source path is near the previous native task rate, but is not a device hardware maximum.
Next: read-only io_uring queue/lifetime controls and separately verified cached/faster
source native controls, then evidence-based prefetch choice. CQ/RNR attribution stays
separate. All evidence and next-step detail: [result](2026-11-06-v3-source-supply-01-result.md).
