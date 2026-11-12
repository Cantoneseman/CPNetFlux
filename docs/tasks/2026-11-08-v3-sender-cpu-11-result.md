# V3 Sender CPU Attribution 11 Result

Date:2026-11-08. Input:11742a948c50054d1bcaa582280c403a2c9b38a9.
Implementation:2bd8280e671e1f5bdd061807c96c2b0f6b465054.
Branch:codex/V3-GRIDFTP-OPTIMIZED-RERUN-01.
Origin:git@github.com:Cantoneseman/CPNetFlux.git; source push SHA readback matched.

## Decision

Sender CPU attribution is implemented and verified. No new throughput candidate
was selected or accepted. Complete-task throughput remains approximately28 Gbps;
30-50 Gbps,70-85/100 Gbps and production readiness remain unproven. Scope08's
accepted explicit inline/mmap download option remains experimental; defaults,
V3 protocol, frame size, CRC, leases, checkpoint/recovery, publication and runtime
concurrency policy are unchanged. RDMA and structural queue/worker expansion stay
paused. TCP over the RDMA-capable NIC is not native verbs.

## Implementation and Verification

CPNETFLUX_SENDER_PROFILE=1 enables additional sender observations only when
existing phase/range observations are enabled. Default, receiver and observation-
disabled exports contain sender_cpu_observation:null. Enabled export has schema
cpnetflux_sender_cpu_observation_v1, CLOCK_THREAD_CPUTIME_ID, explicit measurement
scope, wrapper call/attempt counts, logical bytes and CPU durations.

Producer:readAtAll, frame checksum plus file CRC combine, bounded pipeline push.
Lane:pipeline pop, prepare, lease start/finish, buffered socket write. Queue CPU
excludes sleeping time and includes reacquire work. Thread totals contain wrapper
CPU; residual is unnamed work. Instrumentation contributes CPU to measurements.
Cross-thread wall intervals overlap and are not CPU percentages or task duration.

Real128 MiB striped upload/download smoke tests check both endpoint exports,
content/CRC, gating, complete logical-byte accounting, wrapper relationships and
CPU containment. The new smoke failed on the previous binary with missing sender
CPU observation contract before source changes. Existing range smoke and new
enabled/disabled sender CTest cases pass.

Both hosts built the fixed implementation commit with matching0.1.0 version,
configuration and eight binary SHA-256 hashes. Desktop/cross-domain tests off;
tests, ISA-L, io_uring, TLS and RDMA compiled on. Transfer selection was TCP with
data TLS off. Related CTest:R1 **198/198**,74.38 s; R2 **198/198**,74.56 s.
This is a related test subset, not a new full-suite or production acceptance.

