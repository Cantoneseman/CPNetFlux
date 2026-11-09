# S1 Summary Path22 Development Scope

Date:2026-11-09. Status: opt-in implementation and bounded acceptance complete;
see [result](2026-11-09-v3-summary-path-22-result.md). Shared default not promoted.
Input experiment/source `522fb68d7f65e08fc1c3359331d23ea702aee2c4`;
start implementation from the pushed Baseline21 result commit and record its SHA.
R770-1 sole serial writer; no agents/worktrees. R770-2 fixed-commit peer only.

Execution input: `4f62a137faa0b6d48c3bcc7529204c6ebbf7ef0f`.
The existing filesystem reader is private inside tree_transfer_client.cpp. Move
it into the existing core/tree/tree_hash.{h,cpp} module so direct behavior tests
can exercise symlink/FIFO rejection without a transfer. Keep the original reader
as the control; cache validated names only in the candidate. Exact additional
files: `tests/unit/tree_hash_test.cpp`,
`tools/test/run_cpnetflux_summary_path_smoke.py`,
`tools/perf/run_summary_path_ab.py`, `tools/test/run_summary_path_ab_test.py`.
Use task-local `CPNETFLUX_SUMMARY_PATH=original|cached`; reject invalid values at
upload/download entry, before connecting. Freeze mode per run and record requested/
effective mode in JSON; reuse path reports not_used. Default stays original in
this round, including after an opt-in candidate passes. Shared-default promotion
requires the separate matched large-file regression gate, not assumed here.
Tests: new smoke first against existing binary (expected missing-mode assertion),
tree-hash unit cases and related summary/tree/V3 resume/conflict gates; then both
nodes fixed source/build receipts before 40 formal samples (five pairs for each
of three filesystem-read cases plus the16KiB x10K reuse negative control).
No parallel writer/agent, production changes, or unrelated October14 docs edits.
Closure also updates the S0 result with a historical timing caveat and adds
Default Gate23 as the explicit unexecuted follow-up before shared-default promotion.
The shared traversal also propagates a terminal iterator error, checked with an
unprivileged permission-denied directory test. Invalid mode fails before control
connection; existing CLI parsing may create an empty download directory first,
but no payload/checkpoint is created. Initial negative-test fixture errors
(duplicate port and invalid remote absolute paths) were corrected and retained
in test evidence; they were not transfer-runtime failures.

## Execution Commands

```sh
cmake --build build -j 4 --target cpnetflux-gridftp-server cpnetflux-tree-download-client cpnetflux-tree-upload-client cpnetflux_unit_tests
ctest --test-dir build --output-on-failure -R '^(TreeHash.*|cpnetflux_(small_file_baseline_driver_test|summary_path_ab_test|summary_path_smoke|tree_pending_window_smoke|tree_receiver_policy_smoke|tree_manifest_coalesce_smoke|v3_unified_executor_smoke|v3_interrupted_resume_smoke|v3_corrupt_partial_checkpoint_smoke|v3_target_conflict_idempotency_smoke|tree_edge_cases_smoke))$'
CPNETFLUX_SUMMARY_PATH=cached ctest --test-dir build --output-on-failure -R '^cpnetflux_summary_crc_auto_smoke$'
python3 tools/perf/run_summary_path_ab.py --commit FIXED_SHA \
  --evidence /home/Su/experiments/CPNetFlux/2026-11-09-v3-summary-path-22/formal \
  --payload-root /dev/shm/cpnetflux-s0-summary22
```

Record the pushed FIXED_SHA before transfer, then compare both build receipts.
The16KiB x10K reuse negative control requires abs median change<=5%, both CV<=5%
and descriptive p95 no worse. Its effective mode must be not_used. All40 samples
must pass independent SHA/member manifest/tree terminal checks before cleanup.

## Mechanism, Scope And Non-Goals

### Measurement Revision2

First40 samples completed on c8e1428, integrity/resources/cleanup passed, but
overall acceptance failed:64KiB x1K control CV5.217% and negative-control
descriptive p95 worsened. Preserve all in `formal/`, do not select samples or
relax gates. Review found `run_guarded` synchronously runs ss then timestamps
the loop end, including collector work after the client has already exited.
The two64KiB control samples at25 collector iterations show19.99/21.13ms beyond
client elapsed+hash, versus3.45/5.40/4.18ms at24 iterations. Structural code
confirms the spawn-to-exit contract was not enforced.

