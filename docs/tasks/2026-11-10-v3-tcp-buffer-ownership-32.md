# V3 TCP Buffer Ownership32

Date:2026-11-10. Status: next task; design/diagnostic not implemented.
Runtime input:a0ae45728cb4223aec45c9a2f970d9001d8f8c01, after Publication31.
Exact handoff/docs input is the Publication31 closure.json final_commit; record
the actual HEAD before edits. R1 sole writer, R2 fixed-commit build/test peer.

## Architecture Decision And Hypothesis

Return to large-file TCP data movement after the bounded
[publication repair](2026-11-10-v3-publication-reconcile-31-result.md). Pause the
per-file attribution branch: Audit30 established no conservative10% removable
full-task budget. SourceRead20's source-mmap candidate is also closed; do not
repeat that candidate or infer every copy-removal approach has been exhausted.

Current FramedDataSocket::writeFrame gathers header, CRC prefix and payload into
ordinary sendmsg. It does not make an extra userspace concatenation copy. Its
caller may recycle the owned vector after the synchronous write completes.
Hypothesis: retaining a bounded pool of complete immutable frames until kernel
release notifications may reduce sender-to-kernel TCP copy cost enough to help
the whole task. This is unproven, not an accepted runtime optimization.

Kernel semantics source:[Linux MSG_ZEROCOPY](https://docs.kernel.org/networking/msg_zerocopy.html).
Copy avoidance adds page/accounting/completion work, requires buffer retention,
can report deferred copying and does not imply remote receipt or persistence.
Loopback forces deferred copy; local tests can validate correctness but cannot
establish physical-NIC copy avoidance or speed. Do not change the kernel to evade it.

## Scope And Non-Goals

First validate the ownership contract and host capability, then implement at most
one bounded standalone TCP send diagnostic if feasible. Keep V3 wire framing and
CRC/checkpoint/lease/ACK semantics unchanged. No initial runtime integration,
global FramedDataSocket flag, source mapping borrow, RDMA, TLS optimization,
sendfile/splice combination, large DATA frames, thread/window sweep, system
network/memlock/THP/swap changes or small-file microbatch work.

Read only relevant code: framed_data_socket.{h,cpp}, persistent_data_session.cpp,
persistent_tree_transfer.cpp sender queue/recycle sites, payload_pipeline.h,
tcp_frame_bench.cpp and its smoke. Record actual line anchors at the input commit.
Do not change existing direct-receive diagnostics or production publication code.

## Development Sequence And Acceptance

1. Freeze a small ownership design and capability receipt. Check the two kernels,
   SO_ZEROCOPY availability, locked-memory limits and NIC route/features read-only.
   Identify every borrowed header/prefix/payload lifetime and the real vector
   recycle point. Before implementation specify one per-connection owner,
   immutable pooled slots, accepted-byte tracking for partial sends, successful
   syscall IDs, inclusive/coalesced/out-of-order/wrapping completion ranges,
   copied-completion classification and unrelated error-queue handling. Kernel
   completion frees a buffer; it must never certify a V3 chunk or advance ACK.
2. Specify bounded backpressure, cancellation and terminal-error behavior before
   coding. All three frame spans remain owned until safely released; stack headers
   cannot outlive their call. No recycling on send return, timeout, shutdown or
   peer error without proof of release. A standalone process may retain resources
   until exit on terminal failure; do not hide that as a reusable runtime design.
   Plan explicit capability/ENOBUFS/copied outcomes, not silent success. Do not
   increase host limits when the contract cannot be met.
3. If feasible, add a separate cpnetflux-tcp-send-bench diagnostic and focused
   tests. Likely files:tools/perf/tcp_send_bench.cpp, a small diagnostic-only owner
   helper if needed, tools/test/run_cpnetflux_tcp_send_bench_smoke.py, focused
   tests/unit tests and CMakeLists.txt. Finalize exact edit files before mutation.
   Reuse existing V3 encoding/CRC and result validation; do not hand-roll another
   data protocol. Compare ordinary copied send versus one explicit zero-copy
   mode at identical framing, payload, CRC, lane count and receiver work.
4. Test ownership first: varied bytes and independent full CRC/content check,
   partial writes, EINTR/EAGAIN, failed/zero-byte sends, merged/out-of-order/wrapped
   completions, copied completion, unrelated errors, peer close, cancellation and
   drain timeout. Assert no early slot reuse and bounded bytes/slots. Distinguish
   deterministic completion-accounting tests from actual kernel tests. Local
   loopback results are correctness-only; unsupported capability is an explicit
   blocked feasibility result, not measured throughput.
5. Commit/push/readback on R1, then sync R2 and match configuration/build/three
   runtime binaries plus the diagnostic hash before inter-node tests. Start with
   a small physical-NIC correctness probe using192.168.100.2 ->192.168.100.1,
   explicit binds and isolated ports. Retain source SHA/CRC, route/interface/error
   counters and completion/copied statistics. If unsafe, unsupported, unable to
   drain, or mostly copied without explanation, stop before performance runs.
6. Only after correctness, run at most five fixed-commit AB/BA diagnostic pairs,
   one resident1GiB varied-byte source, four lanes, identical65532-byte maximum
   data slices/current chunk boundaries, fixed pool and receiver. Prefer at most
   4MiB in-flight slots per lane,16MiB total, never exceed64MiB owned pool. One
   source/target at a time; no64GiB fixture. Count preparation/CRC/frame work,
   completion processing and final drain in both comparable service intervals;
   keep remote V3 result separate from local release. Collect wall/CPU, bytes,
   successful send calls, completion ranges, copied/true-release counts, errors,
   faults and peak owned/pinned-accounting bytes. Do not label queued bytes as
   proven pinned bytes or CPU samples as task-time savings.
7. Retain all samples and cleanup costs outside timed service. A local diagnostic
   requires median wall improvement>=15%, CV<=5% and integrity/resource gates,
   plus a concrete conservative>=10% complete-task budget, before proposing V3
   integration. CPU savings alone are insufficient. Use existing scope17 evidence
   carefully: overlapping send/receive service cannot be summed into wall savings.
   If the budget cannot be established in this bounded round, stop and record it;
   no repeated profiler loop, pool sweep or seed tuning to seek a passing number.

## Resource And Delivery Gates

For any inter-node probe keep MemAvailable>=12GiB, sampled owned swap0 and host
swap-out delta0 on both nodes; stop on violations and clean only marked fixtures.
Source/target/pool memory all count. Preserve production identities, retain route
and NIC deltas and keep historical scope17+94 unresolved. Do not claim this new
diagnostic fixes that history. Evidence under /home/Su/experiments/CPNetFlux with
this task's dated ID; no payload/binaries/credentials committed or mirrored.

Deliver a capability/ownership decision even if no diagnostic proceeds, or tested
diagnostic source and bounded results if feasible, with exact input/test commands,
known limits, GitHub readback, two-node receipts and cleaned fixtures. A diagnostic
pass only authorizes a separate reviewed runtime design. Future integration must
preserve cancel/resume/range/lease semantics and then pass>=5 full8GiB task pairs,
median throughput gain>=10%, CV<=5%, integrity/resource/network gates. Current
27.3Gbps is not upgraded by theoretical copy counts or this task's local service rate.
