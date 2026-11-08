# V3 Sender Service 12

Date:2026-11-08. Input:fa25de238164b7930db4672b0c3023833f684d1c.
Authoritative root:/home/Su/projects/CPNetFlux on R770-1, one serial writer.

## Scope and Hypothesis

Scope11 attributed significant sender CPU to read/socket work but did not prove
an internal root cause. First isolate resident source read, CRC and existing
bounded queue handoff under1/4 lanes. Compare same-thread processing with the
existing depth2 producer/consumer ownership, keeping frame geometry65532 bytes
and1 MiB chunk boundaries. Record actual byte/call/digest accounting, thread
CPU/user/sys/context switches, wall and queue waits. Local diagnostics are not
file-task throughput. Inspect runtime scheduling read-only if the gap persists.

If supported by evidence, implement at most one bounded sender path candidate;
amend exact affected source/tests and behavior before that edit. No assumed CRC
backend/cache defect:current backend is auto and cached GF(2) powers already exist.

Initial affected tracked files:this task. Diagnostic scripts/CPP, receipts and
hashes stay outside Git at:
/home/Su/experiments/CPNetFlux/2026-11-08-v3-sender-service-12.
Final tracked files:this task,result and docs/ROADMAP.md; candidate source/tests
must be named in a scope amendment before implementation.

## Non-goals

No RDMA, extra workers/queues/larger buffer budgets, page-population retry,
protocol/frame-size changes, weakened CRC/work leases/checkpoint/recovery/atomic
publication, kernel/network/NUMA/affinity changes or production restart. Preserve
unrelated October14 task files and CPSS(DCC). No agents or worktrees.

## Acceptance and Order

1. Verify tools, both tree identities, resource headroom and exact target code.
   Shell/SSH/SCP and dedicated chat dispatch exposed; Codex CLI located. No chat
   dispatch or CLI continuation invoked. Use existing serial branch.
2. Bounded local service control using the existing library, same content/ranges,
   one <=8 GiB source per host, <=1 MiB application payload capacity per four
   lanes. Independent source SHA/residency outside timing. CRC digests must agree
   across comparable modes. Record failures, CPU measurement scope and resource
   gates, not only favorable samples.
3. Candidate only with an explained repeatable gap. Meaningful behavioral tests
   first, related CMake/CTest, explicit R1 commit/push/GitHub readback, then fixed
   R2 synchronization/build/test. Same commit/config/version/eight binary hashes
   before any CPNetFlux interhost transfer.
4. Five interleaved complete8 GiB base/candidate pairs, four TCP lanes, explicit
   .100.1/.2 bind/routes/NIC counters, inline/mmap receiver, CRC32C/verified_chunks,
   checkpoint16/TLSoff/commit_sync=none. Require >=10% median task gain,CV<=5%,
   integrity/resources without regression. Existing observations equal in both
   arms; measure instrumentation separately if its boundaries change.
5. MemAvailable>=12 GiB, owned swap0, host pswpout delta0, at most one source and
   output each host; cleanup only owned exact paths. Audit original production
   PID/start times. Final task/result/ROADMAP, scripts/dependency/evidence hashes,
   commit/push/readback/R2 sync and explicit next handoff.

## Initial State

Both hosts at input. R1 has only two unrelated untracked October14 tasks; R2 clean.
Both tmpfs empty at prior scope close. Data transport remains TCP, not verbs.
Scope11 source2bd8280e related CTest198/198 per host and six complete tasks passed.
Current task throughput27.857/27.927 Gbps with sender clocks off/on; clocks added
3.920% sender CPU. Do not treat this as a throughput improvement or zero overhead.

## Candidate Scope Amendment

18 local diagnostic samples completed on source R2:four-lane same-thread read+
CRC median153.031 Gbps,CPU1.666 s; bounded queue128.185 Gbps,CPU3.178 s and about
130371 voluntary switches. All digest/sink/count checks agree,CV<=1.04% for these
two cells. This establishes an isolated handoff cost, not the root of all task
cost or predicted network gain. The normal memory guard did not identify the
external diagnostic binary(owned RSS0 is uncovered); supplement with time -v
resource evidence. Host memory/swap and exact application capacity gates passed.

Single candidate:explicit CPNETFLUX_SENDER_PAYLOAD_EXECUTION=inline|queued,
default queued. Inline executes the existing per-frame read/CRC preparation on
the network lane and retains a single reusable frame buffer; no producer thread
or queue. Keep the exact send/compression/corruption/lease path, missing-range
iteration, max-chunks, source identity checks and terminal handling. No protocol
changes. It may lose overlap during socket blocking; full-task A/B decides.

Affected files before implementation:include/cpnetflux/core/io/persistent_tree_transfer.h;
src/core/io/persistent_tree_transfer.cpp;CMakeLists.txt;
tools/test/run_cpnetflux_range_observability_smoke.py. Add execution-mode exports,
zero queue/depth/producer CPU accounting for inline, bounded buffer accounting,
and explicit CPU owner so read/CRC is contained by lane CPU in inline mode.
Test first on old binary:real striped upload/download inline contract must fail.
Then check enabled/disabled profiling, invalid setting rejection, related tests
under default and inline(including resume/corruption/source change/compression).
Do not count local diagnostic Gbps as transfer throughput.

Additional affected regression file:tests/unit/persistent_data_session_test.cpp.
The first inline202-test run had201 passes and one legacy assertion requiring a
nonempty sender queue. Update that existing two-range/content/publication test to
exercise both explicit execution modes, preserve positive queue checks for queued,
and require zero queue metrics/single-buffer ownership for inline. Keep the
initial failed receipt; it is a contract mismatch, not evidence of data failure.