Add a failing delayed-observer regression to the existing
`tools/test/run_small_file_baseline_test.py`; use an independent wait thread to
timestamp child exit and separately record collector duration after exit.
No transfer runtime changes. Fix and commit/push, sync both nodes, verify hashes,
then repeat the entire40-sample matrix once in `formal-v2/`, owned root
`/dev/shm/cpnetflux-s0-summary22-v2`. This replaces the timing acceptance set,
not the retained failure evidence. No further reruns to chase CV/p95. Reuse-path
negative-control variability remains subject to its original gate.

[Baseline21](2026-11-09-v3-small-file-baseline-21-result.md) measured1.57989s
summary hashing out of2.59057s T_ready for4KiB x10K. The filesystem-read hash
sort comparator repeatedly calls `filesystem::relative`. Cache the already
validated relative path and size-independent sorting key once per payload entry,
then sort pure strings and reuse that same key in the canonical CRC stream.
Preserve exact byte ordering, artifact exclusion, symlink/nonregular rejection,
filesystem error propagation, file reads, CRC backend and output format.
Do not replace canonical relative-path semantics with unchecked substrings.
Single-writer/no-external-tree-mutation experiment contract remains unchanged;
do not claim new concurrent mutation guarantees for either implementation.

Affected files: this task/result, `src/core/io/tree_transfer_client.cpp`, relevant
tree-hash unit or script tests, `tools/perf/run_small_file_baseline.py` for a
bounded same-binary A/B selector, CMake test registration if needed, ROADMAP and
route status. Confirm exact existing test/helper ownership before edits.
No protocol/CRC-algorithm/frame/transport/lease/checkpoint/publish changes,
no unconditional verified-chunk reuse, no pending-window/default changes,
no thread/queue/THP/network/RDMA tuning, no member aggregation in this candidate.

## Implementation And Correctness

1. Add failing behavior/performance-contract coverage for flat and nested trees,
   ambiguous lexical prefixes, empty files/tree, non-ASCII names, artifacts and
   unexpected symlink/nonregular entries. Compare canonical hash to a separately
   constructed reference; don't encode platform traversal order as the oracle.
2. Implement one cached-key path. Retain the original path as an explicit bounded
   experiment control selector so both arms use the same committed binary.
   Validate selector values; record requested/effective mode in evidence. Default
   remains original until full-task acceptance; selector must not affect reusable
   verified-chunks hashing, verification policy or transport.
3. Prove cached relative resolution occurs once/entry, not per comparison, using
   structural review and a focused count/trace if needed. One bounded trace is
   diagnostic only, excluded from throughput samples. Check memory O(file count).
4. Build and run related hash/summary/V3 tree/resume/conflict/corrupt-checkpoint
   tests. Fix failures before source commit/push/sync/build SHA comparison.

## Bounded Performance Acceptance

Keep Baseline21 configuration, source/output<=1GiB, fresh outputs, CRC32C and
complete T_ready through summary exit. Receiver detail timers/profiles off.
Freeze pending window2 on both arms, while recording why auto currently selects2.
Five interleaved pairs each at4KiB x1K,4KiB x10K and64KiB x1K, separate from any
pilot. Negative control at16KiB x10K (reuse path), plus large-file regression
check with the existing accepted scope if promoting a shared default. Source
and output resource/ownership limits and post-run cleanup are unchanged.

Candidate gate: median full T_ready reduction>=20%, arm CV<=5%, paired direction
and hash duration consistent with mechanism, no correctness/recovery regression.
Three-sample p95 from S0 is not a tail gate; record each sample and descriptive
p95 from new runs without claiming statistical tail guarantees. Do not accept
p95 worsening; unstable/inconclusive cases remain open. Large-file regression
<=5% under a matched explicit check; local gains don't change the100G shortfall.
If budget is insufficient after tracing or one candidate fails, retain evidence
and stop this mechanism without a parameter sweep. Do not combine auto-policy or
CRC-reuse changes into the A/B result.

## Subsequent Order

First reassess summary versus transfer/manifest/ACK fixed work. Fresh-download
auto's missing remote statistics is a separate policy/correctness design, not a
performance default promotion: S0 fixed8 gains were mixed and below3%.
Then propose fixed member microbatch only with a new conservative budget;
preserve per-member identity/CRC, failure isolation, recovery and atomic publish.
Adaptive batching follows an independently accepted fixed mechanism. RDMA stays
paused; large-file frame/CRC and zero-copy directions retain their design gates.

Evidence goes under `/home/Su/experiments/CPNetFlux/2026-11-09-v3-summary-path-22`.
Before transfer: fixed source SHA pushed/read back, both nodes same configuration,
version and three transfer binary SHA-256 values. Record exact commands/tests,
all failures/samples, input/output SHAs, resource/counter/cleanup outcomes and
the next handoff. No payload, credentials or giant logs in Git.
