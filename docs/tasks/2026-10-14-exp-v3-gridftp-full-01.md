# EXP-V3-GRIDFTP-FULL-01

## Purpose

Compare the current committed CPNetFlux V3 transfer profile against GridFTP
using GSI on the R730/R750 100GbE direct Ethernet link. This answers the
request for a full V3 feature profile; it is not a release-readiness claim.

## Scope

- Input source: clean origin/main commit 80c576b10254937e7cac6b46e63befb2282f95d6.
- Worktree: /tmp/cpnetflux-runs/EXP-V3-GRIDFTP-FULL-01/src on
  codex/EXP-V3-GRIDFTP-FULL-01.
- Compare one 128 MiB dense tree, one 256 MiB mixed tree, and one 256 MiB
  single large file in both directions, with three repetitions per system.
- Required CPNetFlux profile must combine token authentication and control TLS,
  data TLS, CRC32C, dynamic file/range scheduling, tree data-session reuse,
  automatic compression, control-worker reuse, and the global adaptive
  scheduler. Do not drop a requested capability to make a case run. The
  observed client summary and server counters must confirm each feature.
- If this combination is rejected by the committed build, stop before any
  payload transfer. Record the incompatibility and treat implementation and
  regression coverage for the unified V3 path as a prerequisite to this
  comparison, rather than substituting a partial profile.
- Use GridFTP with a refreshed, valid GSI proxy, protected data channel, and
  fixed parallelism. Record exact GSI/data-protection and stream settings.
- Hard cap total payload traffic at 10 GiB across both systems, directions,
  repetitions, preflight, and any retries. Stop before the cap if failures or
  reruns would exceed it.

## Non-goals

- No source or protocol changes, server reconfiguration, service restart, or
  changes to either host's credentials.
- No RDMA-verbs performance claim. This experiment uses IP/TCP over the
  ConnectX Ethernet link, regardless of the switch being RDMA-capable.
- No long soak, extra parameter sweep, cross-domain run, or claim that GridFTP
  can reproduce CPNetFlux-specific queue/range semantics exactly.
- No use of uncommitted Shenzhen root files; no repository payloads or secrets.

## Acceptance

- Clean worktree builds from the recorded input commit; CTest result and skips
  are recorded before network traffic begins.
- Authentication and protected-data preflights pass for both systems before
  timing. GSI proxy lifetime covers the run.
- The full profile is accepted and observed on every CPNetFlux test case. If
  not, the benchmark is blocked and has no CPNetFlux/GridFTP performance result.
- Each planned case records the requested and observed feature profile,
  direction, repeat, bytes, elapsed time, logical goodput, phase data where
  available, and source/destination integrity. Hash/manifest failure invalidates
  that measurement.
- At least three valid measurements exist for each system/workload/direction;
  otherwise report the comparison as incomplete. Aggregate traffic stays below
  10 GiB.
- Result includes medians/spread, exact limits and feature mismatches, the
  Ethernet/TCP path, source SHA, test commands, evidence paths, and next steps.

## Allowed paths and rollback

- Versioned files: only this task and its result in docs/tasks under this
  worktree; add a harness only if the existing harness cannot express the
  accepted profile, and list that file here before editing it.
- Build and logs: /tmp/cpnetflux-runs/EXP-V3-GRIDFTP-FULL-01/.
- Host datasets and destinations: task-specific directories under
  /tmp/cpnetflux-exp-v3-gridftp-full-01/ on R730 and R750.
- No source changes are planned. Rollback is a revert of this task-record-only
  commit; retain external evidence for audit and do not remove existing services
  or credentials.

## Planned validation

1. Configure/build Release with TLS enabled and run full CTest on the clean
   worktree.
2. Confirm that all required V3 capabilities can be selected together; if not,
   run only a zero-payload rejection check and stop.
3. After the full-profile gate passes, run protected-data/auth preflights and
   the fixed 3-workload x 2-direction x 3-repeat comparison, checking
   hashes/manifests after each case and
   accounting every payload byte against the hard cap.

## Status

Build and CTest passed. The full-profile compatibility check rejected the
combined configuration before network I/O; no comparison payload was
transferred. A committed unified V3 path is required before this comparison
