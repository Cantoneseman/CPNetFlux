# R770 RDMA end-to-end baseline: results

Date: 2026-11-04. Task: `2026-11-04-r770-rdma-e2e-baseline-01`.

**Collection complete; performance/correctness acceptance NOT passed.** All 16 cells have three recorded attempts: 42 pass and 6 fail. All six failures are 64 GiB V3 downloads. No failed run is assigned a successful task rate. All 18 successful real-file samples passed external SHA-256.

Evidence root: `/home/Su/experiments/CPNetFlux/2026-11-04-r770-rdma-e2e-baseline-01/` on R770-1. Raw peer logs and snapshots are also collected there. This report records a baseline and concrete defects; it is not 100G or production readiness.

## Scope, Inputs And Configuration

- Work was serial on R770-1, with R770-2 as transfer peer. No transfer source was changed.
- Measured executable revision: `a4ae9963db17bc061aadcfa8d95d13de6c86849f`; build manifest: `acceptance/build.json`. Both hosts had this commit at preflight and all four executable SHA-256 values still match the manifest at closeout.
- During the task R770-1 HEAD advanced to documentation-only `21502bdb4edab2bff51c690a89d0b9dbaef026e3`; R770-2 stayed at `a4ae996`. The complete diff is documentation/index files only. This metadata mismatch is disclosed; the measured source/binaries are identical. Final documentation is synchronized after publication.
- R770-1 `.1` and R770-2 `.2` use `192.168.100.0/24`, `eno17095np0` / `mlx5_0`, explicit source binds and verified direct routes. Ethernet MTU 1500, RDMA path MTU 1024. Control uses TCP; CPNetFlux samples explicitly select `data_transport=rdma` through its RDMA socket adapter. The native verbs reference is `ib_send_bw`.
- Both nodes: Xeon 6515P, 16 cores / 32 threads, one NUMA node. Experiment files are on root `/dev/sda2`, the approximately 446.6 GiB RAID system disk; the separate 2.2 TiB lab disk was not used.
- Sizes are total bytes per task: 4 or 64 GiB. Memory four-lane cases send one quarter of that total per independent client/server connection, rather than four full-size files.
- V3 transfers contain exactly one file, connections/file-parallelism 1 or 4, dynamic scheduling, tree data reuse, worker control reuse, pending window 8, 64 KiB buffer, CRC32C, compression/scheduler/TLS off, anonymous authentication, reconnect budget zero.
- Requested client chunk size is 4 MiB. Upload actually uses 4 MiB; download uses the server default 1 MiB. The harness did not explicitly set the server chunk-size option, and the client option does not override the download sender policy. This is a disclosed harness/configuration asymmetry, not proof that a documented client option is broken and not an equivalent 4 MiB download configuration.
- Upload server final verification is `full` for 4 GiB and `verified_chunks` for 64 GiB; file+directory fsync is requested. The tree download receiver retains its default full verification/checkpoint policy. External SHA-256 is outside timed runs. The asymmetric policies and uncontrolled page-cache state limit strict cross-direction comparisons.
- 64 GiB source data repeats the 4 GiB random input 16 times, with compression disabled. Successful repetitions start with a fresh destination and do not resume.

## Rate Definitions

- `payload steady`: native tool-reported average for ib_send_bw; sum of independent lane payload rates for socket bench; V3 logical payload divided by the first-to-last active HCA counter interval (>=256 KiB delta, sampled about every 100 ms). The latter includes storage stalls and is a payload-phase equivalent, not a claim that every interval is steady.
- Socket lane sums use different local timing windows and can exceed the native reference in short runs. Their common HCA-envelope estimates are recorded separately in `data/final-summary.json`; four-lane sums must not be called one-file throughput.
- `client task`: logical total bytes / external client process (or concurrent client group) wall time, including connection initialization, finalization, summary hashing and process teardown. It excludes external audit SHA-256.
- `instrumented harness`: before listener startup through listener teardown and post snapshots. It additionally includes SSH, sampler start/stop, counter collection and audit orchestration overhead. This deliberately conservative lifecycle rate is not a product latency metric; do not attribute its multi-second overhead to CPNetFlux.
- Filesystem physical rates use root-partition sector-counter deltas divided by the matching sampler window, independently on both nodes. They include background I/O and logs and can be zero on a warm page-cache read. They are not isolated storage ceilings. Logical file read/write effective rate equals successful client task rate.
- HCA port data units are converted using 4 bytes/count and include transport overhead; netdev counters are retained independently and do not necessarily account for RoCE payload. Do not equate either counter with logical file bytes.

