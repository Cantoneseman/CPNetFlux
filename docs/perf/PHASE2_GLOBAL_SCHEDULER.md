# Phase 2 Feedback-Driven Global Scheduler

## Scope

Phase 2 promotes the global scheduler connection count from a fixed value to a
runtime target selected by feedback. The implementation remains file-level:
planned WorkItems provide ordering and accounting, while existing framed
STOR/RETR executors perform the transfer.

The completion truth source remains the tree manifest, per-file manifest, and
`verified_chunks`. Scheduler queue state, target connections, ramp events, and
metrics are explanatory only.

The `off` mode is the default. `global/fixed` is the Phase 1 comparison path.
`global/adaptive` starts conservatively at one connection and ramps toward the
CLI `--connections` limit after stable low-watermark observations. High
watermark, send/write/CPU pressure, pause state, and retries can ramp down or
pause later dispatches. Hysteresis prevents a single low-watermark sample from
causing a ramp.

## Commands

Local build and tests:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m py_compile \
  tools/perf/run_gridftp_tree_private_matrix.py \
  tools/perf/analyze_phase1_global_scheduler.py \
  tools/perf/analyze_phase2_global_scheduler.py \
  tools/test/run_gridftp_tree_scheduler_smoke.py
```

Representative local comparison:

```bash
python3 tools/perf/run_gridftp_tree_private_matrix.py \
  --remote local \
  --server-host 127.0.0.1 \
  --local-build-dir build \
  --remote-build-dir build \
  --output-dir tools/perf/results/<timestamp>_phase2_local_matrix \
  --datasets small \
  --directions upload,download \
  --scheduler-modes off,global \
  --scheduler-policies fixed,adaptive \
  --file-parallelisms 1 \
  --connections 2 \
  --checksums crc32c \
  --resume \
  --repeat 1
```

Analyze the generated raw and summary CSV:

```bash
python3 tools/perf/analyze_phase2_global_scheduler.py \
  --raw-csv <raw.csv> \
  --summary-csv <summary.csv> \
  --output <phase2-analysis.md>
```

For the Shenzhen/Shanghai private matrix, use the same command with the
configured remote client and reachable server address. Public 10M cloud
traffic can establish correctness, resume, backpressure, queue behavior, and
metrics explainability only. It is not 100G readiness evidence.

Canonical Shenzhen server / Shanghai remote-client matrix:

```bash
python3 tools/perf/run_gridftp_tree_private_matrix.py \
  --remote cpnetflux-beta-shanghai \
  --server-host 120.25.121.51 \
  --server-bind-host 0.0.0.0 \
  --local-build-dir build \
  --remote-build-dir /root/projects/CPNetFlux-Beta/build \
  --output-dir tools/perf/results/<timestamp>_phase2_private_matrix \
  --datasets small,mixed \
  --directions upload,download \
  --scheduler-modes off,global \
  --scheduler-policies fixed,adaptive \
  --resume \
  --file-parallelisms 1,2 \
  --connections 2 \
  --checksums crc32c \
  --repeat 1 \
  --case-timeout 900
```

Use the SSH alias, not raw `root@47.116.174.181`, because the alias carries the
configured non-interactive private key. The public Shenzhen EIP is NATed and
cannot be bound by the local server, so the matrix binds the server to
`0.0.0.0` while the Shanghai client connects to `120.25.121.51`.

For this exact matrix, allow Shanghai-to-Shenzhen traffic from
`47.116.174.181/32` to Shenzhen TCP `22310-23250` for control and
`23300-24751` for passive data. The passive range covers each case's data-port
base through `24240` plus the CPNetFlux server's 512-port passive scan window.
Keep Shanghai SSH reachable from Shenzhen for the remote client launcher.

Bounded cross-host backpressure probe:

```bash
python3 tools/perf/run_gridftp_tree_private_matrix.py \
  --remote cpnetflux-beta-shanghai \
  --server-host 120.25.121.51 \
  --server-bind-host 0.0.0.0 \
  --local-build-dir build \
  --remote-build-dir /root/projects/CPNetFlux-Beta/build \
  --output-dir tools/perf/results/<timestamp>_phase2_private_backpressure_probe \
  --datasets small \
  --directions upload \
  --scheduler-modes global \
  --scheduler-policies adaptive \
  --file-parallelisms 1 \
  --connections 2 \
  --checksums crc32c \
  --scheduler-workitem-min-bytes 262144 \
  --scheduler-workitem-max-bytes 524288 \
  --repeat 1 \
  --case-timeout 900
