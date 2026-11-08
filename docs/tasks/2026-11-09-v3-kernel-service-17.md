# V3 Kernel Service17 — bounded process-scoped sampling

Date2026-11-09. Input50b7c0aa6780d036e5946b5f3208a0d931bbf728.
Branch codex/V3-GRIDFTP-OPTIMIZED-RERUN-01; R770-1 sole writer.

## Scope and hypotheses

Execute scope16's next independent diagnosis: attribute CPU in the actual complete
V3 TCP/tmpfs path, especially receiver sys~6.4s and~2.11M minor faults/8GiB.
Competing explanations: first-touch/page allocation and mapping faults; TCP and
file data copies; sender resident-file reads; lock/scheduling costs. None is yet
proven unique. Do not infer wall savings by subtracting cumulative CPU.

Use process-scoped99Hz cpu-clock sampling on BOTH owned processes, low-overhead
frame-pointer callchains (user unwinds may be incomplete), plus the SAME bounded
/proc sampler in both baseline/profile arms. Attach to exact owned PID/start tick
and command before client exec/server work; require profiler-ready handshake.
Exclude profiler setup from client-exec-to-exit wall in BOTH arms. Save full
external process resource usage and explicitly report gate/setup timing separately.
Profile intervals may include idle setup but independent SHA is outside the
profiled processes. Preserve flat Self samples and kernel/user attribution;
callchain Children percentages overlap and must not be added.

Current perf_event_paranoid=4 prevents unprivileged perf. Existing sudo -n perf
works on both hosts; use only scoped perf operations and exact profiler shutdown.
Do not alter sysctl, network, THP, affinity, services or credentials. R2 has one
NUMA node; record both hosts' topology rather than assume NUMA is the cause.

## Fixed workload and non-goals

Same as scope16 detail-OFF: real resident8GiB source R2, fresh output R1,4 TCP
lanes/eight1GiB ranges, queued sender/inline experimental mmap receiver,read0/
write0,batch1/depth2,payload64MiB,mapping64MiB/lane<=256MiB,CRC32C,verified_chunks,
manifest/checkpoint16,syncnone,preallocateoff,compression/TLSoff. New CPU profiles0,
existing detailoff; basic phase/trace retained equally. No output prewarming.

Non-goals: runtime code/default changes, new backend/architecture, direct receive,
population or sender-inline retries, more concurrency, RDMA, durable/production/
100G claims. A candidate requires separately recorded mechanism and benefit
budget; this diagnosis is not itself a performance optimization.

## Acceptance

1. Record input/scope and commit/push/readback, sync R2 fixed commit, build same
   configuration/version and verify10 binary hashes before transfers. Preserve
   unrelated October14 docs. No agents, worktrees, desktop or parallel writers.
2. Before8GiB experiments, smoke-test profiler attach/control/stop with owned
   small process, inheritance of new threads, readable symbols, sample file and
   cleanup; capture failures, do not call unavailable or partial sampling valid.
3. Run3 interleaved baseline/profile pairs, same common /proc and memory guards.
   CV<=5% each and absolute median task-wall/profile CPU difference<=5% are the
   predeclared low-overhead gates. If failed, report perturbed profiles; at most
   one lower-frequency remeasurement may be separately scoped, not sample fishing.
   No profile sample becomes a new throughput gain. Aim>=300 usable samples/host
   per transfer; report actual count/lost/unknown/unwind quality and limitations.
4. Require actualV3/TCP,8GiB,8ranges,139264 payload read/write calls,CRC/full
   external SHA,committed manifest8192 verified chunks,successful protocol termini,
   no recovery. Bind192.168.100.1/.2 and retain routes/interface counters.
5. MinMemAvailable>=12GiB,ownedVmSwap0,hostpswpoutdelta0,one8GiBsource/output max
   per respective host. Clean each owned output afterSHA and source at end.
   Profiler files bounded to32MiB each, no systemwide sampling/long capture.
   Preserve production PID/start ticks R1:126766/1191040,R2:164834/1679976.
6. Relevant CMake/CTest or diagnostic script gates must pass. As runtime unchanged,
   reuse unchanged-code scope16 regression evidence explicitly and run fresh
   mapped/profile/local-executor/recovery gates plus profiler contract smoke.
7. Analyze non-overlapping flat sample categories; unknown stays unknown. Report
   sampled CPU share rather than percentage of wall or precise byte-service time.
   Record actionable next hypothesis only if mechanism/budget is justified;
   otherwise stop expanding three-stage and keep full-file performance gate open.
8. Update result/ROADMAP, commit explicit documents and push/readback. R2 sync
   only fixed commits. Raw evidence stays outsideGit, with hashes and dependencies.

## Intended files and evidence

Tracked: this task,corresponding-result.md,docs/ROADMAP.md. Diagnostic harness,
contract smoke,/proc monitor,analysis and raw profiles under
/home/Su/experiments/CPNetFlux/2026-11-09-v3-kernel-service-17.
Owned tmpfs /dev/shm/cpnetflux-v3-kernel-service-20261109.
Runtime changes require a new explicit scope, not incidental edits here.
Tools this turn: shell/terminal/apply_patch/chat-send exposed;SSH/SCP/Codex CLI
found;chat-send/CLI resume not invoked. Serial current agent only. Execution,
systematic-debugging and verification skills applied.