## Matrix Results

Rates are Gbps; medians of three successes where present. Failed cells show no performance result.

| Path | GiB total | Connections/QPs | Pass/attempts | Payload steady* | Client task | Instrumented harness |
|---|---:|---:|---:|---:|---:|---:|
| native | 4 | 1 | 3/3 | 92.580 | 51.680 | 6.333 |
| native | 4 | 4 | 3/3 | 92.580 | 48.068 | 6.295 |
| native | 64 | 1 | 3/3 | 92.590 | 88.355 | 50.112 |
| native | 64 | 4 | 3/3 | 92.580 | 46.082 | 32.969 |
| bench | 4 | 1 | 3/3 | 30.262 | 9.906 | 3.829 |
| bench | 4 | 4 | 3/3 | 94.859 | 11.378 | 3.692 |
| bench | 64 | 1 | 3/3 | 30.224 | 26.829 | 21.135 |
| bench | 64 | 4 | 3/3 | 91.745 | 63.330 | 36.675 |
| v3-upload | 4 | 1 | 3/3 | 29.603 | 5.033 | 2.993 |
| v3-upload | 4 | 4 | 3/3 | 14.733 | 4.177 | 2.664 |
| v3-upload | 64 | 1 | 3/3 | 4.064 | 3.928 | 3.800 |
| v3-upload | 64 | 4 | 3/3 | 3.077 | 2.660 | 2.601 |
| v3-download | 4 | 1 | 3/3 | 29.518 | 6.709 | 3.509 |
| v3-download | 4 | 4 | 3/3 | 9.846 | 2.929 | 2.096 |
| v3-download | 64 | 1 | 0/3 | - | - | - |
| v3-download | 64 | 4 | 0/3 | - | - | - |

*See rate definitions; lane sums, counter-envelope estimates and native averages are distinct measurements. Per-sample values, ranges and failures are in `data/samples.csv`, `data/summary.csv`, `data/final-samples.json` and `data/final-summary.json`.

## Filesystem And Network Evidence

Median physical block-device rates during the sampled interval (Gbps). These include data, checkpoint/log I/O and cache effects. For failed cells they describe failed work, not successful throughput.

| V3 path | GiB | Connections | R1 read | R1 write | R2 read | R2 write |
|---|---:|---:|---:|---:|---:|---:|
| v3-download | 4 | 1 | 0.001 | 2.581 | 0.000 | 0.000 |
| v3-download | 4 | 4 | 0.000 | 1.633 | 0.000 | 0.008 |
| v3-download | 64 | 1 | 2.695 | 0.996 | 0.892 | 0.001 |
| v3-download | 64 | 4 | 0.000 | 0.004 | 0.000 | 0.003 |
| v3-upload | 4 | 1 | 0.000 | 0.000 | 0.000 | 3.444 |
| v3-upload | 4 | 4 | 0.000 | 0.000 | 0.000 | 3.042 |
| v3-upload | 64 | 1 | 3.830 | 0.001 | 0.000 | 3.862 |
| v3-upload | 64 | 4 | 2.620 | 0.001 | 0.000 | 2.832 |

- Across all accepted matrix windows, both nodes have zero deltas for netdev rx/tx errors, rx/tx drops and rx_missed_errors. This does not establish HCA error-free operation.
- All sample/host HCA, hardware and netdev deltas, disk bytes, QP maxima, routes and sampling windows are retained in `data/final-samples.json` and raw before/after snapshots/JSONL.
- RNR/out_of_buffer is not eliminated: 4 GiB single-connection upload has sender RNR deltas 93/50/58; 64 GiB single-connection upload has 2033/0/0. Four-connection uploads have zero RNR deltas in these samples. The label post-RNR-fix does not mean zero RNR for every configuration.
- Single-connection memory bench still records RNR and peer out_of_buffer; inspect final sample deltas rather than combining historical counts. Native ib_send_bw has zero RNR in this matrix.
- Each failed 64 GiB single-connection download records R1 local_ack_timeout_err +6, req_transport_retries_exceeded +1 and large CQ flush increments. Successful cases also have close-time CQ error/flush increments. They cannot all be dismissed as harmless or all attributed to payload errors; phase-specific tracing remains required.