```

The backpressure probe requires `queue_high` evidence. A `ramp_down` event is
not required there because adaptive starts at its lower bound of one connection.

## Evidence Requirements

Each global case must retain:

- `scheduler_summary.csv`
- `scheduler_events.jsonl`
- `scheduler_samples.csv`
- tree client JSON summary and client/server logs
- auth preflight JSON and final process audit JSON

The raw matrix CSV records the sidecar paths, policy, result, elapsed time,
throughput, source/destination tree hashes, resume partial result, server bind
host, auth source label, and audit paths. The summary CSV groups by dataset,
direction, resume, file parallelism, connections, scheduler mode, scheduler
policy, checksum, and backend.

Adaptive evidence should show `workitem_dispatch`, `executor_complete`,
`queue_low` or `queue_high`, and `ramp_up`/`ramp_down` when the selected
feedback conditions occur. A successful tree hash is still required even when
the scheduler reports a pressure or queue event.

The executor now reports per-file stage observations without changing the
framed data path: thread CPU is sampled with `RUSAGE_THREAD`, while send/write
pressure is derived from the corresponding measured phase time divided by the
file executor wall time. These are bounded feedback signals, not a claim that
the client can observe NIC queue depth. The summary appends
`max_send_pressure`, `max_write_pressure`, and `max_cpu_pressure`; executor
completion events include the observed values in `message`.

## Current Validation Record

On August 26, 2026, the final representative local matrix at
`tools/perf/results/20260826T104741Z_phase2_local_matrix_final_audit/` completed
12/12 cases:
upload/download, fresh/resume, and `off`, `global/fixed`,
`global/adaptive`. All tree hashes matched. All eight global cases retained
complete scheduler sidecars. Adaptive cases recorded a target increase from
one to two connections, and executor events contained CPU/send/write
observations. The observed small-file result was effectively equal to fixed
within the single-repeat measurement noise. The dedicated backpressure probe
at
`tools/perf/results/20260826T103959Z_phase2_backpressure_observed/` also
passed with equal source/destination hashes and recorded one `queue_high`
transition plus repeated high-watermark hits while the ready queue drained.

The final local verification also passed `cmake --build build`, the focused
20-test scheduler/options filter, scheduler smoke, Python bytecode checks,
`git diff --check`, `bash -n tools/perf/sync_remote.sh`, and full CTest
`199/199` (one environment-dependent io_uring test skipped).

The Shenzhen/Shanghai remote matrix was not completed in this run because SSH
authentication to `root@47.116.174.181` was unavailable. The route was
reachable, but `remote_auth` reported `auth=none` and the non-interactive SSH
probe returned `Permission denied (publickey,password)`. No password, private
AGENTS.md content, or key material was written to the repository. No
cross-region correctness or performance conclusion is drawn from that blocked
attempt.

On August 27, 2026, the cross-host exit-gate harness audit was retained at
`tools/perf/results/20260827T022024Z_phase2_private_exit_gate/`. The configured
SSH alias `cpnetflux-beta-shanghai` passed a BatchMode probe using a dedicated
key. `remote_auth.py --status` reported no password source, so the matrix
records the non-secret source label as `ssh_key_or_agent` when that BatchMode
probe succeeds.

The Shenzhen server must bind `0.0.0.0` because the public EIP
`120.25.121.51` is NATed and cannot be bound locally. Shanghai successfully
reached the low matrix endpoint: control `22310`, `EPSV` returning passive
port `23300`, and a TCP data connection to `23300`. The true upper control
endpoint `23250` still timed out from Shanghai while the Shenzhen server was
listening locally, so the canonical 48-case cross-host matrix was not started.
No cross-host raw/summary CSV or analyzer report is claimed for this blocked
attempt.

The same audit passed Shenzhen and Shanghai `cmake --build build`, full CTest
`199/199` on both hosts with the expected io_uring availability test skipped,
standalone scheduler smoke on both hosts, Python bytecode checks,
`bash -n tools/perf/sync_remote.sh`, `git diff --check`, and a local one-case
harness smoke for `--server-bind-host`. Final process audits found no residual
CPNetFlux server or transfer process. The remaining blocker is security-group
coverage for the upper matrix port range, specifically Shanghai-to-Shenzhen
control `22310-23250` and passive data `23300-24751`.

On August 27, 2026, a fresh upper-bound recheck was retained at
`tools/perf/results/20260827T053137Z_phase2_private_exit_gate_recheck/`.
The Shenzhen server successfully listened on `0.0.0.0:23250` with passive
base `24751`, but Shanghai again timed out connecting to
`120.25.121.51:23250`. Consequently the required `USER`/`PASS`/`EPSV` upper
probe did not run to completion, and the canonical 48-case matrix remains
`BLOCKED`. No cross-host raw CSV, summary CSV, analyzer Markdown, sidecar,
resume, or backpressure result is claimed from this attempt.

On August 27, 2026, after the security-group change, the effective public-EIP
path passed a fresh upper-bound preflight. Shenzhen listened on
`0.0.0.0:23250` with passive base `24751`; Shanghai, through the
`cpnetflux-beta-shanghai` BatchMode SSH alias, connected to
`120.25.121.51:23250`, completed `USER cpnetflux`, `PASS cpnetflux`, `EPSV`,
connected to the returned passive port `24751`, and completed `QUIT`. The
preflight evidence is retained at
`tools/perf/results/20260827T070715Z_phase2_private_exit_gate/20260827T_network_preflight.log`
and the temporary server log is in the same result directory. This proves the
effective route and port behavior, not merely a console rule edit.

The canonical cross-host matrix was then run from Shenzhen with:

```bash
python3 tools/perf/run_gridftp_tree_private_matrix.py \
  --remote cpnetflux-beta-shanghai \
  --server-host 120.25.121.51 \
  --server-bind-host 0.0.0.0 \
  --local-build-dir /root/projects/CPNetFlux-Beta/build \
  --remote-build-dir /root/projects/CPNetFlux-Beta/build \
  --output-dir tools/perf/results/20260827T070715Z_phase2_private_exit_gate \
  --datasets small,mixed \
  --directions upload,download \
  --scheduler-modes off,global \
  --scheduler-policies fixed,adaptive \
  --file-parallelisms 1,2 \
  --connections 2 \
  --checksums crc32c \
  --resume \
  --repeat 1 \
  --case-timeout 900
