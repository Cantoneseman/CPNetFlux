# V3-PARALLEL-COMPRESSION-01 First Implementation

Date: 2026-10-21
Status: implementation/code gates complete; lab deployment/performance pending
Branch: codex/V3-PARALLEL-COMPRESSION-01
Input implementation: 390774cad2eaf3b58cb19e0a86ca3d3e02a2699b
Plan/decision commit: 3660ac5d1a3abaa978da7312072d2d6cb66e0041
Checkout: /root/projects/CPNetFlux-worktrees/v3-parallel-compression-01
Owner: current commander only; no subagents or chat handoff

## Delivered

- New shared parallel compression executor: fixed four workers, sixteen jobs and 256 MiB payload reservation ceiling; admission backpressure and cancellation checks. Tasks/results retain their budget through completed-result lifetime and socket writes. Encoder failures preserve raw bytes and keep workers alive; destructor joins workers safely.
- The V3 payload pipeline submits compression from its producer and emits FIFO results from its sender. Its existing chunk/byte bounds continue to count logical bytes after raw ownership moves into the executor.
- Existing zlib DATA framing is retained. CRC32C still covers logical bytes; checkpoint/range identity, offset writes, manifest and finalize remain in the existing transaction path. Poor-ratio or failed encoding sends a recorded raw frame.
- COMPRESSION=zlib is negotiated for every V3 control lane in both directions and renegotiated on reconnect. Unknown/duplicate codecs and unnegotiated compressed directory DATA are rejected; a dynamic batch cannot mix compression policies.
- JSON records requested/negotiated codec, raw/compressed frames, byte accounting and observed worker/job/byte peaks. Pool peaks are local-process lifetime values, not isolated per-task or remote-worker measurements. Download compression happens at the remote sender.
- No peer source files were copied. The colleague branch was used as the behavioral reference recorded in the task; implementation uses CPNetFlux interfaces and the existing zlib dependency.

Files: CMakeLists.txt; parallel_compression_executor header/source/test; payload_pipeline header/source; persistent_tree_transfer header/source; tree_transfer_client; control_server; persistent_data_session_test; compression smoke script; lab runner; this task/decision/baseline/board.

## Verification

Build directory: /tmp/cpnetflux-v3-parallel-compression-build
Configuration: Linux/g++ 11.4, core tests on, TLS on, desktop and io_uring off. The ordinary correctness build does not imply a performance build; the lab runner creates a new Release build from a Git archive.

Commands executed:

```bash
cmake --build /tmp/cpnetflux-v3-parallel-compression-build -j2
ctest --test-dir /tmp/cpnetflux-v3-parallel-compression-build \
  -R 'cpnetflux_parallel_compression.*smoke|ParallelCompression' --output-on-failure
CPNETFLUX_TEST_TOKEN="$(openssl rand -hex 24)" \
  ctest --test-dir /tmp/cpnetflux-v3-parallel-compression-build -j1 --output-on-failure
ctest --test-dir /tmp/cpnetflux-v3-parallel-compression-build \
  -R 'cpnetflux_parallel_compression.*smoke|cpnetflux_release_hygiene' --output-on-failure
python3 -m py_compile tools/perf/run_parallel_compression_lab.py
python3 tools/perf/run_parallel_compression_lab.py --self-test
git diff --check
```

Results:

- Targeted compression suite: 14/14 passed, including nine executor tests, three persistent compressed range tests and two real-client smoke variants. Existing payload pipeline tests also passed in targeted and full runs.
- Full serial CTest: 424 total, 423 passed, one skipped (FileIoTest.IoUringContextReadWriteSmokeWhenAvailable), zero failed; 181.40 seconds.
- After that full run, the TLS smoke was strengthened to use a private temporary token in addition to required control/data TLS. Both real-client compression smoke variants plus release hygiene were rerun: 3/3 passed.
- Smoke: 67 files with repeated, random, small and empty content. Raw and auto upload/download SHA-256 trees matched. Auto compressed suitable frames and fell back on incompressible frames. Completed upload resume sent zero payload.
- Partial compressed range resume: only the missing 192 KiB of a 256 KiB file was sent after a verified 64 KiB checkpoint; output bytes matched. Separate-channel range commit and rejection of unnegotiated compressed DATA left no unchecked checkpoint.
- Shared-budget stress: twelve logical transfer threads share one executor, bounded jobs/bytes, valid decoded content and no retained reservations. Ordered task ownership, cancellation, encoder exceptions and task lifetime beyond executor lifetime are covered.
- Lab script self-test: four dataset shapes, repeatable hash/count/size accounting, rejection of parent cleanup paths, and EOF-driven child-server termination passed. SSH deployment and network collection remain untested on the lab pair.
- The first full attempt lacked CPNETFLUX_TEST_TOKEN: two existing authentication gates failed. A private temporary test token fixed the environment; the subsequent full runs passed. No credentials were written to Git or logs.