Reproduction commands from project root:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R 'Persistent|BoundedPayload|TransferStages|WorkExecutor|DynamicFileQueue|sender_cpu|range_.*observability|range_observation_export|mapped_download'
python3 tools/test/run_cpnetflux_range_observability_smoke.py --build-dir build --sender-profile 1
```

## Complete-Task Observer Control

Three interleaved base/CPU pairs:base,cpu / cpu,base / base,cpu. Existing range and
receiver payload observations stay on in both arms. Only added sender CPU clocks
are toggled. This controls their incremental cost, not all observation overhead.

R770-2 -> R770-1,8 GiB resident tmpfs source,4 TCP lanes,8 one-GiB ranges, direct
POSIX source, read0/write0, depth2/batch1, inline/mmap receiver64 MiB windows,
CRC32C/verified_chunks, checkpoint16, no compression/preallocation,
commit_sync=none and TLS off. Explicit192.168.100.2/.1 bind, routes and eno17095np0
counters retained. Both endpoints reported no native QPs. Source physical IO0.
Task rate includes launch through successful protocol completion; independent
SHA-256 and cleanup are outside this timing. Every task used a fresh destination.

| Arm | n | Median task Gbps | CV | Median task s | Sender process CPU s | Receiver CPU s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Base | 3 | 27.857158 | 1.168514% | 2.466852 | 9.95 | 8.02 |
| Sender CPU clocks | 3 | 27.926832 | 0.618072% | 2.460697 | 10.34 | 7.99 |

CPU-clock arm wall difference:-0.249487%; sender CPU median change:+3.919598%.
The wall difference is within this small sample's variation, not a speedup or
proof of zero observer overhead. Do not compare these arms as an optimization.

All six tasks passed V3 range/data/control terminals, logical bytes, independent
SHA-256, source residency, interface/resource and cleanup gates. Per task139264
DATA frames,139272 pops including8 range EOFs,139264 read/checksum/push/socket and
each lease-start/finish calls. Receiver128 map/unmap pairs,8 GiB written,
active mapped bytes0, map failures0. Each active range owns at most64 MiB mapped
address space; four active ranges cap it at256 MiB.

## Sender Attribution

Three CPU-observed tasks, summed across ranges then median across tasks:

| Wrapper | CPU seconds per8 GiB |
| --- | ---: |
| Source readAtAll | 2.620889 |
| Frame checksum plus CRC combine | 1.474786 |
| Pipeline push | 0.545018 |
| Pipeline pop | 0.392102 |
| Frame preparation | 0.116994 |
| Lease start/finish | 0.716261 |
| Buffered socket write | 3.481994 |

Producer/lane thread CPU medians5.006223/5.402307 s. Unnamed producer/lane residual
medians0.471963/0.658909 s; medians of components need not add to the median total.
Read plus socket CPU is approximately59% of observed sender process CPU. It does
not establish which kernel function, cache effect or lock causes that cost.
Producer/consumer wait wall and socket wall are overlapping time, not extra CPU.
The sender has both data-supply work and downstream backpressure; queue waiting
alone does not justify more buffering or concurrency.

## Checksum Isolation Control

An external diagnostic links the same build's core/ISA-L libraries, uses the real
ChecksumComputer API,17 frames per1 MiB(16x65532+64),139264 frames for8 GiB logical
data. A single reusable1 MiB buffer is allocated, not an8 GiB payload. Three
serial repetitions per host separate CRC update, precomputed-frame combine and
both. First-chunk CRC agrees with full input; combine/both produce identical
final CRC2779589221 and matching frame-value sums; exit status0 on both hosts.

| Host | Update CPU s | Combine CPU s | Both CPU s |
| --- | ---: | ---: | ---: |
| R770-1 | 0.101091 | 0.184075 | 0.293260 |
| R770-2 | 0.101721 | 0.184031 | 0.293456 |

This is single-thread hot-cache CPU cost without source IO, queue handoff,
socket traffic or per-frame CPU clocks. It is not task throughput, a replacement
baseline, or a prediction of savings. Its difference from1.475 s in the concurrent
task is unresolved. Existing64 GF(2) byte-power operators are already cached;
repeating that historical optimization or deleting CRC is not justified here.
No runtime CRC candidate was introduced; the five-pair candidate gate is not met
or claimed by these diagnostics.

## Resources, Cleanup and Evidence

Minimum sampled MemAvailable:R1 20.562 GiB/R2 19.988 GiB. Maximum sampled owned
RSS:R1 258.617 MiB/R2 19.813 MiB. Owned VmSwap and host pswpout deltas0 throughout
the six tasks. Receiver major faults0. Polled RSS is not an instantaneous peak
guarantee. Resource receipts for the small checksum control are also retained.

Source SHA-256:0dacd7937ddb7d74d005cb056020a66d6524d178c0437dfe76099b5357e56187.
Each fresh output/sidecar removed after verification; source size/SHA rechecked
before unlink. Exact empty owned directories removed with rmdir. Both hosts final
/dev/shm used bytes0, no owned live transfer. Production PID126766/164834 and
Oct30 19:36:11/19:36:13 start times preserved. Unrelated October14 tasks and CPSS
were untouched. No desktop, dispatch, CLI continuation, agents or worktrees used.

Primary evidence on R1, outside Git:
/home/Su/experiments/CPNetFlux/2026-11-08-v3-sender-cpu-11

- analysis.json, metrics/*.verified.json, matrix.log and checksums/source.json.
- acceptance/matching-builds-sender11-2bd8280e671e.json and per-arm identities;
  build-r1/r2.log, ctest-r1/r2.log and red-sender-cpu.log.
- R770-1/R770-2 logs and snapshots, per-sample cleanup/checkpoint receipts.
- sender_harness_11.py, sender_matrix_11.py, analyze_sender_11.py;
  checksum_split_11.cpp/binary, checksum-split-r1/r2.jsonl and resource receipts.
- cleanup-R770-1/R770-2.json, final_audit_11.py, evidence-sha256.json.

Harness dependencies retained outside Git:baseline_evidence.py under
2026-11-07-v3-tcp-scaling-audit-01 and v3_8g.py under2026-11-07-v3-path-isolation-01.
Scripts, dependency hashes, source identity and raw failures must accompany reuse.
Do not restore large historical perf payloads into the working tree.

Next independent scope:isolate resident source read/cache/CPU placement and
per-frame socket work under the same4-lane configuration. First record actual
user/system CPU, thread scheduling/migration and bounded source-service costs;
separate checksum update/combine in concurrent execution if needed. Only choose
a bounded data-supply or send-path candidate after it explains a repeatable gap.
Any candidate must retain CRC/lease/recovery and pass fixed-commit five-pair
complete-task >=10% median gain, CV<=5%, integrity and memory gates. Do not grow
queues/workers, retry page population, or treat lower isolated CPU as task gain.
