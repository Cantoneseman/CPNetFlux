# V3 RDMA Startup 01 result

Status: measured improvement in cold complete-task time; stability remains open.
Input fd59059ab722d48f2c0036d90fe778fede878f15; profiling/baseline
eb1e4745ec20052ebff7105ec747ed31a6b4ee04; candidate
aa5efcc072b6a39d3e27caa9b9636b76ab7c5737. Each committed/pushed before peer sync.
Final documentation commit and GitHub SHA readback: external acceptance/github-backup.json.

## Change and attribution

The previous zero-filled vector incurred about one second per endpoint to
prepare a 2088.125 MiB receive pool. Active preparation precedes the connection
request; passive preparation lies inside the active CM wait. These times must
not be added to the inclusive data-connect timer a second time.

Connection-owned mmap storage now aligns logical buffers to 2 MiB, requests
process-local MADV_HUGEPAGE, then explicitly zeros all logical bytes as before.
The hint is best effort; ordinary pages remain valid. Queue depths, slot sizes,
wire/checksum/checkpoint/commit policies and thread ownership are unchanged.
MR deregistration still precedes destruction/unmapping. Alignment reservations
add at most 2 MiB plus page rounding of virtual space per pool; only logical
bytes are touched/registered. This is not an MR cache or cross-connection pool.

Independent native close-barrier probes observed zero AnonHugePages in old
receive mappings; old alignment assertion failed as intended. Candidate probes
on BOTH servers observed aligned 2138240 KiB receive VMAs, with 2138112 KiB
AnonHugePages (2088 MiB) and hg flag. This is measured probe backing; a successful
hint alone is not proof, and future fragmented environments may use base pages.

## Three-run medians

All runs: one lane, 1 GiB tmpfs source/target, native RDMA, explicit data binds,
CRC32C, compression off, depth 2, direct write, verified_chunks/every16 and
commit_sync=none. Same source, fresh output, same harness; no setup moved out
of timing. External SHA/warmup is outside the CLI timer on both versions.

| Metric | Baseline | Candidate |
|---|---:|---:|
| Complete task wall | 2.767690 s | 1.616185 s |
| Complete task rate | 3.103648 Gbps | 5.314944 Gbps |
| GNU time CLI elapsed | 2.740000 s | 1.590000 s |
| GNU time CLI rate | 3.135013 Gbps | 5.402475 Gbps |
| Data connect | 2.102050 s | 1.058050 s |
| File phase wall | 0.431120 s | 0.448104 s |
| File phase rate | 19.924709 Gbps | 19.169500 Gbps |
| Client CPU | 1.620000 s | 1.020000 s |
| Endpoint CPU estimate | 3.160000 s | 1.920000 s |
| Payload threads CPU | 0.780931 s | 0.791575 s |

Complete task rate +71.25%; wall -41.61%.
File phase rate is 3.79% LOWER in this comparison; no payload-path improvement
is established. Actual write wall rose from 0.233137 to 0.250874 s. Most savings
come from startup/cleanup work, not a higher steady file service rate.
Client CPU falls 37.04%; the sampled endpoint CPU sum falls 39.24% but is an
estimate (server last available /proc sample), not an exact terminal sum.

| Startup component | R770-1 baseline/candidate | R770-2 baseline/candidate |
|---|---:|---:|
| send_pool_allocated (s) | 0.007913/0.002699 | 0.019282/0.003204 |
| send_pool_registered (s) | 0.000756/0.000126 | 0.001219/0.000129 |
| receive_pool_allocated (s) | 0.980221/0.506167 | 1.006518/0.520619 |
| receive_pool_registered (s) | 0.046599/0.004637 | 0.025791/0.004636 |
| receive_preposted (s) | 0.000034/0.000035 | 0.000037/0.000034 |
| queue_pair_initialized (s) | 1.039269/0.515686 | 1.058401/0.530113 |
| cm_established_wait (s) | 1.064014/0.531356 | 0.002852/0.000705 |

One full-policy correctness control: task 1.766439 s / 4.862853 Gbps; file 14.463352 Gbps.
No repeated full baseline was run; this is not an estimate of full-policy gain.
Seven outputs pass external SHA, protocol terminals and native QP evidence;
all source/output residency checks pass and sender physical read_bytes is zero.
RNR/out_of_buffer deltas are zero in all seven formal samples.
Python Popen.wait(timeout) can add up to ~50 ms observation delay to outer task
wall; GNU time is reported separately at 0.01 s resolution. The ~1.15 s saving
is much larger than this uncertainty. Sequential groups of three are bounded
diagnostic evidence, not randomized, long-duration or production acceptance.

## Memory, tests and unresolved issues

R770-1: minimum available 25.545731 GiB; maximum owned RSS 2.068020 GiB, plus about 1 GiB tmpfs. Sampled process swap and host swap-out zero; guards pass.
R770-2: minimum available 25.060719 GiB; maximum owned RSS 2.067894 GiB, plus about 1 GiB tmpfs. Sampled process swap and host swap-out zero; guards pass.

Four new owner tests cover alignment/zeroing/bounds, ordinary-page behavior,
overflow/reallocation rejection and destructor unmapping. The baseline native
alignment probe fails; candidate probes pass. New unit tests provide positive
coverage; no claim that each of these new unit tests was run against old code.
RDMA duplex smoke additionally checks phase ordering, component accounting,
pool byte budgets, alignment, QP identities and hint/error observability.
R770-1 related 191/191 (52.07 s), full 551/551 (252.55 s); R770-2 related
191/191 (52.67 s), no skips. CMake builds and git diff --check pass. The first
candidate build exposed a missing const data accessor; corrected and rebuilt
before tests/commit/transfer. Failed and successful build logs are retained.
Full-suite peer-mapping evidence was preserved via its supported evidence-dir
option in generated CTest configuration, then exact original bytes restored.
No rejection occurred this round; the prior intermittent rejection remains
unexplained. No control-path fix or stability claim is made.

CQ response/flush deltas remain unexplained. R770-1 baseline pairs include
(65,64)/(129,128); candidate includes (129,128)/(321,320)/(193,192). R770-2
also has nonzero pairs. Some candidate samples have higher counts; neither
correct SHA nor zero RNR proves these harmless. Investigate close ownership
and CQ draining before widening concurrency or claiming stability.

## Cleanup, backup and next work

All seven targets were removed after validation. Source inode 25, 1 GiB, SHA
7180843e27cbffd32d3a4f7cb2767731722e33fdc94b2b781c64b037ab27215e
was rechecked before deletion; payload roots absent on both endpoints.
12 owned peer-mapping fixture directories (381633791 bytes) were hashed and
removed; logs, summaries and cleanup receipts remain. Production PIDs
126766/164834, idle GSI QPs and the two old untracked docs are preserved.
The final fixed-build receipt must match both commits/config/version and eight
binary SHA256 values, with GitHub remote readback. Raw evidence stays outside
Git under /home/Su/experiments/CPNetFlux/2026-11-06-v3-rdma-startup-01.

Next independent scope: attribute response/flush counters to payload vs close
phases and audit QP/CQ ownership/teardown with existing close barriers. Retain
1 GiB memory workload and guard; do not expand queues/lanes yet. Remaining
~0.5 s per receive pool zeroing is a future measured hypothesis; removing
explicit touches requires proving pinning/private-page/zeroing semantics and
counting any migrated page-fault cost inside setup. File writes (~0.25 s/GiB)
and receiver CPU remain bottlenecks after startup. No 100G or persistent-disk
performance result is established.