Logs retained outside Git: /tmp/cpnetflux-compression-build.log, /tmp/cpnetflux-compression-ctest-final.log, /tmp/cpnetflux-compression-combined-gates.log.

## Lab Handoff

Windows SSH configuration was read back: gridflux-lab-main is Su@192.168.1.110; gridflux-lab-small is Su@192.168.1.112. Both BatchMode SSH probes with five-second ConnectTimeout timed out on this run. This establishes unreachability from this host at that time, not a diagnosis that either machine is powered off. Data IPs/interface names have not been reverified; do not substitute management IPs and claim the RDMA path was used.

The runner tools/perf/run_parallel_compression_lab.py is intended to run on the lab sender once both machines are reachable. Use a committed/pushed version of this feature branch and verified data-interface values:

```bash
python3 tools/perf/run_parallel_compression_lab.py \
  --repository "$PWD" --commit "$(git rev-parse HEAD)" \
  --peer-ssh <verified-SSH-alias-to-R730> \
  --peer-ip <verified-R730-data-IP> \
  --local-interface <verified-R750-data-interface> \
  --peer-interface <verified-R730-data-interface> \
  --large-mib 512 --channels 8 --repetitions 3
```

The placeholders are deliberate: actual data-path configuration must be inspected before launch. Management SSH may be separate from the selected transfer IP.

Runner behavior:

1. Archive the exact Git commit, build isolated Release binaries, copy those exact binaries over SSH, compare both SHA-256 values and reject missing receiver runtime libraries. No packaged service or checkout is replaced.
2. Verify route-selected interfaces in both directions and available space. Record link speed where exposed, route, NIC byte/error/drop counters, load, memory/IO/CPU pressure and free space. Link speed is a reported interface property, not measured bandwidth.
3. Generate large repeated, large random, dense (2,048 files) and mixed (514 files) datasets. Defaults stay below 1 GiB per dataset; configurable large datasets are capped at 10 GiB each.
4. Use a private, unique temporary TLS certificate/key and token, required data TLS and CRC32C. Compare off/auto with the same persistent control/data reuse, dynamic file scheduling, eight channels and pending window four.
5. Warm up each mode once, then run three measured repetitions with alternating mode order. Record end-to-end wall time, logical throughput, actual protocol wire bytes, negotiated codec, observed pool bounds, GNU time client CPU/RSS, receiver CPU/RSS and both-side environment snapshots. SHA-256 verification is outside the timed client invocation and is required for acceptance.
6. Delete only generated receiver payloads after each accepted run. On exit close the server supervisor, remove generated local payloads and both-side temporary credentials, retain logs/JSON/cleanup evidence under /tmp/cpnetflux-runs/V3-PARALLEL-COMPRESSION-01/<unique-run>/. The supervisor also handles TERM/HUP and has a 30-minute watchdog. Inspect cleanup.json after interruptions.

No lab run was started. No RDMA, bandwidth, speedup or production-readiness claim is made. This comparison isolates compression off versus this parallel-compression feature; it does not quantify the gain over a one-worker encoder, native verbs, or GridFTP.

## Remaining Work

- Bring both lab endpoints online/reachable, inspect their current data IP/interface/route and resources, then deploy and run the above bounded matrix. Reverse direction can be run by swapping the verified sender/peer roles.
- Review the resulting ratio, CPU, network traffic and end-to-end time before any promotion. Incompressible input may become slower from encoding overhead; high-bandwidth links can be CPU/disk limited.
- Optional later slices: runtime worker tuning, a serial-compression control, adaptive codec selection and zstd/LZ4. No desktop UI feature was added here.
- This branch starts at the documented frozen V3 input and does not include the concurrent desktop/network-binding branch. Integration with that newer branch and default rollout require a separate verified stage.