## Historical 3.29 Gbps Comparison

The original result is `docs/tasks/2026-11-01-v3-rdma-stability-01-result.md`, backed by `/home/Su/experiments/CPNetFlux/2026-11-01-rdma-stability/large-random-01.summary.json` and `.time.txt`: one 4 GiB file, four connections, summary 3.29104394 Gbps, elapsed 10.440376663 s, external process wall 10.59 s.
Current same-summary metric is median 4.25164478 Gbps: +29.19%. Comparing external client wall consistently gives old 3.24454564 Gbps versus current 4.17702750 Gbps: +28.74%. The old result is one historical sample, with uncontrolled cache and some intervening changes; this is evidence of improvement, not an isolated causal A/B of the RNR repair.
The current 14.733 Gbps payload-phase result must never be compared directly with the old 3.291 task result. Four-connection 64 GiB upload remains only 2.660 Gbps client task, and both 64 GiB download cells fail. There is no general near-100G file-transfer result.

## Single-file Four-channel Concurrency

All successful four-connection file samples report one file, four data connections, channels 0/1/2/3 and eight ranges; observed QPs reach five including the pre-existing production QP. The 64 GiB uploads assign two 8 GiB ranges to each channel, 16 GiB per channel.
Temporal overlap can be established without comparing clocks on different hosts: client range wall times are sequential within each of four channels. Their sum / local task elapsed is about 3.90 active range operations. If at most three ranges ran at once, the sum could not exceed 3*T. The measured sum minus 3*T is 184.07/184.96/184.33 seconds, proving a substantial interval with four active range operations. Storage, checksum and waits are included; this is not proof of four simultaneously saturated wire streams.
Thus V3 really uses four channels for this one large upload, but does not reproduce the four independent memory-bench service rates. Four-connection 64 GiB upload is slower than one connection: 2.660 versus 3.928 Gbps. Source read timer sums are about 686 s across about 800 s of range operations, versus about 124 s of reads in a 136 s single-connection range. Physical source reads fall from about 3.83 to 2.62 Gbps. This strongly identifies storage service and parallel range access as the next measured bottleneck, but an isolated filesystem test is still needed before claiming a hardware storage ceiling.

## Reproducible Download Failures

1. 64 GiB / one connection: all three runs receive 68,719,476,736 bytes and report completed_files=1, but the overall task fails with `Work Request Flushed Error`. Internal elapsed is 457.270/456.919/460.424 s; external process time also includes final summary hashing. Median receiver finalize is about 151.5 s and summary tree hashing about 146.2 s. The RDMA receive wait is hard-coded to 30 s (`src/core/io/rdma_data_socket.cpp:486`), and persistent sessions also select 30 s. The fixed wait versus long commit/audit plus retry/flush counters is a concrete failure mechanism to trace. It has not been repaired or causally isolated in this baseline.
2. 64 GiB / four connections: all three fail before any logical payload, at 33.124/32.963/33.013 s, with `RDMA completion event timed out`. The server uses 1 MiB chunks, so an 8 GiB range produces 8192 missing entries. `RangeFileCoordinator::begin` splits the missing set per chunk; `encodeResumeResponsePayload` uses 8+16*N bytes. 8192 entries encode to 131080 bytes, while `PersistentDataSession::kMaxPayload` is 65536 and writeFrame rejects a larger frame. This size defect was reproduced against the measured static library in `scripts/resume-envelope.cpp`; output is `diagnostics/resume-envelope.txt`. At 4095 entries 65528 bytes fits; at 4096, 65544 does not. The client command chunk size is not propagated to the download sender. Error propagation then surfaces a peer timeout. A repaired build/rerun is still required before claiming a complete fix.
3. Failed client output can show a nonzero `throughput_gbps` based on planned bytes despite bytes_transferred=0. The closeout analyzer excludes these values entirely.

## Development Order And Gates