```

The canonical raw CSV is
`tools/perf/results/20260827T070715Z_phase2_private_exit_gate/20260827T070904Z_gridftp-tree-private-matrix.csv`
and the summary CSV is
`tools/perf/results/20260827T070715Z_phase2_private_exit_gate/20260827T070904Z_gridftp-tree-private-matrix-summary.csv`.
All `48/48` rows passed and tree hash mismatches were `0`. The matrix contains
`16` `off` rows and `32` global rows. Every global row retained all three
sidecars, for `32/32` complete cases. Resume partial evidence was present on
`24/24` resume rows as `expected_fail`; the final tree transfer result for all
48 rows was still `pass`.

The analyzer output is
`tools/perf/results/20260827T070715Z_phase2_private_exit_gate/PHASE2_ANALYSIS_CANONICAL.md`.
The JSONL audit found no malformed lines. Fixed cases held
`initial/current/target/max = 2/2/2/2` with no ramp events. Adaptive cases
started at one connection and reached the target of two, recording `20`
`ramp_up` and `4` `ramp_down` observations. Global cases recorded
`workitem_dispatch`, `executor_complete`, and `queue_low` events; the
canonical run had no `queue_high` event. Upload summaries recorded bounded
send-pressure observations, while write and CPU pressure counters remained
zero; these are explanatory executor measurements and do not imply NIC queue
visibility.

Because the canonical run had no `queue_high`, a bounded one-case probe was
run with scheduler work-item bounds `262144-524288`:
`tools/perf/results/20260827T080129Z_phase2_private_backpressure_probe/`.
It passed with matching tree hashes, complete sidecars, `1` `queue_high`,
`1` `ramp_up`, and no `ramp_down`. The probe analyzer is
`tools/perf/results/20260827T080129Z_phase2_private_backpressure_probe/PHASE2_ANALYSIS_PROBE.md`.
No `ramp_down` is expected for this probe because adaptive starts at the
one-connection lower bound.

The combined audit is retained at
`tools/perf/results/20260827T070715Z_phase2_private_exit_gate/PHASE2_ANALYSIS.md`.
Authentication was recorded only as the non-secret label
`ssh_key_or_agent`; no password, key path, or private `AGENTS.md` content was
written. The harness auth and process audits are
`20260827T070904Z_auth_preflight.json` and
`20260827T070904Z_process_audit.json`. A final exact-executable process audit
at `20260827T_final_process_audit.txt` found no residual CPNetFlux server,
file client, tree client, or matrix-runner process on either host.

The Shenzhen and Shanghai build, full CTest, and standalone scheduler-smoke
logs are retained in the canonical result directory. Both hosts passed build
and CTest `199/199`, with the expected environment-dependent io_uring test
skipped. Shenzhen also passed Python byte compilation, `bash -n
tools/perf/sync_remote.sh`, and `git diff --check`.

This is a `PASS` for the Phase 2 private exit gate within the stated 10M
public-cloud boundary: correctness, tree-hash equality, resume evidence,
bounded queue/backpressure evidence, and scheduler metric explainability.
It is not a 100G readiness result.

## Boundaries

- No daemon, Prometheus/HTTP metrics, data-channel pool, protocol multiplexing,
  NIC/NUMA pinning, or lossy compression is introduced.
- Compression remains sampling-only and raw-first.
- Scheduler state never marks a file or tree complete.
- Adaptive changes only future file dispatch targets and requires stable
  low-watermark observations before ramp-up.
- No 100G readiness claim is made from local or 10M cloud results.
