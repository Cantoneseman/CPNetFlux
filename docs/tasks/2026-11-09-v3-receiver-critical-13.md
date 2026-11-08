# V3 Receiver Critical Path 13

Date:2026-11-09. Input:821afa721dc806fbb4efd92406998c48a18e093f.
Branch:codex/V3-GRIDFTP-OPTIMIZED-RERUN-01. R1 single serial writer.

## Scope and Hypothesis

Scope12 removed sender handoff CPU but regressed complete task throughput2.313%.
Keep queued sender as control. Existing receiver wall sums cannot separate CPU,
sleeping and scheduler delay. Add default-off receiver CPU wrappers around actual
frame read,CRC,state validation,lease,write,queue and range physical/metadata work.
Counters identify attempts and thread ownership; nested intervals are not additive.
First compare identical detail observation with only new CPU clocks off/on, then
use actual matched file/sender geometry to select at most one justified candidate.
No unique receiver bottleneck is assumed before this measurement.

Non-goals:RDMA,more workers/queues,sender fusion/page population,protocol changes,
CRC/lease removal,TLS/durable/production acceptance,system networking or affinity.
No no-file/synthetic throughput may substitute for complete task acceptance.

## Acceptance

- Test first against input binaries:real striped upload/download must fail for
  missing receiver CPU contract. Enabled/disabled,inline/queued ownership and
  complete content/publish correctness must pass after implementation.
- Related CMake/CTest including resume/compression/leases. Preserve default-off
  behavior and original sender observation contracts. Test subsets named honestly.
- Commit/push/readback source on verified origin before R2 sync/build/test.
  Same commit/config/version/eight binary hashes before any interhost task.
- Three interleaved8GiB observer off/on pairs,queued sender,four TCP lanes/eight
  1GiB ranges,inline/mmap receiver64MiB windows,same detail observations and CRC,
  verified_chunks/checkpoint16,TLSoff,commit_syncnone. CPU clock is sole variable.
  Examine wrapper byte/call/CPU containment and sender/receiver resource counters.
- Only implement a runtime optimization when specific evidence supports it; amend
  scope first. Candidate needs five lean A/B pairs,>=10%median task gain,CV<=5%,
  independent SHA,exact bytes,V3 terminals,route/NIC,resources and cleanup.
- At most8GiB owned source on R2 and8GiB output on R1. MemAvailable>=12GiB,
  owned VmSwap0,host pswpout delta0. Clean exact owned payloads after each task
  and source at end;preserve production126766/164834 and original start times.
- Record unresolved alternatives. Do not claim receiver limitation solely from
  CPU totals or claim local service rate is complete transfer capability.

## Expected Files and Evidence

include/cpnetflux/core/io/persistent_tree_transfer.h,
src/core/io/persistent_tree_transfer.cpp,
tools/test/run_cpnetflux_range_observability_smoke.py,CMakeLists.txt;
this task,result and docs/ROADMAP.md. Other changes require recorded amendment.
External diagnostic/analyzer/harness/raw evidence remain outside Git at
/home/Su/experiments/CPNetFlux/2026-11-09-v3-receiver-critical-13.
Owned tmpfs root:/dev/shm/cpnetflux-v3-receiver-critical-20261109.
Reuse scope12 harness with recorded hashes;never seed64GiB or overwrite old data.

## Tool and State Check

This turn actual exec/apply_patch/terminal tools,SSH/SCP and Codex CLI discovered;
chat send entry available but not invoked. No agents,worktrees,desktop or chat
dispatch. R1 tracked tree clean at input;two unrelated October14 untracked task
files preserved. Current relevant skills:executing-plans,systematic-debugging,
test-driven-development,verification-before-completion. No additional approval
is needed for this authorized serial development and bounded experiment scope.

## Amendment:Dynamic Download Local Executor Gap

The first cpu0/cpu1 pair at8f42c851 completed bytes/SHA/terminals/resources and
cleaned outputs,but the cpu1 contract assertion rejected local_work_executor=false.
Code confirms tree_transfer_client.cpp creates incoming dynamic download ready
only with globalScheduler;dynamic+scheduler=off skips the local WorkExecutor.
This is an input-path fact,not an observation bug or a payload corruption result.
Do not invent receiver lease cost or rewrite these two original receipts as pass.

V3-only rules require closing this client path before further performance work.
Decision:always create bounded incoming dynamic download work,attach the optional
global scheduler only when present. No global scheduler is activated by this fix.
Add actual dynamic+scheduler-off download regression asserting local executor
and balanced per-frame lease counts;show failure before runtime implementation.
Add affected src/core/io/tree_transfer_client.cpp and
docs/DECISIONS/2026-11-09-v3-dynamic-download-local-executor.md;update roadmap.
Original three-pair acceptance at8f42c851 is superseded;preserve its initial two
samples,then rerun allthree observer pairs under distinct v2 labels at the new
fixed source. This correctness repair is not a claimed throughput optimization.
If it changes throughput,report it as a semantic baseline correction,not a win.
