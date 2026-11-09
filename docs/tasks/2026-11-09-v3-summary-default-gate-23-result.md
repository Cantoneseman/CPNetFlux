# S1B Summary Default Gate23 Result

Date:2026-11-09. Decision: HOLD, not a throughput rejection of cached summary.
Driver implemented; large-file/default-promotion acceptance NOT completed.
No complete transfer/performance samples. Default remains original, cached opt-in.

## Source And Contract

- Input76cd3dd466dc055594b9fd12950d87be7221fb35 (S1 accepted opt-in result).
- Driver0e25ab0c73f44e9cdb4f6a2938d9b09175718d2e; diagnostic correction
  067fd298a6b987302ea0287cd7356b45c25ccd32. Both pushed/read back and synced.
- Same RelWithDebInfo configuration/version0.1.0 and three binary SHA-256 values
  verified on R770-1/R770-2 before launching the driver (matching-builds.json).
- Intended regression: five interleaved original/cached pairs, same binary,
  8GiB resident tmpfs source/new target, four TCP connections/eight1GiB ranges,
  queued sender/inline experimental mmap receiver, CRC32C/verified_chunks,
  checkpoint16, TLS/compression off, commit_sync=none. Explicit .2/.1 local bind.
- Gate remains median wall regression<=5%, each arm CV<=5%, independent full SHA,
  exact8192 chunk coverage, V3 terminals and unchanged recovery/resource gates.
  Independent process-exit timestamp defines T_ready; no CV retries/deletions.

## Implementation

New run_summary_default_gate.py reuses S0 ownership/receipt/cleanup and corrected
exit clock. It adds bounded8GiB generation and mincore residency, exact committed
chunk coverage, mmap eight-range/window checks, paired reuse analysis and saved
preparation resource diagnostics. The common runner has an explicit mapped-download
option; existing S0/S1 defaults are unchanged. No runtime C++ was changed.

The generated source is one random8MiB block repeated1024, fully materialized,
compression off. It is not an entropy/compression benchmark. Per-sample outputs
are independently hashed and cleaned; failed preparation partials are cleaned.
Tests reject missing/extra pairs, unstable arms, changed hashes or summary source,
invalid chunk coverage/backend/window bounds and seed swap-out, and verify that
mincore observation leaves file contents intact.

## Attempts And Resource Failure

| Attempt | Outcome |
|---|---|
| 0e25ab0, top evidence directory | Seed preparation resource assertion; old combined assertion did not identify memory versus swap. No client/server transfer started. Partial source and both marked roots removed. |
| 067fd29, formal-v2 | Seed guard identifies host swap-out delta1 page; no performance samples, no client/server transfer started. Partial source and both roots removed. |

Corrected diagnostic: source materialized5,217,714,176 bytes (4.859375GiB),
minimum MemAvailable25,059,221,504 bytes (23.33822GiB), owned VmSwap0,
pswpout2207567->2207568 (one page;4KiB on this host). This is not evidence of
low available memory or of payload swapping. The host-wide counter cannot
identify which process/page was swapped. It nevertheless violates the declared
zero-increment preparation gate; no further preparation retry or relaxed gate.

R2 has about7.68GiB of pre-existing used swap. Existing production services have
their original PID/start identities and were not stopped or modified. Their
historical swapped memory is not the experiment's owned VmSwap metric. No sysctl,
swap configuration, cache-dropping, THP/NUMA/mount/network changes were made.

## Verification And Evidence

- R1 and R2 each passed20 relevant CTests:10 TreeHash cases; driver/summary
  selectors; normal mmap config/fault/unobserved; V3 interrupted resume,
  corrupt checkpoint and target-conflict/idempotency. New driver originally7
  checks; final seed-abort coverage raises this to8 (final checks logged).
- Initial broader group18/19: unchanged mapped_lifecycle diagnostic failed in
  its collapse/pattern subcase with MADV_COLLAPSE errno12. This diagnostic is
  not used by S1B ordinary mmap; retain the failure, do not call it passed.
  Related normal-mmap tests passed. No collapse policy was changed.
- Historical scope17 evidence was used only to validate the new eight-range and
 8192-chunk parsers, never as this round's performance denominator/result.
- Evidence: /home/Su/experiments/CPNetFlux/2026-11-09-v3-summary-default-gate-23
  on R770-1; peer-capture preserves R2 files, formal-v2 is the corrected attempt.
  matching-builds, matrix logs, seed-resource, CTest logs and final snapshots
  identify commits, binaries, failure, route/counters and exact root absence.
- Both /dev/shm/cpnetflux-s0-summary-default-23 and the -v2 variant are absent
  on both nodes. No large payload remains; only bounded evidence is retained.

## Progress And Next Work

S1's measured small-file gains remain limited to explicit cached filesystem-read
summary. S1B infrastructure is ready, but neither the matched large-file regression
nor default promotion is accepted. This round adds no new Gbps claim; historical
27.3Gbps and30-50/70-85/100Gbps targets retain their prior scope/open status.
The scope17 rx_missed_errors+94 incident remains unexplained.

Hold the default promotion until a stable preparation environment can satisfy the
unchanged resource gate. Independently collect one bounded cached4KiB x10K
fixed-work attribution before designing member batches; see
[Fixed Work24](2026-11-09-v3-small-file-fixed-work-24.md). No adaptive batching,
thread/queue expansion or RDMA restart is justified by these results.