| Order | Change / experiment | Acceptance gate |
|---|---|---|
| P0-A | Bound the resume-mask representation: coalesce contiguous missing intervals or paginate with an explicit protocol contract; explicitly pin or negotiate download chunk/checkpoint/final-verify policy at the owning endpoint; preserve verified-chunk recovery semantics. | Boundary tests at 4095/4096/8192 entries, sparse/corrupt/resumed ranges, clear semantic errors, then three fresh 64 GiB four-channel downloads with full SHA-256. Do not merely raise a constant without bounding allocation and decoder behavior. |
| P0-B | Trace the FILE_END / commit / FILE_RESULT / DIRECTORY_END state machine; make waiting reflect transfer deadlines and explicit finalize progress. Make result/error reporting independent of long hashing and reconcile completed_files with final session status. | Reproduce >30 s receiver commit and audit, prove no false timeout or indefinite hang, test peer death/cancel, then three fresh 64 GiB single-channel downloads with zero recovery and full SHA-256. A timeout increase alone is a diagnostic control, not the final design. |
| P1 | Run sequential versus four-offset read-only storage controls on the actual source/destination filesystem with explicit cache state. Add per-process I/O, per-core CPU, range read/write, checksum, manifest, allocation/registration and data-connect traces. | At least three repeats per control; separate cache, storage and CPU service rates. Select storage capable of the desired sustained payload rate before setting a near-line-rate file gate. At 90 Gbps the payload alone is 11.25 GB/s per endpoint. |
| P2 | Profile/reuse registered pools and connections; reduce current roughly 2.16 GB receive pool per connection through controlled window budgets; batch post/CQ work while preserving slot ownership and backpressure. | Measured setup, RSS, RNR and long-transfer comparisons; no stale-slot/duplex corruption; matching builds and 64 GiB+ tests. Current 64 GiB memory task is about 63 Gbps despite lane payload sums around 92 Gbps. |
| P3 | Tune the existing bounded read/network/write pipeline and range scheduler with storage locality, then checkpoint/CRC and commit/audit work. Change one factor per experiment. | Improve one-file client task rate over this baseline with SHA-256, crash/resume and atomic publication tests; four channels must outperform one under a proven non-storage-limited control before being the default. |
| P4 | Freeze a single-port configuration, repeat both directions and representative payloads, then run sustained/fault tests. Only then evaluate multi-rail. | Suggested stages: 10-20, 30-50, then 70-85 Gbps verified file task rate if storage supports it. The measured native reference is about 92.59 Gbps. This does not validate 100 Gbps application payload or ten-port/1 Tbps scaling. |

The existing performance roadmap remains the parent plan; its stage-0 evidence is this report. Start P0 correctness before interpreting candidate optimizations. No transfer-source implementation has been changed by this collection task.

## Validation, Evidence And Cleanup

- Both prior same-build full CTest logs are retained under `acceptance/R770-{1,2}-full-ctest.log`: 483 CTest records, zero failed, including one hardware smoke skip because no explicit address was supplied. This is 482 executed passes plus one skip per host, not 483 hardware passes.
- The skipped hardware smoke was subsequently run with explicit `.1` and `.2`, serially, and passed on both nodes in about 6.9/6.8 s. Logs: `acceptance/R770-{1,2}-hardware-ctest.log`.
- `baseline.py` and `closeout.py` pass py_compile. Evidence audit: 48/48 records, no malformed or nonmonotonic active counter JSONL, and 18/18 successful-file SHA checks. Initial bench evidence is retained in `diagnostics/bench-initial`; one corrupted collection log is retained as diagnostic evidence and was replaced by a fresh measured run.
- The scope of a task rate, a filesystem rate and a counter is explicit above. Raw failed attempts are retained, not converted into low but successful rates. Cache state and unmatched download policy prevent treating this as a fully symmetric filesystem ceiling study.
- Failed download metadata/manifests and inventory were archived under `diagnostics/failed-file-metadata`, and generated payloads are reclaimed. Task-local source and peer seeds are removed only after evidence validation. Cleanup is audited in `acceptance/cleanup.json`.
- Production PIDs 126766/164834 and port 2121 remain alive; production data, CPSS, unrelated experiments and original cloud workspaces are untouched. The explicitly abandoned 2026-11-03 strong-gridftp task was reclaimed earlier; completed historical evidence is retained.
- Final source revision/binary checks, Git publication and remote SHA readback are recorded in `acceptance/publication.json`. Only the small repository result report is committed; large external raw evidence is not claimed to be backed up to GitHub.

Recompute evidence (after payload cleanup, do not run the original payload-generation matrix accidentally):

```sh
python3 /home/Su/experiments/CPNetFlux/2026-11-04-r770-rdma-e2e-baseline-01/scripts/closeout.py analyze
python3 /home/Su/experiments/CPNetFlux/2026-11-04-r770-rdma-e2e-baseline-01/scripts/closeout.py report
```
