# V3 TCP Buffer Ownership32 Result

Date:2026-11-10. Status: diagnostic implemented and validated; local performance
gate NO-GO. Stop this candidate without runtime integration or parameter reruns.
Input:06d50473d9808e9400b98cf84ba2125dd4d847a7.
Measured implementation:8b7f4fb6cea2a5fbdc7ef70a5cc9bec668586588.
GitHub push/readback verified. Final docs commit and both receipts:closure.json.
R1 sole writer; R2 builds/tests the fixed commit. No agents/worktrees/desktop.

## Implementation And Ownership

Added Linux cpnetflux-tcp-send-bench, a standalone copied/zerocopy TCP comparison,
diagnostic-only SendOwnership ledger, focused tests, isolated LD_PRELOAD fault
shim and run_tcp_send_bench.py for bounded two-node runs. Production libraries,
V3 runtime defaults, wire schema, recovery/checkpoint/ACK policies remain unchanged.
The three production binary hashes equal Publication31.

The32 slots per lane are4096-byte aligned and69632 bytes each. Four lanes allocate
8912896 bytes total (8.5MiB). Each slot owns the full64-byte header,4-byte CRC and
payload. Only positive successful MSG_ZEROCOPY calls advance the kernel32-bit ID;
each partial send holds its own reference. Inclusive/coalesced/out-of-order/wrapped
completion ranges are validated completely before releasing any slot. A frame
whose bytes are only partially accepted cannot be recycled even if its previous
send calls have completed. The4096-entry ledger is fixed-size and backpressures.
Error-queue reads are batched every8 frames or when the pool/ledger needs space.

Truncated, missing, unrelated or unknown notifications fail closed. ENOBUFS is an
explicit error, never silent ordinary-send fallback. Timeout/peer error does not
authorize slot reuse. Terminal failure retains raw allocations until this isolated
diagnostic process exits; this is not a reusable production cancellation design.
Kernel release is tracked separately from the remote V3 FILE_RESULT.

Source slices are pread from a real varied-byte tmpfs regular file into the same
pool in both arms. Both calculate frame CRC and aggregate combine, use65532-byte
maximum data and1MiB chunk boundaries. Receiver uses current V3 encoders/session,
WorkExecutor leases and a fresh MappedFileWindow sink. The sink has no checkpoint,
publication or resume lifecycle; this is not a complete file-task benchmark.
Source identity is checked before/after; independent external full SHA of each
fresh target and source confirms actual bytes. No repeated constant hot buffer.

Both SO_ZEROCOPY set/get checks pass; memlock is unlimited without modification.
R1/R2 kernels are7.0.0-38/34-generic, direct eno17095np0 route and existing SG/TSO
enabled. No limits, mount, THP, swap, IRQ, NUMA, network or RDMA policy changed.

## Correctness And Build Gates

Eight ledger tests first failed against the explicitly unimplemented state machine,
then passed after implementation. They exercise retention, partial-frame lifetime,
merged/ordered/late ranges, wrap, invalid/duplicate ranges, zero/invalid accepted
lengths, fixed-ledger backpressure and busy/invalid slots.

Real-loopback smoke runs8 normal shape cases (copied/zerocopy,1/4 lanes, tiny and
uneven multi-chunk sizes), one successful injected EINTR/EAGAIN/short-write case,
and7 rejection cases:zero return, ENOBUFS, connection error, CRC corruption, dropped
completion timeout, truncated control and unrelated socket error. Existing target,
symlink source and invalid parameter checks also pass. Loopback copied completions
are expected; it is a correctness test, not evidence of NIC copy avoidance.

A separate process-cancellation probe drops release notifications, waits until
the real receiver has allocated data pages, then SIGTERMs the still-running
client. Exit is-15; receiver reports data connection closed, neither reports a
successful result, and both services/fixtures are cleaned. This tests standalone
process cancellation, not a live V3 runtime cancellation or crash recovery path.

Both full builds pass. Related tests: **R1 140/140 (49.64s), R2 140/140 (49.90s)**,
including Publication31's130 cases, the new8 ledger cases/new smoke and existing
TCP-frame smoke. Exact selection is in ctest-pattern.txt; inherited CPNETFLUX_
settings are cleared. Reproduction of focused correctness:

```sh
cmake --build build -j8
ctest --test-dir build -R '^TcpSendOwnership\.|^cpnetflux_tcp_send_bench_smoke$' --output-on-failure
```

The first diagnostic build had an ambiguous empty DynamicFileQueue constructor;
using the explicit PersistentFileIdentity vector fixed it. Its failed build log
is retained. Final R1 source was rebuilt/retested after completion-read batching
and the extra error-queue rejection cases. No failed sample was discarded.

Two-node receipts/configurations/three production hashes plus diagnostic hash
match before inter-node traffic. Diagnostic SHA256:
0bc28dfcccfaa01723365d8e2a15e5d642a18a9b862f711e94f60b8bcd2f0c08.

