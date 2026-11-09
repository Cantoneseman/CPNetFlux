# S1 Summary Path22 Development Scope

Date:2026-11-09. Status: approved-route follow-up, not implemented or benchmarked.
Input experiment/source `522fb68d7f65e08fc1c3359331d23ea702aee2c4`;
start implementation from the pushed Baseline21 result commit and record its SHA.
R770-1 sole serial writer; no agents/worktrees. R770-2 fixed-commit peer only.

## Mechanism, Scope And Non-Goals

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
