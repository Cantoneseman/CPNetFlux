# V3 Observation Audit 16 — complete-task measurement control

Date: 2026-11-09. Input: 65093281c945eb6924fea4c00a77eedb9c9fe6e6.
Branch: codex/V3-GRIDFTP-OPTIMIZED-RERUN-01. Sole writer: R770-1.

## Scope and hypothesis

After scope15 closed direct mapped receive (<10% feasibility gain), measure the
cost of EXISTING range/payload detail observation in real complete V3 file tasks.
Use existing switches, no runtime implementation changes: detail ON sets
CPNETFLUX_RANGE_TIMING=on and CPNETFLUX_RECEIVER_PROFILE=1; detail OFF sets off/0.
Sender/receiver CPU profiles and mapped-write profile stay0 in BOTH arms.
Basic phase timing and sparse protocol-boundary traces stay ON equally; detail
OFF is not a claim of zero instrumentation. Disabling diagnostics is not a new
algorithmic optimization. Scope13 local WorkExecutor fix is present in both arms.

Hold fixed: 8GiB resident tmpfs source on R2, fresh output on R1, four TCP lanes,
eight1GiB ranges, queued sender, inline experimental mmap receiver,64MiB window
per lane, read0/write0, buffer64MiB, batch1/depth2, CRC32C, verified_chunks,
checkpoint16, commit_sync=none, preallocate off, compression/TLS off. Same commit,
configuration, version and binaries on both hosts. Actual V3 leases/checkpoints,
verification and publication remain required when their detail counters are off.

Non-goals: new algorithm/backend, direct-receive integration, sender-inline or
population retries, additional threads/queues, RDMA, system/THP/affinity changes,
durable/TLS/production readiness,100Gbps claim. No new candidate is authorized by
this audit alone. Existing three-stage engineering is closed; performance gate
remains open until evidence passes under an explicit configuration.

## Acceptance and commands

1. Commit scope, push verified GitHub origin and read back remote SHA. Fast-forward
   R2 only to that commit, build both with existing CMake configuration. Record
   versions/config and ten binary SHA-256 values before any interhost task.
2. Relevant CTest gates include range-observation disabled, dynamic download
   local WorkExecutor, mapped profile off/unobserved, mapping fault/recovery and
   existing V3 transport/frame/checkpoint tests. Save exact selected test names
   and full output. No instrumentation-only tests substitute for transfer SHA.
3. Run5 interleaved ON/OFF pairs, alternate order. Primary metric:8GiB*8 divided
   by full client task wall (includes setup/finalization); also process user/sys,
   receiver/sender phase detail, external SHA and all protocol termini. Do not
   compare scope15 framed payload rates to these complete-task rates. Report
   medians, sample-standard-deviation CV and paired differences; CV<=5% required
   for a stable baseline. No >=10% algorithmic-gain claim for observer removal.
4. Assert actual_transport V3/data_transport TCP,8 ranges,139264 payload read/write
   calls,8GiB validated, no retry/recovery, identical stage/verification settings.
   ON: positive observation counters; OFF: range observation false, nullable
   profiles absent and disabled detail counters zero. Mapped write byte/count
   and lifetime counters remain valid in both. Preserve checkpoint evidence and
   run existing recovery/lease gates; null diagnostic counters do not mean no IO.
5. Independent SHA-256 outside task timing; source resident before/after. Save
   explicit192.168.100.1/.2 bind, routes via eno17095np0, interface byte counters,
   memory guards, PID identity and exit status. TCP over this NIC is not verbs.
6. Before/during tasks require MemAvailable>=12GiB, owned VmSwap0, host pswpout
   delta0. At most one8GiB source R2 and one8GiB output R1; mappings<=256MiB.
   Clean each exact owned output after verification, clean owned source at end.
   Preserve production PIDs/start ticks R1:126766/1191040,R2:164834/1679976 and
   unrelated October14 untracked docs. Abort on a resource/integrity violation.
7. Summarize results/limitations/next bounded step in result and ROADMAP. Commit
   only explicit documents; push/read back SHA and sync R2. No git add . or clean.

## Files, evidence and handoff

Tracked changes intended: this task, corresponding -result.md, docs/ROADMAP.md.
Reproducible wrapper/analyzer and raw small evidence stay outside repository in
/home/Su/experiments/CPNetFlux/2026-11-09-v3-observation-audit-16, with dependency
and evidence hashes. Owned tmpfs root:
/dev/shm/cpnetflux-v3-observation-audit-20261109.
Harness reuses pinned scope12 complete-task/baseline and guard helpers; the
source generator is fixed inside this round wrapper with the scope14 source
shape. Historical seed helpers and scope15 frame-only measurements are not used. Acceptance commands/results and output
commit are recorded in result; no runtime code change unless separately scoped.

Tools checked this turn: shell/terminal/apply_patch and chat-send are exposed;
SSH/SCP/Codex CLI present. Chat dispatch/CLI resume not invoked. Single serial
writer, no agents/worktrees/desktop. Execution, systematic-debugging and
verification skills applied to the existing authorized plan.

## Completion / handoff

Scope/measurement commit25ff8eed56fd88facdfbd4df4341567a23f4019d pushed and
remote-SHA-readback; no runtime code changed. Both hosts245/245 related CTest,
matching configuration/version/10 binary hashes. Five interleaved8GiB pairs
all pass actual V3/CRC/external SHA/committed8192-chunk manifest/resources.
ON/OFF medians27.611519/27.858476Gbps, sample CV1.856872/2.396810%; ratio+0.894399%,
paired median+0.243070%, no stable benefit. Full-task30Gbps gate remains unmet.
Both payload roots absent, tmpfs used0, production PID/start ticks preserved.
See [result](2026-11-09-v3-observation-audit-16-result.md) for evidence and the
next independent kernel/memory service-rate diagnosis; no architecture expansion.