## Physical-NIC Experiment

Fixed8b7f4fb, R2 192.168.100.2 -> R1 192.168.100.1, explicit binds, four TCP lanes,
TLS off, CRC32C, fresh tmpfs sink. First one64MiB pair passed SHA/resource/network
gates. Then exactly five1GiB AB/BA pairs, all samples retained, no tuning/reruns.
Source preparation/residency/reference SHA is outside service; worker/pool setup,
pread, CRC/combine, header/send, completion processing/final drain, target page
fault/write/map close and remote V3 result are inside their endpoint service.
Connection and target open/resize are separate setup. External SHA and cleanup
are outside service. These intervals are not historical8GiB T_ready.

| Metric | copied | zerocopy |
|---|---:|---:|
| Sender service seconds, five samples | .252095/.277624/.241437/.246114/.241134 | .250567/.247484/.279737/.247104/.264516 |
| Median sender service | .246114s | .250567s |
| Population CV | 5.391% | 4.903% |
| Median sender CPU sum | .555661s | .445407s |
| Median receiver CPU sum | .934394s | .946801s |
| Receiver minor faults/sample | 262237-262239 | 262237-262241 |

Sender CPU sum falls **19.842%**, but median service time rises **1.809%**.
Pairwise wall reductions:+0.606/+10.856/-15.864/-0.402/-9.697% (only2/5 positive).
The required>=15% local wall benefit is absent and copied CV exceeds5%. This is
a failed acceptance gate, not a statistically established1.81% runtime regression.
No conservative>=10% full-task budget is established. Do not integrate simply
because CPU drops, repeat the matrix for a nicer CV, or extrapolate a service
rate to a full file or100Gbps result.

Across the five zerocopy samples96160 successful calls all receive accounted
release notifications, none marked COPIED. This supports absence of reported
deferred-copy fallback on this physical path; it is not a DRAM traffic measurement
or a guarantee that every stack/device copy disappears. Maximum sum of lane held
accepted bytes8069460; maximum final drain1.410ms. Held bytes are not measured
pinned pages. No packet capture/loopback comparison is used as a speed claim.

Receiver CPU demand stays about.94 CPU seconds/GiB, nearly four CPU seconds per
wall second in this four-lane diagnostic. Approximately262K minor faults persist.
This supports examining receiver service/work distribution next; it does not prove
all receiver CPU is fault handling, that faults directly equal wall time, or that
more threads scale. Scope17's CPU sample percentages have a different contract.

## Resources, Evidence And Cleanup

Evidence:/home/Su/experiments/CPNetFlux/2026-11-10-v3-tcp-buffer-ownership-32.
Raw per-run stdout/stderr, source/reference SHA, results, guards, counters, routes,
capabilities, red/green/final tests, initial build failure, cancellation, analyses,
receipts and cleanup are retained. R2's logs/receipts are copied under r2/.

All12 transfers pass independent full source/target SHA. Across4301 guard samples,
minimum MemAvailable29035307008 bytes (~27.04GiB), sampled owned swap0 and host
swap-out delta0 on both hosts. Source preparation is also guarded. This is sampled
monitoring, not proof of an instantaneous RSS/pinned-memory peak. Each host holds
at most one1GiB source or target, plus bounded pools/mappings; no64GiB fixture.
Both batches remove their exact marked /dev/shm/cpnetflux-tcp-send-32-fixed8b7f4fb
roots on both nodes. Owned loopback/cancel roots and diagnostic processes are gone.
Production identities stay R1 126766/1191040 and R2 164834/1679976.

Every measured pair's rx/tx error/drop/missed/CRC deltas are0 on both interfaces;
overall matrix deltas are rechecked at closure. This does not resolve historical
scope17+94. Two October14 untracked R1 documents remain untouched. No credentials,
payload or binaries enter Git or the local evidence mirror.

## Decision And Next Boundary

Keep the standalone diagnostic and its tested ownership model for research; current
copy-avoidance candidate is NO-GO for V3 integration. Production stays unchanged.
Large-file reference remains approximately27.3Gbps under scope17;30-50/70-85/100Gbps
are unpassed. RDMA, per-file attribution, S1B default promotion remain paused/open
as recorded in the route. This round closes one candidate, not all large-file work.

Next: a bounded receiver service/work-distribution budget decision, reusing scope17
and these records plus current source before adding instrumentation. Distinguish
kernel receive/page work, CRC and userspace copy/lease work; identify one concrete
mechanism that can reduce critical-path work with a conservative>=10% whole-task
budget. The observed CPU/wall ratio is not authority to expand threads/queues.
Do not reopen sender-inline, population, direct-receive, source-mmap or collapse
candidates unchanged. Keep preparation/zeroing/cleanup and fresh-target semantics
honest. If no new mechanism clears the budget, record that limit and stop the
branch; do not automatically create another profiler or performance sweep.
