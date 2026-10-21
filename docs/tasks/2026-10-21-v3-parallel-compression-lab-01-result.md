# V3-PARALLEL-COMPRESSION-LAB-01 Results

Updated: 2026-10-22 (Asia/Shanghai)
Owner: current commander only, serial execution, no subagents.
Branch/checkout: codex/V3-PARALLEL-COMPRESSION-01,
/root/projects/CPNetFlux-worktrees/v3-parallel-compression-01.
Initial runtime input: 2f78eca7f59f6b524e9aeccf31176e9be8ffffd6.
Plan commit: b28c861a1fbb26de14c2dad43ff627ee23dd4244.

## Verified Pair and TCP Baseline

R730: Su@192.168.1.112, data enp130s0np0 / 192.168.100.1/30.
R750: Su@192.168.1.110, data enp177s0np0 / 192.168.100.2/30.
Both interfaces report 100000 Mbps. Both data routes and bidirectional BatchMode
SSH work. The R730 ED25519 host key on the data IP was compared with its trusted
management SSH identity before adding it on R750. Existing services are preserved.

Eight-stream iperf3, three 5-second samples per direction, receiver throughput:

| Direction | Samples (Gbps) | Median (Gbps) | Sender retransmits (sum) |
| --- | --- | --- | --- |
| R730 to R750 | 33.252, 35.191, 25.923 | 33.252 | 0 |
| R750 to R730 | 22.672, 21.536, 28.164 | 22.672 | 8 |

This is TCP over the direct RDMA-capable Ethernet interfaces, not native verbs,
not a 100 Gbps end-to-end claim, and not cross-domain evidence. Source cache is
warm; no host-wide TCP tuning or cache dropping. R750 had about 69 GiB free on
its 92%-used root filesystem; R730 had about 64 GiB free before the matrix.

## Failed Pilot and Prerequisite Fixes

The first raw dense warm-up stopped after 992 reported file commits (995 shared
executor completions) out of 2048 files / 128 MiB, with five failures. Its final
error was `reconnect attempt budget exhausted: work retry limit exceeded`.
It is excluded from all performance comparisons.

The R750 SSH process soft descriptor limit is 1024. Integrity callbacks retain
FileTransaction objects until the batch ends, and each published transaction
retained its file descriptor. A new actual-client regression under a process-only
96-fd limit reproduced failure at 79 files. After successful publish, manifest
commit and temp unlink, commit now releases the writer and temporary descriptor.
Published identity/manifests remain available to the existing integrity callbacks.
No receiver replay/integrity policy or global resource limit was weakened.

Focused recovery also exposed a migrated-range attempt bug. Shared recovery
increments a work item's attempt, but the sender overwrote it with the current
lane attempt; a healthy lane could turn retry 1 into attempt 0 and trigger a
duplicate begin rejection. A socket regression claiming recovered work on a
different healthy worker failed with wire/callback attempt 0. The sender now
preserves the greater work/lane attempt. Old-lease commit rejection remains tested.

The corruption fixture assumed a sibling file committed before another lane
reached the fault threshold. Only that fixture now uses one lane/one pending
file to make its prior-commit condition deterministic. Multi-lane range/reset
tests are retained. One early full suite omitted CPNETFLUX_TEST_TOKEN and failed
two existing auth fixtures; the final full suite supplies a private test token.

Raw pilot evidence is retained in Shenzhen at
/tmp/cpnetflux-runs/V3-PARALLEL-COMPRESSION-01/lab-initial-failure-20261021.tar.gz,
SHA-256 3399ebfc16f0529780039090e3fa3a22ce39a329e6a8f3f729b56c340e04744e.
Generated diagnostic payloads and temporary credential files were removed;
logs, summaries and scheduler evidence remain outside Git.

## Runner and Deployment Contract

The runner validates auto-mode negotiation and pool bounds while accepting zero
jobs when auto rejects incompressible input. It now builds all three server/tree
upload/tree download binaries and writes artifact-manifest.json with exact commit,
source archive SHA-256 and binary hashes. --build-artifacts can reuse that exact
artifact set for reverse-direction tests only after provenance/hash checks.
R750 APT's configured mirror returned HTTP 403 before installing missing build
headers. Its system source configuration was preserved; reuse of the exact R730
artifacts avoids that build-host dependency and keeps pair binaries identical.

Deployment is isolated under the task's /tmp workspaces and optional immutable
/home/Su/.local/opt/cpnetflux-compression/<runtime commit>/ prefix. The packaged
desktop/agent and formal system services are not replaced by this CLI experiment.
Temporary TLS keys/tokens, owned receivers and payloads are cleaned after each
run; retained logs and versioned binaries are not installed as a default service.

## Acceptance Commands

```bash
cmake --build /tmp/cpnetflux-v3-parallel-compression-build -j2
ctest --test-dir /tmp/cpnetflux-v3-parallel-compression-build -j1 \
  --output-on-failure --repeat until-fail:10 \
  -R 'HealthyWorkerPreserves|fd_bound|parallel_compression_directory_failure_smoke|parallel_compression_directory_recovery_smoke|cpnetflux_reconnect_smoke'
CPNETFLUX_TEST_TOKEN="$(openssl rand -hex 24)" \
  ctest --test-dir /tmp/cpnetflux-v3-parallel-compression-build -j1 --output-on-failure
python3 -m py_compile tools/test/run_cpnetflux_tree_fd_bound_smoke.py \
  tools/test/run_cpnetflux_reconnect_smoke.py tools/perf/run_parallel_compression_lab.py
python3 tools/perf/run_parallel_compression_lab.py --self-test
git diff --check
```

Repeated prerequisite gates: six CTest gates, ten executions each, all passed
(404.18 seconds). Final full-suite status and hardware matrix results are appended
after completion; no compression speedup claim is made by this prerequisite record.

Final full serial CTest: 430 total, 429 passed, one expected unavailable io_uring
skip, zero failures (197.70 seconds). Python compilation, lab self-test and
git diff --check passed. Logs: /tmp/cpnetflux-lab-prerequisite-build.log,
/tmp/cpnetflux-lab-prerequisite-repeat.log and
/tmp/cpnetflux-lab-prerequisite-full.log. The correctness build has no Release
optimization claim; the hardware runner performs its own pinned Release build.

Runtime 4a51e6eddde916c8c9f9ec868514ab455539e5bd passed all eight dense
cells on R730 to R750; the next 1 GiB raw warm-up failed before payload with
`link reserve exceeds admission limit: link0`. Investigation confirms range
planning already produced 16 x 64 MiB ranges: the harness left the aggregate
admission budget at its 64 MiB default, so concurrent claims exceeded it. The
harness now sets a 512 MiB metadata-work reservation budget, 100 Gbps capacity
and 1 ms conservative RTT explicitly. This budget is not a payload cache.
Runner SHA is recorded independently; both modes/directions retain identical
settings, and measured large/mixed cases require observed range completions.
The partial prior run is retained but excluded from the restarted matrix.
