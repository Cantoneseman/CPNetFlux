# V3 Mapped Path Attribution 09

Date: 2026-11-08.
Input: 4c7d813c078978c37bc4a0e3ee368922bb30216e.
Single writer: R770-1 /home/Su/projects/CPNetFlux, existing branch.

## Scope

Attribute the remaining approximately 29 Gbps full-task mapped download cost
before selecting one optimization hypothesis. Start with existing sender,
receiver, payload, Entry/checkpoint and thread CPU observations. Compare lean
and detailed observation on the same fixed source/configuration, and report
observer overhead independently from production throughput. Check locally
available sampling tools without changing kernel policy or installing packages.
If evidence establishes a dominant avoidable cost, implement at most one narrow
candidate, with relevant behavioral tests and a separate full-task acceptance.
If attribution remains insufficient, retain the evidence and name the missing
measurement; do not invent a performance fix.

## Non-goals

No additional lanes/workers/queues, automatic mmap/default changes, RDMA, checksum
removal, weaker integrity/durability policy, kernel/network/NUMA changes, production
service restarts, small-file scope, 64 GiB payload, subagents or worktrees.
Do not alter scope 08 receipts or classify its local 55 Gbps diagnostic as a full
transfer. SIGBUS remains an isolated experimental-task failure.

## Files and Ownership

Initial tracked change: this task. Final: this task, a result record and
docs/ROADMAP.md. Any measured hotspot implementation must first be recorded here
with its exact source/test files and behavioral contract before those edits.
Experiment drivers/analyzers/raw receipts live under
/home/Su/experiments/CPNetFlux/2026-11-08-v3-mapped-profile-09, with script hashes.
R2 receives only fixed commits, builds and tests; no independent source edits.
Preserve the two unrelated 2026-10-14 untracked task files on R1 and CPSS(DCC).

## Acceptance and Order

1. Verify input tree, tools, existing observation contracts and hardware sampling
   permission. Read current RESEARCH_BASELINE boundaries and scope 08 handoff.
2. Fix/commit/push/read back each completed source stage, sync R2, build both
   configurations and check version plus all eight binary SHA-256 before transfer.
   No dirty or mixed-version inter-host run, including diagnostics.
3. Three interleaved lean/detailed mapped 8 GiB task pairs initially; exact same
   source and four inline TCP lanes, read0/write0, POSIX direct, no compression,
   verified_chunks, CRC32C, checkpoint 16, commit_sync=none, TLS off. Separate
   diagnostic overhead from lean results; wall and CPU scopes/nesting explicit.
4. Retain full V3/CRC/SHA/route/interface/terminal/build receipts. Bind
   192.168.100.1/2 on eno17095np0; management SSH is separate. One 8 GiB source
   and one receiver output at a time. MemAvailable >=12 GiB, owned swap=0,
   pswpout delta=0; close mappings and clean owned payload after verification.
5. A candidate needs evidence, focused regression/contract checks, then five
   interleaved baseline/candidate full-task pairs at fixed commits with matching
   binaries per run. Retain only with >=10% median gain, CV<=5%, and no integrity,
   recovery, resource or cleanup regression. No extra candidate if unsuccessful.
6. Run relevant CMake/CTest or script gates, record failures accurately, final
   result/source/evidence hashes and next step, commit/push/readback and sync R2.
   Never turn a profiler sample, nested timing sum or diagnostic rate into a
   complete-task or production readiness claim.

## Initial State / Tools

Both ends start at input SHA. R1 has only the two unrelated untracked task files;
R2 is clean, /dev/shm empty. Dedicated chat tools, shell/SSH and local Codex CLI
are exposed/located this turn; no dispatch or CLI continuation is invoked.
perf exists; perf_event_paranoid=4. Probe actual permission before concluding
sampling availability. Follow executing-plans, systematic-debugging and
verification-before-completion. Local _codex_stage08 source copies remain from
the prior automatically rejected deletion; no deletion retry is part of scope.

perf's actual task-clock probe is rejected. gprofng is installed on both hosts,
but OpenSSL and CPNetFlux checksum-bench calibration both report changed interval
timer warnings and sample CPU far below process CPU. Neither calibration is
accepted as hotspot evidence; no sampler/kernel policy changes are made.
The initial matrix therefore uses existing detailed timing plus thread CPU.
Its harness is a scoped copy of the accepted 8 GiB harness: only root/size and
mapped-byte accounting differ; all original integrity/resource/terminal checks
remain. Mapped bytes must equal file bytes while pwrite-observed bytes remain zero.
